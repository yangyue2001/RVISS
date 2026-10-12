# 外设系统解耦评估（对照 QEMU）

> 范围：`include/device/device.h`、`src/device/*`、`src/machine/*`。
> 评判标准取 QEMU 的设备模型原则（它是"设备模型"这件事上最成熟的工业实现）。
> 结论先行：**骨架方向是对的，但有 3 个真实耦合点应尽快修，2 个结构债在加设备前修，
> 其余的明确记为 YAGNI（现在不做）。**
>
> **实施状态（2026-10-09）：R1–R5 全部完成**，实施记录见文末第六节。

---

## 一、评判标准：QEMU 的设备模型原则

| # | 原则 | QEMU 里的体现 |
|---|---|---|
| Q1 | **设备不知道自己的地址** | `MemoryRegion` 只是"一段空间的行为"；由板子 `memory_region_add_subregion(..., addr, mr)` 挂到地址空间 |
| Q2 | **中断线是对象，由板子接线** | `qemu_irq`；设备持有输出线，板子 `sysbus_connect_irq(dev, n, line)` |
| Q3 | **设备状态封装在实例里** | `DeviceState` + `opaque` 传递，不用文件级全局变量 |
| Q4 | **明确的生命周期** | realize / reset / unrealize；系统 reset 会复位设备 |
| Q5 | **设备间不直接调用** | 通过 bus/qdev 树交互；板子负责组装拓扑 |
| Q6 | **设备访问内存走地址空间**（DMA） | `AddressSpace`/`dma_memory_read()`，而不是直接碰宿主内存指针 |
| Q7 | **配置用属性** | qdev properties（FIFO 深度、reg-shift…），而不是编译期常量 |

---

## 二、逐条对照（现状 → 评估 → 改法）

### ✅ 已经符合的

- **回调 + opaque 的 `MmioRegion`**：方向正确，等价于 `MemoryRegion` 的 read/write 回调。
- **设备互不认识**（Q5）✓：`clint.c`/`uart16550.c`/`plic.c` 之间零引用。
- **板子负责装配**（Q5 的板级角色）✓：`machine.c` 决定实例化哪些设备、地址从 `MachineConfig` 来。
- **注册表集中**：MMIO 分发只在 `bus.c`，没有散落到各设备。

### ❌ P1：真实耦合点（建议尽快修，改动很小）

#### P1-1　设备自带地址（违反 Q1）

现状：`MmioRegion.base` 属于设备对象，`bus.c:34` 用设备自己的 `base` 做匹配；
`uart16550_init(0x10000000)` 把地址喂给设备。

问题：
- 设备"知道自己住在哪"，于是**同一设备无法被映射两次**（两个 16550 就会抢同一个
  `static MmioRegion region`），也无法"先构造实例、后决定地址"。
- 地址概念混在设备行为里，板子换地址要改设备参数。

改法：

```c
/* 设备只描述"多大、怎么读写" */
typedef struct MmioRegion {
  const char *name;
  paddr_t size;
  word_t (*read)(void *opaque, paddr_t offset, int len);
  void   (*write)(void *opaque, paddr_t offset, int len, word_t data);
  void   *opaque;
} MmioRegion;

/* 板子决定挂在哪（并在函数内做重叠检查，见 P1-3） */
void mmio_map(MmioRegion *region, paddr_t base);
```

#### P1-2　中断是全局编号、设备直接 raise（违反 Q2，最严重）

现状：`irq_set(IRQ_MTIP, true)`，其中 `IRQ_MTIP` 来自 `cpu/interrupt.h`。
**后果：设备层 include 了 ISA 头** —— `clint.c:3` 就是全项目唯一一处"设备依赖 RISC-V"
（我们一直在标榜设备层不认识 ISA，这里是漏网之鱼）。PLIC 更明显：它的输出本该是
"连到 hart 的 MEIP 线"，现在若照 CLINT 的写法写，就必须知道 ISA 的编号。

改法（对象化，QEMU 的 `qemu_irq` 最小版）：

```c
/* include/device/irq.h：一条中断线就是一个"可被拉高/拉低"的对象 */
typedef struct IrqLine {
  void (*set)(void *ctx, bool level);
  void *ctx;
} IrqLine;

static inline void irq_raise(IrqLine *l) { if (l) l->set(l->ctx, true); }
static inline void irq_lower(IrqLine *l) { if (l) l->set(l->ctx, false); }
```

- CLINT 持有 `IrqLine mtimer_irq, msip_irq`；PLIC 持有 `IrqLine meip_irq`；
  设备只在状态变化时 `irq_raise/lower`，**不知道对端是谁、更不知道"编号"**。
