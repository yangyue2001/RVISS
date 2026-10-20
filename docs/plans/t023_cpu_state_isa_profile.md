# T023：CPU 状态、ISA 能力和多 hart 预留设计

状态：design active（2026-10-13）。本任务先冻结边界和迁移顺序；本轮不实现多
hart，也不把 Sv48/Sv57 伪装成已经支持的能力。

## 结论先行

当前 `CPU_state` 把四类东西放在了同一个结构体中：

1. 客户机可以观察或修改的架构状态；
2. 实现 CSR 所需的隐藏状态；
3. 固定硬件能力；
4. 单 hart 执行辅助状态。

这在单 hart 原型阶段可以工作，但会让 checkpoint、difftest、ISA 配置和未来多 hart
边界混在一起。后续应按“谁拥有、谁修改、是否需要保存”拆分，而不是简单把结构体
缩短成“CSR + PC”。

目标分层如下：

```text
RiscvIsaProfile       不可变的硬件能力/实现参数，不属于客户机状态
        │ 引用
RiscvHart              一个 hart 的容器（未来引入，本轮不实现调度）
        ├── ArchState   可恢复、可对拍的 RV64 架构状态
        ├── HartRuntime 单 hart 执行辅助状态
        └── irq input   该 hart 的中断输入和 pending 快照

Machine                共享 RAM、设备实例、总线和平台时间
SimState               RVISS 引擎事件/预算/逻辑时间，不属于客户机 CPU 状态
```

## 1. `CPU_state` 不是只有 CSR、计数器、PC 和特权级

“CSR + PC + priv”仍然缺少几类架构可见状态：

- `gpr[32]` 是最基本的 RV64I 架构状态；
- `fpr[32]` 和 `fcsr` 是 F/D 扩展状态，Linux 上下文切换会使用；
- `pc`、`priv` 不是 CSR 存储成员，但决定当前执行点和有效权限；
- PMP 的 `pmpcfg[]/pmpaddr[]` 是 CSR 的后端存储，虽然 CSR 地址空间是数组形式；
- LR/SC reservation 不是 CSR，但会影响下一条 SC 的结果，需要在暂停/恢复时保持；
- `mip_software` 不是标准独立 CSR，而是软件写入的 pending 位，必须和设备中断线分开，
  才能实现 `mip = software_pending | hardware_pending`；
- `mcycle/minstret` 是客户机可见、可写的 CSR，不是“常量计数器”；
- `cycle/time/instret` 是视图或计算结果；RVISS 的 `SimState.instret/event_seq`
  又是宿主引擎计数，不能放进客户机 CSR 状态。

所以结构体变长本身不是错误。真正的问题是能力字段和状态字段没有清晰区分。

## 2. 当前字段分类

| 当前字段 | 实际性质 | 目标归属 | 处理意见 |
| --- | --- | --- | --- |
| `gpr[32]` | 架构状态 | `ArchState` | 保留，每 hart 一份 |
| `fpr[32]` | 架构状态 | `ArchState` | 保留，每 hart 一份 |
| `pc` | 架构执行状态 | `ArchState` | 保留，每 hart 一份 |
| `RVISS_CSRS_OWNED` 生成成员 | CSR 后端存储 | `ArchState` | 保留清单生成方式 |
| `priv` | 当前 hart 架构状态 | `ArchState` | 保留，每 hart 一份 |
| `mip_software` | CSR 读写与设备线之间的隐藏架构状态 | `ArchState` 或 `HartInterruptState` | 保留，不能删；不能把设备线直接写进软件存储 |
| `pmpcfg[16]` | PMP CSR 后端存储 | `ArchState.pmp` | 保留，但应和能力数量分开 |
| `pmpaddr[16]` | PMP CSR 后端存储 | `ArchState.pmp` | 保留，但应和能力数量分开 |
| `pmp_count` | 固定实现能力/板级 profile | `RiscvIsaProfile.pmp_entries` | 从架构状态移出；客户机不能改变 |
| `sv39_supported` | 地址翻译实现能力 | `RiscvIsaProfile.vm_modes` | 从架构状态移出，不能由 `satp` 写入改变 |
| `reservation_*` | A 扩展的 hart 局部架构执行状态 | `ArchState.reservation` | 保留；未来每 hart 一份，不能做成全局 |
| `mhartid` | 每 hart 固定 CSR 值 | `ArchState` 的 CSR 成员 | 保留在 CSR 清单；由 hart 初始化，不允许客户机写入 |
| `hw_pending`（当前在 `intr.c` 静态变量） | 设备到 hart 的输入线状态 | `HartInterruptState`/平台接线 | 当前单 hart 可静态保存，未来必须按 hart 分配 |
| `sim.instret/event_seq/logical_ticks` | RVISS 引擎状态 | `SimState` | 不放入 `CPU_state`，也不伪装成客户机 CSR |

