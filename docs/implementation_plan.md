# RVISS 实现方案

> 目标：一个**只支持 RV64**、面向"能读懂、能调试、能对拍"的指令集模拟器。
> 目录约定见 [architecture.md](architecture.md)。

---

## 一、定位与范围

| 维度 | 选择 | 理由 |
|---|---|---|
| ISA | 只做 RV64（含 M/A/F/D/C + 常用 Z 扩展；H/V 可选） | 用户明确要求；不做多 ISA 抽象 |
| 引擎 | 只有解释器（threaded-code 等优化后置） | 可读性 > 性能；先要正确 |
| 运行模式 | 系统模式（M/S/U 三特权级）；用户模式（Linux syscall 转发）可选 | 系统模式是主要用途 |
| 确定性 | 默认开启（时间由指令数驱动） | checkpoint/difftest 的前提 |
| 精度 | F/D 用 softfloat（先）；V 用 RVV 1.0 语义 | 位精确优先 |

---

## 二、ISA 扩展支持矩阵（参考 Linux `arch/riscv` 的现状）

Linux 的 RISC-V 移植把扩展分成"内核必需"和"可选加速/特性"两类。
下表按这个思路排出 RVISS 的目标：

### 阶段一：Linux 启动所需（必做）

| 扩展 | Linux 里 | RVISS 落点 | 状态 |
|---|---|---|---|
| RV64I | 必需 | `instr/i/` | ✅ 50 条（含 `*W` 字运算与 `fence`/`fence.i`） |
| Zicsr / Zifencei | 必需 | `instr/system/`、`csr/` | ✅（fence.i 待补） |
| M（乘除） | 必需（内核用它做除法） | `instr/m/` | ⏳ |
| A（原子） | 必需（自旋锁） | `instr/a/` | ⏳ |
| F / D | 必需（LP64D 用户态 ABI） | `instr/f, d/` + `rtl_fp.c` | ⏳ |
| C（压缩） | 强烈推荐（发行版 ABI 用） | `instr/c/` | ⏳（译码已留 RVC 分支） |
| Sv39 / Sv48 | 必需（MMU） | `mmu/mmu.c` | ⏳ |
| PMP | 必需 | `mmu/pmp.c` | ⏳ |
| S 模式 + 陷阱委派 | 必需 | `trap/trap.c`（medeleg/mideleg） | ⏳ |
| Zicntr / Zihpm | 必需/常用 | `csr/` | ⏳ |

### 阶段二：Linux 会用到的可选特性

| 扩展 | Linux 里 | RVISS 落点 |
|---|---|---|
| Zba / Zbb / Zbs（B） | 可选（编译优化） | `instr/b/` |
| Zicbom / Zicboz | 可选（DMA cache 维护） | `instr/z/` |
| Zihintpause | 可选 | `instr/z/` |
| Zicond | 可选 | `instr/z/` |
| Zawrs | 可选（WRS） | `instr/z/` |
| Zacas / Zabha | 可选（新原子指令） | `instr/a/` |
| Svnapot / Svpbmt / Svvptc | 可选（页表优化） | `mmu/` |
| Sstc | 可选（S 模式定时器） | `csr/` + `clint.c` |
| Sscofpmf（overflow 中断） | 可选 | `csr/` + `trap/` |
| Smstateen / Smepmp | 可选（安全） | `csr/` + `mmu/pmp.c` |
| AIA（IMSIC） | 可选（新中断控制器） | `device/` + `trap/` |
| Zfh / Zfhmin、Zfa | 可选（半精度/更多 FP） | `instr/f/` |
| V（RVV 1.0） | 可选 | `instr/v/` + `cpu.h` 加 vreg |

### 阶段三：可做但非必需

| 扩展 | 说明 |
|---|---|
| H（Hypervisor） | 两阶段翻译、hfence/HLV/HSV、guest page fault；工作量≈半个 MMU |
| Sdtrig | 调试触发器（mcontrol/icount） |
| Smrnmi | 可恢复 NMI |
| Zicfilp / Zicfiss | 控制流完整性 |

**取舍原则**：阶段一做完就是一个"能跑 Linux"的模拟器；阶段二按需加；
阶段三不追求（结构上留好扩展点即可）。

---

## 三、模块实现方案

### 3.1 译码（`target/riscv64/decode.c`）

- 表结构：`InstrEntry{ name, pattern, key, mask, shift, fmt, exec }`，一条指令一行。
- `pattern_decode()` 把 `"0000000 ????? ... 0110011"` 编译成 `(key, mask, shift)`；
  运行时 `(instr >> shift) & mask == key` 即命中。
- 操作数格式（fmt）决定如何填 `Dest/Operand`：
  `R / I / X(移位立即数) / Z(csr 立即数) / S(st 偏移) / B(分支偏移) / U / J / N`。
- 现在**线性扫描**（53 条无压力）。表变大后（RVV 全量 ~600 条）再换两级表或 hash，
  接口不变。
- RVC：`(instr & 3) != 3` 时进入 2 字节分支；扩展指令展开成等价 32 位语义再走原表
  （M5 实现，`Decode.ilen` 已经预留）。

