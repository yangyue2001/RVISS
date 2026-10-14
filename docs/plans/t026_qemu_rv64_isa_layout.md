# T026：QEMU RV64 配置对照与 RVISS ISA 元数据布局

状态：design proposed（2026-10-14）。本任务先固化设计和迁移边界，不改变解释器、
CSR、MMU 或 Linux 启动行为。后续每个代码切片都要独立验证并提交。

## 1. 目标和结论

QEMU 的 RISC-V 实现可以借鉴三点：

1. CPU 模型把 ISA 扩展、特权版本、分页模式、PMP 数量和实现参数集中成一个配置；
2. CSR 访问使用按地址直接索引的操作表，普通访问走统一检查，特殊语义由回调处理；
3. CPU 状态、机器设备和中断接线分属不同对象，CPU profile 描述固定能力，不能被客户机
   的 CSR 写操作改变。

RVISS 可以采用这些原则，但不能复制 QEMU 的对象层或引入新的多 ISA 抽象。当前工程
目标仍然是 RV64GC + M/S/U + Sv39、单 hart、C 解释器。建议建立一个窄的
`include/cpu/isa/` 公共元数据目录，存放 profile、扩展枚举和分页模式描述；实际
指令、CSR、PMP 和页表语义继续放在 `src/target/riscv64`。不建议创建
`src/isa/<isa>/`，也不建议现在建立 RV32/RV64 通用运行时框架。

## 2. QEMU 的 RV64 支持方式

以下内容以 QEMU 的 RISC-V system emulator 和 TCG 代码为实现参考。具体版本的默认
扩展可能变化，使用时应以固定的 QEMU 源码版本为准。

### 2.1 系统入口、机器和 CPU 模型

- `qemu-system-riscv64` 模拟整机；`-M virt` 选择常用的虚拟板级，机器代码负责内存
  图、UART、CLINT、PLIC、固件和设备树。
- `-cpu` 选择 CPU 模型。QEMU 提供基础模型、`max`、RVA22/RVA23 profile 模型以及
  若干厂商模型。CPU 模型不是一条 ISA 字符串，而是多个属性的组合。
- `-bios`、内核镜像和设备树由机器层装配。CPU 能力、设备树声明和固件使用的扩展必须
  一致，否则客户机可能在启动后执行非法指令或错误选择 Linux alternatives。

### 2.2 配置字段

QEMU 在 `target/riscv/cpu_cfg_fields.h.inc` 等文件中维护 CPU 配置字段，主要分为：

| 类别 | 示例 | 含义 |
| --- | --- | --- |
| 单字母扩展 | I/E、M、A、F、D、C、B、V、H | 对应 `misa` 或基础 profile 的指令集合 |
| 多字母扩展 | Zicsr、Zifencei、Zba、Zbb、Zbc、Zbs、Zicntr、Zihpm、Zicbom、Zicboz、Zfa、Zfh | 额外指令、计数器或内存语义能力 |
| 特权扩展 | Sstc、Svadu、Svnapot、Svpbmt、Svinval、Smepmp、Smstateen、AIA 相关扩展 | 特权 CSR、中断或页表语义 |
| 分页和内存保护 | `max_satp_mode`、`pmp`、`pmp_regions`、`pmp_granularity`、`mmu` | 可用分页模式、PMP 数量/粒度和 MMU 开关 |
| 架构身份 | `misa_mxl_max`、`mvendorid`、`marchid`、`mimpid`、特权规范版本 | CSR reset 值和可观察的硬件身份 |
| 实现参数 | `vlenb`、`elen`、cache block size、PMU mask、大小端 | V 扩展、性能计数器和实现限制 |

QEMU 会检查扩展依赖。例如 I/E 互斥，S 依赖 U，H 依赖 I/S，D 依赖 F；Z* 扩展和
特权扩展也有各自的最低规范版本和依赖。这个检查发生在 CPU 初始化阶段，而不是等
客户机执行到相关指令后才发现配置错误。

QEMU 用一张扩展描述数组保存多字母扩展的名称、最低特权规范版本、QOM 属性名和配置
字段偏移，并据此注册 CPU 属性。这个做法适合“扩展名称到配置字段”的管理；扩展真正
涉及的指令、CSR 和内存行为仍由各自的实现代码负责。