### PMP 为什么不能简单说“已经在 CSR 里”

从体系结构观点，`pmpcfg` 和 `pmpaddr` 确实就是 PMP CSR 的存储；当前代码把它们
单独放在 `CPU_state`，是因为：

- PMP CSR 是可变索引数组，不适合直接作为普通 CSR 清单成员展开；
- `pmpcfg` 有锁位、WARL 和相邻 TOR 规则；
- `pmpaddr` 的读写需要按实现位宽和粒度掩码处理；
- 当前 CSR 表用 `riscv_pmp_read/write()` 处理这些副作用。

因此合理的重构不是删除这些数组，而是把它们明确归入 `ArchState.pmp`，并把
“有多少个 entry、粒度是多少”移到 profile。PMP 内容需要 checkpoint/difftest，
entry 数量属于硬件能力，不能混为一个字段。

### `mip_software` 为什么有必要

`mip` 的可见值不是单一存储：设备线可能产生 MTIP/MEIP/SEIP 等电平，软件又可以
写 SSIP/STIP/SEIP 的软件位。当前实现用：

```text
cpu.mip = software_pending | hw_pending
```

如果删掉 `mip_software`，软件写 `mip` 就会覆盖设备线，设备拉低线后也无法正确清除
对应的可见位。这个字段是实现 CSR 语义所需的隐藏架构状态，名称可以改成更明确的
`mip_software_pending`，但不能移除。

## 3. ISA 能力应使用不可变 profile

`sv39_supported` 的问题不是布尔值太简单，而是它把“硬件能力”放进了“客户机状态”。
建议增加一个只读、不可序列化指针所指向的静态 profile：

```c
typedef enum {
  RV_VM_BARE = 0,
  RV_VM_SV39 = 8,
  RV_VM_SV48 = 9,
  RV_VM_SV57 = 10,
  RV_VM_SV64 = 11, /* 当前规范保留，不启用 */
} RiscvVmMode;

typedef struct {
  RiscvVmMode mode;
  uint8_t va_bits;       /* 39 / 48 / 57 */
  uint8_t levels;        /* 3 / 4 / 5 */
  uint8_t pte_bytes;     /* RV64 为 8 */
  uint8_t page_shift;    /* 当前为 12 */
} RiscvVmModeInfo;

typedef struct {
  uint8_t xlen;          /* 当前固定 64 */
  uint8_t pa_bits;       /* 当前固定 56 */
  word_t misa_fixed;     /* 编译能力对应的固定扩展集合 */
  uint8_t pmp_entries;
  uint8_t pmp_granularity_log2;
  const RiscvVmModeInfo *vm_modes;
  size_t vm_mode_count;
  bool svnapot;
  bool svpbmt;
} RiscvIsaProfile;
```

`misa_fixed` 是 profile 的硬件能力；`cpu.misa` 仍是客户机可以观察的 CSR，其中
当前实现允许 C 位按既有语义动态关闭。profile 不能直接替代 `misa`，它是 CSR WARL
规则和 reset 值的输入。

第一阶段只提供一个实际 profile：

```text
RV64GC + Zicsr + Zifencei + Zicntr + M/S/U
Bare + Sv39
16 PMP entries, 4-byte granularity
Svnapot = false, Svpbmt = false
```

不应因为结构中能列出 `RV_VM_SV48`/`RV_VM_SV57` 就宣称已经支持；只有 walker、
`satp` WARL、异常、PMP、A/D、Linux/Spike 回归都完成后，才把模式放入实际 profile。

### Sv39、Sv48、Sv57 和用户所说的 Sv56

RV64 `satp.MODE` 的标准分页模式是 Bare、Sv39、Sv48 和 Sv57；Sv64 的编码目前保留。
不存在标准的 Sv56 模式，因此不能按“Sv39、Sv48、Sv56”设计枚举。正式实现时应使用
Sv57；如果未来出现自定义 Sv56，只能放在明确的 custom profile 中，不能复用标准
`satp.MODE` 编码或冒充标准能力。

