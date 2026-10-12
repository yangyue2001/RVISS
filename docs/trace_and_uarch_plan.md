# Trace 与微架构结构：架构规划

> **范围声明（2026-10-09 决定）：RVISS 是功能模型（functional model），不做时序模拟。**
> 本项目只对"实现的正确性"负责：给定同样的镜像，必须得到同样的体系结构状态。
> 因此本文中的"微架构"仅指**纯统计/行为层面的结构**（分支预测器含 PHR、cache 命中统计等），
> 它们不产生周期数、不改变执行、不引入任何时间概念。
>
> 目标：让"收集指令执行情况"（trace）和这类微架构实验能持续生长，
> 而**核心执行引擎一行都不用改**。
>
> 本文只讲规划与理由，不写实现。实现按第五节的分步路线走。

---

## 一、核心思想：核心只"发事件"，谁想分析谁订阅

现在 `engine/interpreter/interp.c` 的循环是：

```
取指 → 译码 → cpu.pc = snpc → exec() → 中断轮询
```

我们要在里面加**固定几个观察点**，把"发生了什么"以事件形式广播出去：

```
        ┌─────────────── engine / memory（核心，只发事件）───────────────┐
        │  DECODE(pc,instr,class)  RETIRE(...)  BRANCH(...)  MEM_*(...)  │
        └───────────────────────────┬───────────────────────────────────┘
                                    │  事件流（单向）
                        ┌───────────▼────────────┐
                        │  观察者注册表 (observers)  │
                        └───┬──────────┬─────────┬───┘
                            │          │         │
                      计数型 trace  流式 trace  微架构模型
                      (统计/BBV)   (文件/环形)  (预测器/cache，纯统计)
```

**核心永远不认识任何具体观察者**（它只 include `include/observe/observe.h`）。
这是整个设计的承重墙：加一种统计、换一个预测器，都不用碰 core。

### 事件在哪发（这一点决定了改动面的大小）

关键设计是：**所有观察点都落在 ISA 无关的文件里**。

| 事件 | 发出位置 | 为什么放这里 |
|---|---|---|
| `DECODE` | `engine/interpreter/interp.c` | 引擎手里已有 `Decode`（pc/instr/类别），不需要 target 配合 |
| `RETIRE` | 同上（`exec()` 之后） | 提交点唯一，统计与 BBV 的天然锚点 |
| `BRANCH` | 同上：`taken = (cpu.pc != s.snpc)` | **通用推导**：不区分 jal/jalr/beq，比较"下一条实际 pc"与"顺序 pc"即可得到 taken 与 target |
| `MEM_READ/WRITE` | `memory/vaddr.c` | 访存都必经这里，且这里不认识 RISC-V |
| `TRAP` | `engine/interpreter/interp.c`（收到 `riscv_raise_trap` 返回后） | 陷阱发生的时机只有引擎看得到 |

唯一需要 target 配合的是"给每条指令标一个**类别**"（ALU/LOAD/STORE/BRANCH/JUMP/SYSTEM…）：
在 `InstrEntry` 里加一个 `class` 字段，`instr.def` 加一列。这是纯元信息，
**不改动任何指令语义**。类别枚举对齐 NEMU 的 `instr_stat` 分类（便于以后对拍）。

### 事件长什么样

```c
/* include/observe/event.h */
typedef enum { EV_DECODE, EV_RETIRE, EV_BRANCH, EV_MEM_READ, EV_MEM_WRITE, EV_TRAP } EventKind;

typedef struct {
  EventKind kind;
  uint64_t  insn_index;     /* 全局第几条（= instret）：功能模型里唯一的"时间"锚点 */
  vaddr_t   pc;
  uint32_t  instr;          /* 原始编码 */
  uint8_t   class;          /* 指令类别 */
  union {
    struct { bool taken; vaddr_t target; } branch;
    struct { vaddr_t addr; int size; word_t value; } mem;
    struct { word_t cause; vaddr_t epc; } trap;
  };
} Event;

typedef void (*ObserveFn)(const Event *ev, void *ctx);

void observe_init(void);
int  observe_attach(ObserveFn fn, void *ctx);   /* 返回订阅句柄 */
void observe_detach(int handle);
void observe_emit(const Event *ev);             /* 核心调用；无订阅者时是一个分支预测 */
```