- 板子（`machine.c`）负责接线：把 CLINT 的线接到 CPU 的 MTIP 输入、PLIC 的线接到 MEIP 输入。
- CPU 侧提供一个输入端点（由 target 实现，例如 `IrqLine *cpu_irq_input(int mip_bit)`），
  在 set 回调里改 `cpu.mip` **那一位** —— "编号 ↔ 位"的映射只存在于接线处（板级），
  这正是 QEMU 里 `sysbus_connect_irq` 的位置。

#### P1-3　MMIO 注册不检查区间重叠（违反"不静默"的项目原则）

现状：`mmio_register()`（bus.c:22）只查数量上限，两个设备映射到重叠区间时
**后注册者永远查不到**（`region_find` 先命中先注册的），且没有任何提示。

改法：在 `mmio_map()` 里做区间重叠检查，冲突直接 `panic` 并打印两个设备名与区间
（配置错误应立刻暴露，这与 `mmu.c` 未实现 Sv39 时选择 panic 是同一条原则）。

### 🟡 P2：结构债（加第二个同类设备/加 reset 之前修）

#### P2-1　设备状态是文件级 static（违反 Q3）

现状：每个设备都是 `static MmioRegion region; static uint64_t mtime; ...`
→ **隐含假设"每种设备全局只有一个实例"**。

影响：双串口、PLIC 多上下文（每 hart 一个 context）、或单元测试里想同时建两个实例，
都会互相踩。当前单 hart 规模下不致命，但这是"以后一定会想改"的地方。

改法：把状态搬进实例结构体，通过 `opaque` 传递（QEMU 的 `DeviceState` 最小版）：

```c
typedef struct {
  MmioRegion mmio;       /* mmio.opaque = 本结构体 */
  IrqLine    mtimer_irq, msip_irq;
  uint64_t   mtime, mtimecmp;
  uint32_t   msip;
} ClintState;

void clint_init(ClintState *s);          /* 状态由板子提供存储 */
```

#### P2-2　没有 reset 生命周期（违反 Q4）

现状：只有 `xxx_init()`，`cpu_reset()` 不复位设备。将来加
"运行结束再跑一次"/回滚/`--reset` 时会踩坑（mtime/PLIC pending 残留）。

改法：引入最小的 `Device` 概念（名字 + reset 回调 + opaque），
`machine_reset()` 遍历所有设备复位。可与 P1-2 的接线表合并成一张"本机设备表"。

#### P2-3　`device.h` 是"设备大杂烩头"

现状：每加一个设备都要在 `include/device/device.h` 里加一行声明（现在已有
`clint_init`/`uart16550_init`/`plic_init`）→ 公共头随设备数增长，且任何设备变动
都会让所有 include 它的文件重编译。

改法：`device.h` 只留框架（`MmioRegion`/`IrqLine`/bus/`mmio_map`）；
每个设备自己的头（`include/device/clint.h` 等）只被板级文件 include。

### ⚪ P3：明确记下、现在不做（YAGNI）

- **设备属性/配置结构**（Q7）：等真的需要配 FIFO 深度/reg-shift 时再做，
  形式是"配置结构体传入 init"，不要加全局变量。
- **DMA 走地址空间**（Q6）：现在没有 DMA 设备。**加 sd 卡/网卡这类设备时必须注意**：
  设备访问客户机内存要经 `paddr_read/paddr_write`（将来是 AddressSpace），
  绝不能拿 `mem_host_ptr()` 直接读写 —— 那会绕过 MMIO 分发与 PMP 检查。
- **总线查找优化**：设备数 <20 时线性扫描无感；设备多了再按 base 排序/二分。
- **设备热插拔/qdev 树**：本项目不需要。

### 另外一个"半耦合"点：设备节拍由引擎驱动

现在 `engine/interpreter/interp.c` 里有：

```c
static inline void devices_tick(void) {
#ifdef CONFIG_HAS_CLINT
  clint_tick(sim.instret);
#endif
}
```

引擎因此知道"有 CLINT 这个设备"。改法很小：把这个函数下沉到 device 层
（`include/device/device.h` 声明 `void devices_tick(uint64_t instret);`，
实现在 `src/device/tick.c`，内部再 `#ifdef CONFIG_HAS_CLINT`），
引擎只调一个与具体设备无关的入口。

---

## 三、改进后的目标结构

```
include/device/
├── device.h            框架：MmioRegion、bus 接口、devices_tick（不含具体设备）
├── irq.h               IrqLine（中断线对象）
├── char/uart16550.h    设备的公开接口（实例结构体 + init/reset）
├── timer/clint.h       只被板级代码（machine.c）包含；
└── intc/plic.h         与 src/device 的分类目录一一对应

src/device/
├── bus.c           设备框架：mmio_map（含重叠/压 RAM 检查）/ dispatch
│                   + 设备节拍注册表 devices_tick(instret)（引擎唯一入口）
├── char/uart16550.c    Uart16550State + 行为   （QEMU: hw/char/）
├── timer/clint.c       ClintState + 行为       （QEMU: hw/timer/）
└── intc/plic.c         PlicState + 行为        （QEMU: hw/intc/）
    ↑ 设备按类别分子目录，与 QEMU 的 hw/ 组织一致；bus.c 是框架本身，不属任何类别

src/machine/machine.c
    板子：建实例 → mmio_map(addr) → 接线（设备输出线 → CPU 输入线）
```