### 3.2 执行（`engine/interpreter/`）

- 主循环：`decode → cpu.pc = snpc → exec(&s) → 中断轮询 → clint_tick`。
- 跳转类指令直接改写 `cpu.pc`；非跳转保持 `snpc`。
- 当前每条指令轮询一次中断。批量化（每 N 条一次）是后续优化，语义不变。
- 后续优化点（按收益排序）：指令表 hash/分级 → RTL 内联到单编译单元（threaded code）
  → 基本块缓存（tcache）。

### 3.3 CSR（`target/riscv64/csr/csr_access.c`）

- 表驱动：`{ addr, field, view_mask, writable, min_priv, name }`。
- `view_mask` 表达"视图"关系：`sstatus/sie/sip` 是 `mstatus/mie/mip` 的掩码视图，
  不复制状态，天然保持一致。
- 访问检查：地址存在性 + 特权级；失败上报 `EX_II`（与硬件一致）。
- 写后副作用集中在一处（目前只有 `satp` → 刷 TLB），后续 `mstatus`/`mie` 等在此扩展。
- 计数器（`mcycle/minstret`）下一步接 `cpu.instret`，为确定性时钟服务。

### 3.4 陷阱与中断（`target/riscv64/trap/`）

- 统一入口 `riscv_raise_trap(cause, epc)`：写 `mcause/mepc/mtval`、
  改 `mstatus.MPP/MPIE/MIE`、选 `mtvec`（direct/vectored）→ 返回入口地址。
- M3 加委派：按 `medeleg/mideleg` 选 M 或 S，S 侧再按 `hedeleg/hideleg`（若做 H）。
  `trap.c` 结构已经按"选目标特权级"预留。
- 中断：设备线 → `mip` → `mie & mstatus.MIE` → 按优先级（MEI > MSI > MTI > SEI...）。
- 异常来源要补全：取指/访存异常的 `tval` 语义（地址 / 指令编码）。

### 3.5 MMU（`target/riscv64/mmu/`）

Sv39/Sv48 要点（M3 的全部工作量）：

1. **页表遍历**：按 `satp.PPN` 逐级取 PTE（每级 9 位 VPN），
   PTE 非法/非叶时按规则判定（V=0 或 R=W=X=0 且非最后一级 → 下一级；否则 fault）。
2. **权限**：R/W/X/U 位、`SUM`（S 访问 U 页）、`MXR`（可执行页可读）。
3. **A/D 位**：硬件更新（`Svade` 则触发 page fault 由软件置位）。
4. **fault 归类**：取指→IPF、读写→LPF/SPF，`tval` 填缺失地址。
5. **TLB**：直接映射表（vpn+asid → pte 权限快照），失效点：
   `sfence.vma` / 写 `satp` / 写页表（软件责任）。
6. **两阶段（H 扩展，阶段三）**：VS-stage + G-stage，`hgatp` 与 guest page fault。

### 3.6 内存与 MMIO（`memory/`）

- `paddr_read/write`：RAM 直接小端访问；否则查设备总线；再否则打日志按 0/丢弃。
- `vaddr_*`：翻译（`isa_mmu_*` 钩子）→ 物理访问。TLB 加在这里。
- 待补：非对齐访问策略（RISC-V 允许硬件处理或触发异常，需明确）、
  MMIO 的访问宽度校验、跨页访问。

### 3.7 设备与机器（`device/`、`machine/`）

- `MmioRegion` + 中断线，是设备与内核之间的全部契约。
- CLINT 的 mtime 由指令数驱动（`CYCLES_PER_MTIME_TICK`）→ 确定性。
- PLIC 需按 Linux 的用法补全：优先级仲裁、多上下文（每 hart 的 M/S 各一个 context）、
  中断 claim/complete 的真实语义。
- 新设备：写一个 `MmioRegion` + 在 `device.h` 声明 init + 在 `machine.c` 挂上。

### 3.8 difftest / checkpoint（`difftest/`、`checkpoint/`）

- **difftest**：`include/rviss.h` 里的 `difftest_*` 就是 ABI（NEMU 兼容）。
  骨架已实现 regcpy/csrcpy/exec/raise_intr；`store_commit`、`guided_exec`、
  非默认 RAM 大小留待 M7。
- **checkpoint**：格式 = `[头部(含校验和) | CPU_state | RAM]`。
  下一步：**稀疏存储**（只存非零页 4KiB）+ 设备状态（CLINT 的 mtime/mtimecmp、
  PLIC 寄存器）+ 可选 zstd 压缩。恢复时校验 RAM 基址/大小一致。

### 3.9 Trace 与调试（横切）

- 日志两维：**分类**（general/instr/mem/mmu/trap/dev/diff）+ **等级**
  （ERROR..TRACE），运行期开关，不用重编译。
- `-t` 打开指令级 trace（pc + 原始编码 + 助记符 + 操作数）。
- 交互调试器：`c` / `si [n]` / `info r` / `x ADDR` / `q`。
- 待补：最近 N 条指令环形缓冲（报错现场回放）、表达式求值、watchpoint、断点。