### 2.3 QEMU 的 CPU 状态边界

QEMU 的 `CPURISCVState` 由每个 CPU/hart 持有，包含 GPR/FPR、PC、特权级、`misa`、
`mstatus`、`mip/mie`、`satp`、陷阱 CSR、PMP、PMU、向量状态和实现所需的隐藏字段。
Machine 对象拥有共享 RAM、设备和设备时钟；设备通过 IRQ 线或中断控制器连接到指定
CPU。这个边界与 T023 的建议一致：

- 架构状态和 LR/SC reservation 按 hart 保存；
- `pmpcfg/pmpaddr` 是 CSR 的后端存储，但 PMP 数量和粒度属于不可变 profile；
- 软件 pending 位与设备 IRQ 线分开，`mip` 是两者合并后的可见值；
- 模拟器的退休数、事件序号和逻辑时间不放进客户机 CSR 状态。

## 3. QEMU 的 CSR 表和新增方式

### 3.1 按地址直接索引

QEMU 使用 `riscv_csr_operations csr_ops[CSR_TABLE_SIZE]`，表大小覆盖 12 位 CSR 地址。
每项包含名称、可访问性谓词、读取函数、写入函数、读改写操作函数以及最低特权规范
版本。CSR 指令先用地址直接取得表项，再统一执行检查：

1. 检查 Zicsr 和 CSR 编码的最低特权级；
2. 检查表项是否存在、当前 CPU/profile 是否允许访问；
3. 处理只读编码和写掩码；
4. 普通 CSR 形成 `(old & ~write_mask) | (new & write_mask)`；
5. 调用写回调执行 WARL、别名和跨 CSR 副作用；
6. 将旧值返回给 CSR 指令的 `rd`，异常路径不提交目的寄存器。

因此，QEMU 的常规路径避免了遍历全部 CSR；特殊 CSR 仍可用回调表达复杂规则。

### 3.2 QEMU 没有通用的 WARL/WLRL/WPRI 字段解释器

QEMU 的 `write_mstatus`、`write_satp`、`write_mtvec`、PMP 写回调分别实现合法化、
掩码、锁定位和副作用。它没有一个把所有 CSR 字段声明成 `WPRI/WARL/WLRL` 后自动
生成完整语义的通用引擎。这个取舍的优点是特殊语义清楚，缺点是字段规则容易散落。

RVISS 当前 T025 的方向可以保留：用字段规则描述普通位域，再由显式回调处理 `misa`、
`satp`、PMP、委派和视图 CSR 等跨字段语义。建议把“字段规则”和“CSR 地址访问表”
分开，避免一个宏同时承担权限、存储、WARL 和副作用。

### 3.3 增加一个 CSR 的两条路径

标准 CSR 在 QEMU 中通常需要：

1. 增加地址和位定义；
2. 如有后端状态，在 `CPURISCVState` 增加成员；
3. 写读回调或操作回调；
4. 在 `csr_ops[]` 对应地址登记；
5. 增加扩展属性、依赖检查、reset、迁移和测试。

厂商自定义 CSR 可以通过 `RISCVCSR` 描述（CSR 地址、插入谓词和操作表），由 CPU 模型
在初始化时注册。这样自定义 CSR 不必修改所有标准 CSR 的公共路径，但仍然经过统一的
权限和访问检查。

## 4. RVISS 的 `isa` 目录是否合理

合理，但目录的职责必须收窄。用户提出的“宏和 enum”应该表达**架构元数据**，不能把
所有指令实现、CSR 副作用和 MMU 代码都搬到一个新目录。

### 4.1 推荐布局

```text
include/
└── cpu/
    └── isa/
        ├── profile.h       /* RV64 profile、能力集合、profile 查询接口 */
        ├── extension.h     /* 扩展 enum/bitset、名称和依赖元数据类型 */
        ├── vm_mode.h       /* Bare/Sv39/Sv48/Sv57 描述和 satp.MODE 常量 */
        └── field_rule.h    /* RW/RO/WPRI/WARL/WLRL 的公共字段规则类型 */

src/target/riscv64/
├── cpu.c                   /* profile 默认值与 reset 绑定 */
├── csr/                    /* CSR 访问、副作用和扩展门控 */
├── mmu/                    /* 页表遍历、PMP、A/D、故障 */
└── instr/                  /* 指令编码和执行语义 */
```