### 为什么用"事件 + 回调"，而不是让收集器直接读 `cpu` 状态

1. **解耦方向正确**：收集器若直接读 `cpu.gpr[]`/CSR，就必须知道 ISA 细节 → 换 ISA、加扩展、改
   `CPU_state` 都会波及所有分析代码。事件里只有 pc/地址/大小这类**语义通用的信息**。
2. **可组合**：同一条流可以同时喂给统计器、trace 文件、分支预测器（各自独立订阅）。
3. **可离线**：事件流可以落盘，之后喂给任何分析工具（甚至 Python 脚本），不用重跑模拟器。
4. **可证明无副作用**：观察者只读事件、只写自己的状态 → 功能正确性（以及 difftest 结果）
   与"开不开观察者"无关。这条是整个设计能放心长下去的前提。

### 性能：默认零开销

- `observe_emit()` 第一行是 `if (likely(observers_empty)) return;` —— 没订阅者时就是一次判断。
- 更强的做法是编译期开关 `CONFIG_OBSERVE`（Kconfig），关掉时 `observe_emit` 是空宏，
  一行代码都不产生。**默认关，做研究时开**。
- 流式 trace 必须支持"环形缓冲"模式：只保留最近 N 条（NEMU 的 `iqueue` 就是这个思路，
  但只有 32 条、只服务报错；我们做成通用可配的）。

---

## 二、Trace 子系统

**先分两类，别混在一起**（这是接口设计的关键）：

| 类型 | 数据特征 | 用途 | 是否常开 |
|---|---|---|---|
| **计数型** | 聚合量（几 KB） | 指令类别分布、热 PC Top-N、基本块向量 BBV | 可以常开 |
| **流式** | 逐条记录（GB 级） | 回放、调试定位、喂给离线工具 | 按需开，环形/压缩 |

计数型里最重要的一个是 **BBV（基本块向量）**：`基本块ID → 执行次数`。
它正是 SimPoint 采样和 checkpoint 生产链的输入 —— 也就是说，
**trace 子系统与 checkpoint 子系统在上游共用同一份事件流**，这是把它们设计成观察者
而不是塞进 core 的第二个理由。

文件划分（计划）：

```
include/observe/
├── observe.h        注册表 + attach/detach/emit
└── event.h          事件定义（唯一权威）

src/trace/
├── trace.h          对外的"开关与汇总"接口（monitor 只认这个）
├── counter.c        类别计数 + 热 PC 直方图（EV_RETIRE）
├── bbv.c            基本块向量（EV_RETIRE，按 BB 边界切分）
├── stream.c         流式输出：文本(grep 友好) / 二进制(快) / 环形内存(报错 dump)
└── report.c         把收集结果格式化成报告（CSV/表格）—— 与收集完全分离
```

**报告与收集分离**很重要：收集器只产出结构化数据，`report.c`（或 Python 脚本）
负责展示。这样"换个漂亮报表"不会动到数据通路，也能对历史数据重新分析。

---

## 三、微架构模型（含 PHR）

### 铁律：模型只观察，永不改变客户机状态

功能正确性由 core 保证；微架构结构只做两件事：**预测、统计**。
预测结果**绝不允许**反馈进 `cpu.pc` 或任何 ISA 状态。

为什么是铁律：一旦预测影响功能，就必须把预测器状态纳入 checkpoint/回滚/difftest，
正确性验证会立刻变得极其复杂（NEMU 把 profile/checkpoint/tcache 全塞进 `cpu-exec.c`，
就是这类复杂度失控的例子）。分离之后，同一份 trace 可以喂给任意多个模型做对比，
甚至可以离线跑。