数据流（改进后）：

```
客户机访存 → paddr → mmio_dispatch → (base,size,region) 表 → region->read/write(opaque)
设备状态变化 → irq_raise(自己持有的 IrqLine) → 板子接的 set 回调 → cpu.mip 某一位
板级代码是唯一知道"地址映射"和"谁连谁"的地方（等价 QEMU 的 board/sysbus）
```

---

## 四、迁移步骤（可分批验收）

| 步 | 内容 | 验收 |
|---|---|---|
| **R1** | P1-3 重叠检查 + P1-1 去掉 `MmioRegion.base`（引入 `mmio_map`） | `make check` 全绿；故意重叠两个区域 → 立刻 panic 并指出两个设备 |
| **R2** | P1-2 中断线对象化（`IrqLine` + 板子接线，CPU 提供输入端点） | 删除 `clint.c` 对 `cpu/interrupt.h` 的 include；中断行为不变（定时器中断仍能触发） |
| **R3** | 设备节拍下沉（`devices_tick`），`interp.c` 里不再有 `CONFIG_HAS_CLINT` | 引擎不再知道任何具体设备 |
| **R4** | P2-1/P2-2 实例化 + reset（为多实例/reset 铺路） | 单元测试里能并存两个 UART 实例且互不干扰 |
| **R5** | P2-3 拆设备头 | `device.h` 不再随设备数量增长 |

R1–R3 改动小、收益直接（消除 ISA 泄漏 + 消除静默错误），建议要做就一起做；
R4/R5 等真需要多实例时再做也不算晚。

---

## 六、实施记录（2026-10-09，R1–R5 已完成）

改动的文件：

| 文件 | 变化 |
|---|---|
| `include/device/irq.h`（新） | `IrqLine` 中断线对象 + `irq_raise/lower` |
| `include/device/device.h` | 框架化：`MmioRegion` 去掉 `base`；新增 `bus_init(ram…)/mmio_map/devices_tick`；删除 `irq_set/irq_get/irq_pending_mask` 与各设备 init 声明 |
| `include/device/{clint,uart16550,plic}.h`（新） | 设备各立门户：实例结构体 + init/reset |
| `src/device/bus.c` | 映射表 + **重叠/压 RAM/溢出检查**（失败返回 false 并打印两个设备名）+ 设备节拍注册表 |
| `src/device/{timer/clint,char/uart16550,intc/plic}.c` | 状态全部搬进实例结构体（不再有文件级 static 状态）；中断经 `IrqLine` 输出；按类别分子目录（QEMU 风格） |
| `src/target/riscv64/trap/intr.c` | 新增 `cpu_irq_input(mip_bit)` 输入端点；设备线电平存 `hw_pending`，查询时折进 mip |
| `src/machine/machine.c` | 板级：持有设备实例存储、`mmio_map` 映射地址、`cpu_irq_input` 接线、`machine_reset()` |
| `src/engine/interpreter/interp.c` | `devices_tick(sim.instret)`；引擎不再出现任何具体设备名 |
| `tests/unit/{test_bus,test_intr}.c`（新） | 总线冲突检查、中断线语义 |

**过程中发现并修掉的一个真 bug**：原来 `riscv_query_intr()` 用
`cpu.mip |= irq_pending_mask()` 把设备线折进 mip —— 这是**粘滞**的：
设备拉低线之后（例如软件把 mtimecmp 调大），mip 里的位仍然是 1，
会导致**永远误报中断**。现在按"有输入线的位 = 当前电平"更新
（`cpu.mip = (cpu.mip & ~hw_mask) | hw_pending`），并新增
`tests/unit/test_intr.c` 专门覆盖"拉低后必须不再挂起"。

**验证**（`make check`，7 项全过）：

```
test_bits 13 / test_bus 13 / test_decode 24 / test_intr 9 / test_rv64w 13
[smoke] UART 输出 + HIT GOOD TRAP
[irq]   CLINT 定时器中断 → 陷阱 → mret → HIT GOOD TRAP   ← 新增，端到端验证接线
```

机械检查（可复现）：

```sh
grep -rn "cpu/\|target/" src/device/ include/device/   # 无输出：设备层不认识 RISC-V
grep -rniE "clint|uart|plic" src/engine/               # 无输出：引擎不认识任何设备
grep -rn "irq_set\|IRQ_MTIP" src/device/               # 只在注释里出现
```

**遗留（有意为之，见第三/四节的 YAGNI 条目）**：设备属性配置、DMA 走地址空间、
总线查找优化、qdev 式热插拔。