`include/cpu/isa/` 符合本项目“跨模块公共头放在 `include/<模块>`”的约定。CPU、CSR、
译码、MMU 和测试都可以引用 profile 类型；具体 RV64 语义仍留在 target 内部。

如果未来确实需要声明式生成文件，可以在 `include/cpu/isa/` 放一个受控的 `.def` 文件
作为单一事实来源，但不应再增加 `src/isa/rv32`、`src/isa/rv64` 两套实现树。

### 4.2 不推荐的布局

```text
src/isa/<isa>/instr.c
src/isa/<isa>/csr.c
src/isa/<isa>/mmu.c
```

这会把 RV64 的语义从 `src/target/riscv64` 分散出去，制造第二套 target 层；同时会
迫使当前代码引入 RV32、多 ISA 或运行时插件抽象。现阶段与 AGENTS.md 和
`docs/architecture.md` 的边界冲突，不应采用。

### 4.3 enum、宏和 Kconfig 的分工

| 机制 | 用途 | RVISS 建议 |
| --- | --- | --- |
| `enum`/位集合 | 运行时 profile、扩展存在性、分页模式、特权级 | 首选；可读、可调试、可序列化 |
| X-macro/`.def` | 同一清单生成名称、表项、声明 | 只用于确实存在单一事实来源的列表 |
| 普通宏 | 位掩码、常量、兼容别名 | 保持小而明确；不要用宏隐藏控制流 |
| Kconfig | 编译期裁剪和默认配置 | 只打开已有语义和测试证据的能力 |
| 运行时 profile | 当前模拟 CPU 的不可变硬件能力 | 供 CSR/MMU/译码查询，不能由客户机修改 |

“有一个宏”不等于“CPU 支持该扩展”。扩展只有在指令/CSR/MMU/异常、`misa` 或设备树
声明、关闭扩展时的非法指令行为和独立测试都一致时，才可以出现在实际 profile。

## 5. 建议的数据模型

下面是设计方向，不是本轮要直接提交的 ABI：

```c
typedef enum {
  RV_EXT_I = 0,
  RV_EXT_M,
  RV_EXT_A,
  RV_EXT_F,
  RV_EXT_D,
  RV_EXT_C,
  RV_EXT_ZICSR,
  RV_EXT_ZIFENCEI,
  RV_EXT_ZICNTR,
  /* 后续按需增加 Z*、S* 能力；不为未实现扩展伪造默认值 */
} RvIsaExt;

typedef enum {
  RV_VM_BARE = 0,
  RV_VM_SV39 = 8,
  RV_VM_SV48 = 9,
  RV_VM_SV57 = 10,
} RvVmMode;

typedef struct {
  RvVmMode mode;
  uint8_t levels;
  uint8_t va_bits;
  uint8_t page_shift;
} RvVmModeInfo;

typedef struct {
  uint8_t xlen;                 /* 当前固定为 64 */
  uint64_t ext_mask;            /* 固定硬件能力 */
  uint8_t priv_mask;            /* M/S/U */
  uint8_t pmp_entries;
  uint8_t pmp_granularity_log2;
  const RvVmModeInfo *vm_modes;
  size_t vm_mode_count;
  bool svnapot;
  bool svpbmt;
  bool misaligned_access;
} RvIsaProfile;
```

当前唯一实际 profile 应明确为：

```text
RV64GC + Zicsr + Zifencei + Zicntr
M/S/U，Bare + Sv39
PMP 16 entries，粒度按当前实现
Svnapot/Svpbmt/Sstc/V/H 暂未加入能力集合
```

`RV_VM_SV48` 和 `RV_VM_SV57` 可以先作为描述类型，为后续 walker 复用结构；在实现
页表遍历、`satp` WARL、PMP、异常、A/D 和 Linux/Spike 验证以前，不得把它们放进默认
profile。标准 RV64 分页模式是 Sv39、Sv48、Sv57；“Sv56”不是标准 `satp.MODE`，不能
直接作为标准模式加入枚举。

CSR 字段规则可以使用单独的：

```c
typedef enum {
  RV_CSR_RW,
  RV_CSR_RO,
  RV_CSR_WPRI,
  RV_CSR_WARL,
  RV_CSR_WLRL,
} RvCsrFieldRule;
```