### PHR 放在哪、需要什么

PHR（Pattern/Path History Register，模式/路径历史寄存器）是分支预测器的"历史寄存器"：

- **GHR（全局历史）**：最近 N 个分支的方向位串。`gshare = GHR ⊕ PC` 去索引 PHT，
  这是最经典的 PHR 用法。
- **Path history**：更细的路径信息（TAGE 类预测器用它做索引）。

**它需要的输入只有一样**：每个分支的 `(pc, taken, target)` —— 正是我们定义的 `EV_BRANCH`。
这就是为什么事件设计里 `BRANCH` 要带 `taken` 和 `target`（target 给 BTB 用，方向给 PHR/PHT 用）。

> 如果你说的 PHR 不是分支预测的 Pattern/Path History Register（比如是物理寄存器堆
> Physical Register File 等），告诉我，这一节的配置换成对应部件即可 ——
> **接口不变**：任何微结构都只订阅事件流。

文件划分（计划）：

```
src/uarch/
├── uarch.h           对外接口：uarch_init/attach/stats_report
├── uarch.c           把各部件订阅到事件流（唯一 include observe 的地方）
├── branch/
│   ├── phr.c         PHR/GHR：移位更新、快照、复位
│   ├── bht.c         2-bit 饱和计数器表（PHT）
│   ├── gshare.c      PHR ⊕ PC 索引 + 更新规则（可换成 TAGE 等）
│   ├── btb.c         分支目标缓存（pc → target）
│   └── predictor.c   组合以上，暴露 predict(pc) / update(pc,taken,target)
├── cache/
│   ├── cache.c       单个组相联缓存模型（LRU/PLRU 替换策略可选）
│   └── hierarchy.c   L1I/L1D/L2 组合与 miss 传递
└── stats.c           预测准确率、cache 命中率/MPKI 等聚合与输出
                      （只有无量纲统计量，没有周期数/IPC —— 见"为什么不做时序"）
```

每个部件都是"订阅 + 自己维护状态 + 提供统计"的模板，加一个新部件 = 加一个文件，
**在 Kconfig 里加一个开关**，在 `uarch.c` 里订阅一次。

### 为什么本项目不做时序（范围决定）

功能模型只有一个"时间轴"：**已执行的指令数**（`instret`）。这是有意为之：

- 时序模型（流水线、发射宽度、访存延迟、周期数）要求"每周期"的视角，
  而我们的 core 是"每条指令"推进的 —— 两者粒度不同，混在一起会让
  `cpu` 状态里出现"周期计数器""流水寄存器"这类非体系结构状态，
  于是 checkpoint、回滚、difftest **全部要跟着处理它们**，正确性验证立刻变复杂。
- 而我们的目标是正确性，不是性能数字。**一旦引入时序，模拟器的输出就不再是
  "确定的体系结构状态"，而是"某个性能模型的估计"** —— 这两件事必须分开。

所以：**预测器/cache 在本项目里的唯一输出是"命中率/准确率"这类无量纲统计量**，
它们不参与任何控制流决定。哪天真的需要周期数，正确做法是另起一个独立的
timing 模型消费事件流（功能是驱动、时序是观察），而不是改 core —— 但那不在本项目范围。

---

## 四、为什么这样划分（原则总结）

| 原则 | 具体体现 | 理由 |
|---|---|---|
| 单向依赖 | core → `observe_emit`；观察者 → 事件。core 不 include 任何观察者 | 观察者可以无限增加，core 保持冻结 |
| 观察点在 ISA 无关层 | 事件发在 `interp.c`（引擎）与 `vaddr.c`（内存） | 换 ISA/加扩展不波及分析代码 |
| 事件是唯一契约 | 所有工具共享 `event.h` | 加工具不改 core；工具之间可以复用数据 |
| 观察者无副作用 | 模型只读事件，不写客户机状态 | 功能正确性可独立验证；difftest 依然有效 |
| 可关闭 | 编译期 `CONFIG_OBSERVE` + 运行期订阅计数 | 默认零开销；研究模式才付代价 |
| 数据与展示分离 | `counter/stream` 产数据，`report` 出报告 | 报表变化不动数据通路；历史数据可重分析 |
| 与 checkpoint 同源 | BBV 是两者的公共上游 | 避免"两套采集逻辑"漂移 |