官方特权规范的 `satp` 表列出 MODE=8 为 Sv39、9 为 Sv48、10 为 Sv57、11 为保留的
Sv64：[RISC-V Supervisor-level ISA](https://docs.riscv.org/reference/isa/priv/supervisor.html)。

### 通用 page walker 的参数

Sv39/48/57 的遍历差异主要是级数和虚拟地址宽度，可以由 descriptor 驱动：

```text
mode  levels  VA bits  VPN 数量
Sv39     3       39       3
Sv48     4       48       4
Sv57     5       57       5
```

walker 应按 profile 查找 descriptor，而不是写：

```c
if (!sv39_supported || mode != 8) host_error();
for (int level = 2; level >= 0; level--) ...
```

目标接口应类似：

```c
const RiscvVmModeInfo *riscv_isa_vm_mode(RiscvVmMode mode);
bool riscv_mmu_translate(MemAccess *access, const HartContext *hart);
```

walker 仍需独立处理：

- VA canonical/sign-extension 检查；
- `satp` PPN、物理地址位宽和 PTE 保留位；
- 不同 level 的 leaf 大页对齐；
- PTE A/D 隐式更新；
- PMP 对页表访问的权限；
- `Svnapot` 的 N 位和连续页编码；
- `Svpbmt` 的 PBMT 属性和最终 RAM/MMIO 访问策略。

Svnapot、Svpbmt 是页表语义扩展，不应被塞进 `RiscvVmMode` 枚举；它们应作为
`RiscvIsaProfile` 的能力位或单独的 translation feature 集合。

## 4. `CPU_state` 的目标形态

第一步不改变外部 `cpu` 符号和现有测试，先把语义分组：

```c
typedef struct {
  reg_t gpr[32];
  word_t fpr[32];
  vaddr_t pc;

  /* RVISS_CSRS_OWNED 生成的 CSR 后端存储 */
  RVISS_CSR_STORAGE(...)

  uint8_t priv;
  word_t mip_software_pending;

  struct {
    uint8_t cfg[16];
    word_t addr[16];
  } pmp;

  struct {
    paddr_t addr;
    uint8_t length;
    bool valid;
  } reservation;
} RiscvArchState;

typedef RiscvArchState CPU_state; /* 过渡别名，避免一次改变所有调用方 */
```

这里没有 `sv39_supported` 或 `pmp_count`。它们通过当前 hart 的 immutable profile
查询：

```c
const RiscvIsaProfile *riscv_isa_profile(const RiscvHart *hart);
bool riscv_isa_vm_supported(const RiscvHart *hart, RiscvVmMode mode);
unsigned riscv_isa_pmp_entries(const RiscvHart *hart);
```

### 为什么不把 profile 直接嵌入 `CPU_state`

`CPU_state` 当前被 checkpoint 直接 `memcpy`，而 `CPU_state` 布局改变还会影响文件
header。profile 含有指针、表地址和编译期能力，直接嵌入会：

- 把宿主地址写进 checkpoint；
- 使相同客户机状态在不同进程地址空间不可恢复；
- 把 immutable machine capability 当成 guest mutable state；
- 多 hart 时重复保存同一 profile。

profile 应由 machine/hart 创建时绑定，通过 profile ID/hash 写入 checkpoint 头部，
恢复时与当前运行配置比较；`CPU_state` 只保存内容。

## 5. 多 hart 需要提前守住的边界

本轮不实现多 hart，但以下当前全局状态以后必须消除：

| 当前全局/静态状态 | 多 hart 问题 | 未来归属 |
| --- | --- | --- |
| `CPU_state cpu` | 所有 hart 共享 PC、CSR、寄存器 | `RiscvHart.arch` |
| `hw_pending/hw_mask` in `intr.c` | 一个 hart 的设备线会污染另一个 hart | 每 hart 的 IRQ 输入状态 |
| `cpu_irq_input()` 静态输入数组 | 无法区分目标 hart | `cpu_irq_input(hart, bit)` |
| `reservation_*` | LR/SC reservation 不能跨 hart 共享 | 每 hart；共享内存写入时按规范清除/失效 |
| `pmpcfg/pmpaddr` | PMP 是 hart-local | 每 hart 的 `ArchState.pmp` |
| `satp`, `mstatus`, `mie/mip` | 地址空间和中断权限都是 hart-local | 每 hart CSR |
| `cpu.mhartid` | 当前只在 reset 写 0 | 每 hart固定 CSR，由容器初始化 |
| `clint` | `mtime` 全局，`mtimecmp/msip` 按 hart | 设备实例共享时间，比较寄存器按 hart |
| `plic` | context/claim/enable 按 hart/privilege | PLIC 设备共享，context 状态按 hart |
| `device_tick` 全局注册表 | 设备节拍与 hart 调度混在一起 | machine/device scheduler |
| `sim.event_seq` | 当前是单一执行序列 | 保留全局事件号，附带 hartid；每 hart 可另有 local sequence |

QEMU 的可借鉴点是“Machine 管设备，CPU/Hart 对象管 CPU 状态，设备通过 IRQ 线连接
到指定 CPU”，不是把 QEMU 的复杂对象层整体复制进 RVISS。RVISS 当前仍可以保持 C
结构和单线程解释器，只需把 owner 关系固定下来。

建议未来的最小容器：

```c
typedef struct {
  uint64_t hartid;
  RiscvArchState arch;
  RiscvHartRuntime runtime;
  const RiscvIsaProfile *profile;
  HartIrqState irq;
} RiscvHart;

typedef struct {
  RiscvHart *harts;
  size_t nr_harts;
  MachineDeviceState devices;
} RiscvMachine;
```

单 hart 过渡期可以让 `RiscvMachine.harts[0].arch` 仍通过兼容访问器映射到全局
`cpu`，但新代码不能继续读取全局 `cpu` 来决定能力或中断归属。

### 不要提前做的多 hart 语义

- 不要因为有 `nr_harts` 就自动让 Linux `SMP=y`；调度、IPI、CLINT、PLIC、原子性和
  内存交错尚未验证。
- 不要用宿主线程并行执行 hart；这会破坏可重放、trace 顺序和 difftest。
- 不要把 LR/SC reservation 做成共享单例；它是 hart-local，但共享内存写入可能使
  其他 hart 的 reservation 失效，这需要明确的内存事件接口。
- 不要把 `mcycle/minstret` 合并成全局计数；每 hart 的客户机 CSR 和 RVISS 全局事件
  计数必须分开。

## 6. 分阶段实现契约

### P0：设计文档（本任务）

- 固定状态/能力/runtime 的分类；
- 固定 Sv39/Sv48/Sv57 descriptor 方向；
- 记录 Sv56 不是标准 satp 模式；
- 记录多 hart owner 和 checkpoint 约束。

### P1：能力 profile（下一代码切片）

- 新增 `RiscvIsaProfile`、`RiscvVmModeInfo` 和只读查询 API；
- 当前实际 profile 只包含 Bare/Sv39；
- `riscv_misa_supported()` 从 profile 的固定能力生成；
- `sv39_supported` 由 profile 查询替代，保留测试 fixture 的显式配置入口；
- 运行 `make check`、Sv39 测试和 Linux/Spike 回归。

### P2：架构状态分组

- `CPU_state` 过渡别名到 `RiscvArchState`；
- PMP 数组分组到 `ArchState.pmp`，entry 数量从 profile 查询；
- `mip_software` 改名为 `mip_software_pending`，更新 CSR/IRQ 访问；
- checkpoint 使用明确的版本化序列化字段，不继续依赖裸 `sizeof(CPU_state)`；
- difftest 使用访问器，不暴露宿主布局。

### P3：通用分页 walker

- 先把 Sv39 改成 descriptor 驱动，保持现有行为和故障原因；
- 独立加入 Sv48，再加入 Sv57，每种模式有 page-walk、canonical VA、PMP、A/D 和
  `satp` WARL 测试；
- Kconfig 只在语义和独立参考验证完成后提供 `RV_SV48`/`RV_SV57`；
- PBMT/NAPOT 作为独立 feature，不借分页模式开关伪装支持。

### P4：hart 容器（后续任务）

- 把 `cpu`、IRQ 输入、reservation 和 CSR 状态放进 `RiscvHart`；
- Machine 按 hart 实例化 CLINT/PLIC context 和 IRQ 线；
- 引擎仍单线程，先做确定性 hart 轮转和单 hart 回归；
- 再决定 Linux SMP、IPI 和多 hart difftest 的验收范围。

## 7. 验收标准和禁止事项

代码切片必须满足：

1. capability 不由客户机 CSR 写入改变；
2. unsupported `satp.MODE` 整体写入无效，不产生半更新状态；
3. profile 不含宿主指针的可恢复快照；
4. checkpoint 恢复包含每 hart 的架构状态，但通过 profile ID/hash 验证硬件能力一致；
5. 所有 fault、A/D 更新和 MMIO 访问仍走现有结构化访问路径；
6. 单 hart Linux、Spike difftest 和已有 unit 回归保持通过。

禁止：

- 只新增 `bool sv48_supported` 而保留三套复制的 walker；
- 用 `Sv56` 作为标准 RV64 `satp` 模式；
- 把 `pmp_count`、`sv39_supported`、profile 指针写入客户机 checkpoint 状态；
- 通过增加 Kconfig 选项宣称没有实现的 MMU/PBMT/NAPOT 能力；
- 为未来多 hart 提前引入宿主线程或周期模型。

## 当前代码状态

本设计文档先于代码重构落地。当前源码仍保留 `CPU_state`、`sv39_supported`、
`pmp_count` 和单 hart 全局中断输入；这些是 P1/P2 的待办项，不在本文中宣称已经
完成。后续每次修改必须先重新阅读本文件，并在对应阶段补充独立验证和任务状态。