它只描述字段写入约束；CSR 地址权限、扩展依赖、别名和跨 CSR 副作用仍由 CSR 访问层
决定。这样新增 profile 字段不会意外改变 PMP 或 `satp` 的特殊语义。

## 6. 配置和执行流程

```text
Kconfig / defconfig
        │  编译期选择已实现的 RV64 能力
        ▼
RvIsaProfile 默认实例
        │  初始化 CPU/hart，记录能力 hash
        ├── CSR 表：决定 CSR 是否存在、字段规则和 WARL 输入
        ├── decoder：决定指令是否可译码
        ├── MMU：决定 satp.MODE、页表级数和扩展页属性
        └── DTB/固件：只声明 profile 中真实存在的扩展
```

checkpoint 只保存客户机架构状态；profile 作为配置 ID/hash 写入头部，恢复时与当前
运行配置比较。不要把包含指针的 profile 直接 `memcpy` 到 checkpoint，也不要让客户机
通过 `misa` 或 `satp` 写入改变固定硬件能力。

## 7. 分阶段迁移和验收

### 阶段 1：本文档（本轮）

- 固化 QEMU 对照、`include/cpu/isa/` 目录边界和非目标；
- 不改变现有 CSR、译码、MMU 和 Linux 行为；
- 运行 `git diff --check`，提交文档。

### 阶段 2：只搬元数据类型

- 新增 `include/cpu/isa/profile.h`、`extension.h`、`vm_mode.h`；
- 先由当前 RV64GC/Sv39 配置构造一个 profile；
- 保留现有 `cpu` 符号和调用方，不做 `CPU_state` 大规模重排；
- `make ... check`、现有 Linux smoke 和 Spike 基线必须保持不变。

### 阶段 3：连接 CSR 和 MMU

- `misa`、`satp`、PMP 和分页 walker 查询 profile；
- 把 `sv39_supported`、PMP entry 数量等固定能力逐步移出可恢复 CPU 状态；
- profile ID/hash 加入 checkpoint 兼容检查；
- 为扩展关闭路径和 WARL 边界补充单元/差分测试。

### 阶段 4：逐项增加能力

- 每个 Z/S 扩展单独实现、配置和验证；
- Sv48/Sv57、Svnapot、Svpbmt、Sstc 等必须分别通过规范边界、Linux 路径和 Spike
  对拍后才加入 profile；
- 所有阶段使用独立构建目录并创建独立 Git 提交。

## 8. 非目标和风险

- 不实现 RV32 或通用多 ISA 运行时；
- 不把 Linux Kconfig 选项机械复制成 RVISS 开关；
- 不用“存在一个 enum/宏”冒充已经实现扩展；
- 不把 QEMU 的 QOM、TCG 或热插拔机制搬进 RVISS；
- 不在这一步重构 `CPU_state`、CSR 清单或页表 walker；
- QEMU 版本变化可能改变 `max` 或 profile 默认扩展，RVISS 应固定参考版本并在文档中
  记录版本，而不是依赖宿主机安装的 QEMU 行为。

## 9. 参考源码

- [QEMU RISC-V system emulation](https://github.com/qemu/qemu/blob/master/docs/system/target-riscv.rst)
- [QEMU RISC-V CPU models](https://github.com/qemu/qemu/blob/master/target/riscv/cpu.c)
- [QEMU CPU configuration fields](https://github.com/qemu/qemu/blob/master/target/riscv/cpu_cfg_fields.h.inc)
- [QEMU RISC-V CPU state](https://github.com/qemu/qemu/blob/master/target/riscv/cpu.h)
- [QEMU CSR operation types](https://github.com/qemu/qemu/blob/master/target/riscv/tcg/csr.h)
- [QEMU CSR access and operation table](https://github.com/qemu/qemu/blob/master/target/riscv/tcg/csr.c)
- [QEMU custom CSR registration](https://github.com/qemu/qemu/blob/master/target/riscv/tcg/tcg-cpu.c)
- [RVISS 目录与边界](../architecture.md)
- [RVISS CPU 状态和 profile 设计](t023_cpu_state_isa_profile.md)
- [RVISS CSR 字段规则](t025_csr_field_rules.md)