**被否掉的方案**（以及为什么）：

- ❌ 在 `interp.c` 里直接 `if (config.trace) fprintf(...)`：每加一种统计都改 core；
  这正是 NEMU 现在的样子（`Logti`/`instr_stat` 都埋在循环里）。
- ❌ 让预测器写回 `cpu.pc`（"用预测结果驱动执行"）：功能与性能纠缠，正确性验证爆炸。
- ❌ 把统计做成"事后解析日志文本"：文本日志丢信息（没有类别/访存值/taken），解析慢且脆。

---

## 五、路线图（每步独立验收，互不阻塞）

> **前置条件**：S4 起（做微架构实验）**之前**，正确性基础设施必须先就位 ——
> 接入 `riscv-tests` 并与 Spike 对拍（见 implementation-plan 的 M5/M7）。
> 功能模型的第一优先级永远是"做得对"，其次才是"看得清"。
> S1–S3（事件流与 trace）本身也是**正确性的调试利器**，可以与测试建设并行推进。

| 步骤 | 内容 | 验收标准 | 改动面 |
|---|---|---|---|
| **S1** | 事件流骨架：`Event` 定义、注册表、在 4 个观察点埋 `observe_emit`；`InstrEntry` 加 `class` | 开启观察者后 smoke 测试与不开启时**逐指令一致**（可用现有 `-t` 日志对比） | core 一次性改动，之后冻结 |
| **S2** | 计数型 trace：类别计数、热 PC Top-N、BBV | `--trace-stats` 输出指令类别分布与 Top-20 热 PC | 全在 `src/trace/` |
| **S3** | 流式 trace：文本/二进制/Ring buffer（`--trace-ring=N`，出错自动 dump 最近 N 条） | 人为造一个非法指令，dump 能定位到出错前后的指令流 | 全在 `src/trace/` |
| **S4a** | 分支预测器：BTB + 2-bit BHT（先不做 PHR） | 输出预测准确率；与"完美预测器"对比 | `src/uarch/branch/` |
| **S4b** | 加 PHR/GHR → gshare；可切换预测器 | 同一份 workload 上 gshare vs BHT 准确率对比（能看出差别） | 同上，加一个文件 |
| **S5** | cache 模型（L1I/L1D）+ MPKI | 报告 miss 率；调整容量/关联度看趋势 | `src/uarch/cache/` |
| **S6** | （可选）报告工具、BBV 导出、与 checkpoint 链打通 | 统计出 CSV/表格；BBV 能被 SimPoint 脚本消费 | `src/trace/`、`tools/` |

建议顺序的理由：S1 是所有后续工作的地基，改动 core 只此一次；
S2/S3 立刻满足"收集指令执行情况"的需求且风险极低；
S4 起才是微架构研究（PHR 在 S4b 自然落位）。

---

## 六、编码规范与文件划分（本项目）

见 [coding_style.md](coding_style.md)。与本文最相关的四条：

1. **新的分析类子系统（trace/uarch）默认放 `src/<子系统>/`，公开接口进 `include/<子系统>/`**，
   private 头留在子系统目录内。
2. **不许从 `src/trace/` 或 `src/uarch/` 反向 include `src/target/riscv64/`**；
   需要 ISA 信息时，先在事件里补齐字段（改 `event.h`），而不是去摸 target 的头。
3. 每个观察者文件提供统一的 `xxx_init()/xxx_report()` 两个入口，注册动作集中在
   `uarch.c`（或 `trace.c`）一处，便于开关。
4. 统计输出用固定的机器可读格式（CSV/JSON），报告由 `report.c` 或脚本生成。