---

## 四、里程碑（含当前进度）

| 里程碑 | 内容 | 验收标准 | 状态 |
|---|---|---|---|
| **M0 骨架** | 目录/构建/日志/内存/测试框架 | `make check` 通过 | ✅ 已完成 |
| **M1 最小解释器** | 译码表 + RVI 子集 + 系统指令 + `nemu_trap` | 裸机程序 HIT GOOD TRAP | ✅ 已完成 |
| **M2 异常与 CSR** | 统一陷阱入口、mstatus/mtvec/mepc、ecall/ebreak/mret/sret、CSR 表 | 异常能跳到 mtvec 并返回 | ✅ 基本完成（委派待 M3） |
| **M3 虚拟内存** | Sv39/Sv48 + TLB + PMP + 陷阱委派 | 能跑开启分页的 S 模式程序 | ⏳ 下一步 |
| **M4 设备与中断** | CLINT/UART 完善、PLIC 完整、中断注入 | 定时中断驱动裸机程序 | 🚧 骨架已有 |
| **M5 扩展指令** | M → A → C → F/D(softfloat) → B → V | riscv-tests / arch-test 相关组通过 | ⏳ |
| **M6 Trace 工具** | 指令环、统计、性能计数、UI 增强 | 能定位一个自己写错的指令 | 🚧 基础已有 |
| **M7 对拍与快照** | difftest 全 ABI（store_commit/guided）、稀疏 checkpoint、压缩 | 与 Spike 对拍通过 | 🚧 骨架已有 |
| **M8 打磨** | 用户模式 syscall、gzip 镜像、文档、性能 | 能跑 Linux 用户态程序 | ⏳ |

---

## 五、测试策略

| 层次 | 内容 | 现状 |
|---|---|---|
| 单元测试 | 位操作、译码（真实走取指路径）、后续：CSR/MMU 页表遍历 | ✅ test_bits / test_decode |
| 冒烟测试 | 汇编程序 → 装载 → 执行 → 校验 UART 输出与退出码 | ✅ tests/smoke |
| ISA 一致性 | `riscv-tests`（rv64ui/rv64um/...）、`riscv-arch-test` | ⏳ M5 起接入 |
| 差分测试 | 与 Spike（difftest）逐指令对拍 | ⏳ M7 |
| 长稳 | Linux 启动 → shell → 跑 busybox | ⏳ M7/M8 |

原则：**每加一条指令/一个 CSR，先写一条单测再写实现**。
译码单测不需要构造整个机器（`mem_init` + `cpu_reset` 即可）。

---

## 六、关键决策记录

| # | 决策 | 备选 | 结论与理由 |
|---|---|---|---|
| 1 | 构建系统 **Makefile + Kconfig** | CMake | 与 NEMU/Linux 同构：defconfig 预设、配置即代码开关、fixdep 精确增量；已从 NEMU 移植 kconfig/fixdep 工具 |
| 2 | 译码用扁平表 | 两级表 / 生成器 | 可读性优先；表大再优化，接口不变 |
| 3 | 指令语义用 RTL 原语 | 直接写 C | 保留引擎可替换性（NEMU 最值得保留的设计） |
| 4 | 浮点先 softfloat | host FPU | 位精确、便于 difftest；接口隔离，可换 |
| 5 | 确定性时钟（指令数驱动） | 宿主时钟 | checkpoint / difftest 的必要条件 |
| 6 | 机器参数运行期配置 | 编译期 Kconfig | 一套二进制跑多板子 |
| 7 | **模型定位：功能模型，不做时序** | 加流水/周期模型 | 只对"实现的正确性"负责；引入时序会让输出从"确定的状态"变成"性能估计"，checkpoint/difftest 都要跟着背非体系结构状态。微架构只做纯统计（准确率/命中率），见 [trace_and_uarch_plan.md](trace_and_uarch_plan.md) |
| 8 | RTL 用外部函数而非 static inline | NEMU 的单编译单元内联 | 模块边界清晰；性能优化见 3.2 |

---

## 七、风险与开放问题

1. **性能**：纯解释器 + 跨 TU 函数调用较慢（当前 ~5M instr/s 量级，够用但不适合跑大数据集）。
   若需要，按 3.2 的顺序优化，不改语义与接口。
2. **A/D 位与 sfence 的精确性**：Linux 对 `satp` 切换与 TLB 失效很敏感，
   M3 要严格按 spec 写（含 `ASID` 语义）。
3. **非对齐访问语义**：需明确是"硬件处理"还是"触发异常"（RISC-V 两者都允许），
   并保持与 difftest 参考模型一致。
4. **PLIC/AIA 的复杂度**：PLIC 要支持多上下文；AIA（IMSIC）是另一套模型，
   建议先在 M4 只做 PLIC。
5. **V 扩展**：RVV 的语义细节极多（tail/mask agnostic、vstart、异常恢复），
   建议放最后，且先只做整数部分。
