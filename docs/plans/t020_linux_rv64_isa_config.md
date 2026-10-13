# T020：Linux RV64 ISA 配置与 RVISS 硬件能力映射

状态：active（2026-10-13；Linux/RVA23 清单、Zifencei/Zicntr 和两种非对齐访问
模式已经完成并验证；RVA23、PBMT、Svnapot、Sstc 等仍按本契约继续实现）。

## 目标

Linux 的 RISC-V Kconfig 描述的是“内核要编译、优化和使用哪些能力”，而 RVISS
描述的是“模拟 CPU 实际提供哪些能力”。两者必须相容，但不是同一份配置：Linux
可以用 alternatives、启动探测和 SBI/DT 信息在一份内核中适配多种硬件；RVISS
必须在启动时给出准确、可观察且与指令/CSR/MMU 语义一致的 ISA 身份。

本任务只考虑 RV64、单 hart、M/S/U、当前固定 Linux 6.11.4 工件和 Sv39。目标是：

1. 固化 Linux 6.11.4 的 RV64 配置选项、运行时 ISA 探测入口和非对齐访问策略。
2. 对照 RVA23U64/RVA23S64 列出必选扩展，明确当前 RVISS 的实现、缺口和先后顺序。
3. 将已经有真实语义和测试证据的能力暴露为 RVISS Kconfig；未实现能力不得只加
   一个看似可用的开关。
4. 为 PBMT、Svnapot、非对齐访问、Z* 扩展和未来 RVA23 支持定义可验证的任务边界。

## 范围和非目标

范围是配置契约、ISA 身份、扩展能力映射和逐项实现。每个代码切片必须有实际
单元/差分/启动验证和独立提交。

本任务不做以下工作：

- 不加入 RV32、Sv48/Sv57、多 hart、H、AIA 或完整 V 实现。
- 不把 Linux Kconfig 中的选项机械复制为 RVISS 开关。没有指令、CSR、MMU、异常
  和运行时探测语义的选项只能记录为 blocked。
- 不把 RVA23 当作一个可以绕过各扩展验证的总开关。只有当其必需集合全部实现并
  与 Linux/Spike 对拍后，才允许提供 `RV_RVA23S64` 配置预设。
- 不修改固定 Linux 6.11.4 输入、OpenSBI 工件、Spike 参考实现或既有验收期望。

## 术语和三层配置模型

### Linux 编译期配置

Linux `arch/riscv/Kconfig` 的 `ARCH_RV64I`、`MMU`、`FPU`、`RISCV_ISA_C` 等选项
决定编译器 `-march/-mabi`、内核代码路径和优化。`RISCV_ISA_SVNAPOT`、
`RISCV_ISA_SVPBMT`、`RISCV_ISA_ZBA/ZBB/ZBC/ZICBOM/ZICBOZ/ZAWRS` 等选项的含义是
“内核可以在启动确认硬件存在后使用这些扩展”，不等于 CPU 已经实现扩展。

Linux 6.11.4 的 `arch/riscv/Makefile` 从 `ARCH_RV64I + FPU + RISCV_ISA_C` 产生
`rv64imafdc`（工具链需要时附加 `zicsr_zifencei`）；未选择高效非对齐访问时附加
`-mstrict-align`。因此 Linux 的 `CONFIG_RISCV_PROBE_UNALIGNED_ACCESS` 是内核
对硬件行为的探测/仿真策略，不是 RISC-V ISA 扩展名。

### 运行时硬件身份

设备树优先使用 `riscv,isa-base` 与 `riscv,isa-extensions`，旧平台才由
`CONFIG_RISCV_ISA_FALLBACK` 允许解析已废弃的 `riscv,isa`。Linux 还会通过启动
探测和 alternatives 选择代码路径，并通过 `/proc/cpuinfo`、`riscv_hwprobe`
等接口向用户空间报告已识别能力。RVISS 的 DTB/固件必须只声明真实实现的扩展；
“配置打开但 DTB 没有对应扩展”与“DTB 声明但 CPU 不实现”都属于配置错误。

### RVA23 profile

RVA23 是 64 位应用处理器 profile。`RVA23U64` 是用户级必需/可选扩展集合，
`RVA23S64` 在其上增加 Linux 所需的特权扩展。它不是 Linux Kconfig 开关，也不
代表所有 Linux 发行版默认启用每个扩展。RVISS 只有在每项语义都实现后才能声明
满足 profile。

## Linux 6.11.4 RV64 选项清单

以下清单来自固定源码 `/home/yang/study_dir/riscv_embedded/linux-6.11.4`，当前
工件配置为 `build/linux/artifacts/linux.config`，输入模板为
`configs/linux/rv64gc_min.config`。

| Linux 选项 | 作用 | 固定 RVISS/Linux 基线 |
| --- | --- | --- |
| `ARCH_RV64I` | 选择 RV64I 基础 ISA、lp64 ABI | `y`，仅 RV64 |
| `MMU` | 启用分页地址空间 | `y`，RVISS 使用 Bare/Sv39 |
| `FPU` | 内核浮点保存/使用路径 | `y`，对应 F/D |
| `RISCV_ISA_C` | 编译压缩指令 | `y`，对应 C |
| `SMP` | 多 hart 内核支持 | `n`，RVISS 当前单 hart |
| `NONPORTABLE` | 允许依赖固定硬件的内核 | `y`，固定 virt 板级 |
| `RISCV_ALTERNATIVE` | 启用运行时替换机制 | `y`，不等于某个 ISA 扩展 |
| `RISCV_ISA_FALLBACK` | 兼容解析旧 `riscv,isa` DT 属性 | `n`，固定 DT 使用显式扩展列表 |
| `RISCV_PROBE_UNALIGNED_ACCESS` | 启动探测非对齐速度，缺失时 Linux 仿真 | 固定工件为 `y`；RVISS Linux profile 选择严格 trap |
| `RISCV_EMULATED_UNALIGNED_ACCESS` | 总是允许 Linux 对缺失硬件做仿真 | 当前未选；需要 RVISS 明确策略 |
| `RISCV_SLOW_UNALIGNED_ACCESS` | 假设硬件支持但较慢的非对齐访问 | 当前未选 |
| `RISCV_EFFICIENT_UNALIGNED_ACCESS` | 假设硬件高效支持并启用优化 | 当前未选 |
| `RISCV_ISA_SVNAPOT` | Svnapot 超大自然对齐页 | `n`，RVISS 未实现 |
| `RISCV_ISA_SVPBMT` | PTE PBMT 内存类型 | `n`，RVISS 未实现 |
| `RISCV_ISA_V` | V 向量及用户态保存/恢复 | `n`，RVISS 未实现 |
| `RISCV_ISA_ZAWRS` | 等待存储的低功耗指令 | `n`，RVISS 未实现 |
| `RISCV_ISA_ZBA/ZBB/ZBC` | 位操作/无进位乘法优化 | `n`，RVISS 未实现 |
| `RISCV_ISA_ZICBOM` | cache block 管理（非一致 DMA） | `n`，无 cache/DMA 语义 |
| `RISCV_ISA_ZICBOZ` | `cbo.zero` | `n`，RVISS 未实现 |

Linux 的 `FPU=y` 对应 F/D 代码路径；Linux 的编译 `-march` 还隐含 I/M/A 基础
整数/原子能力。`Zicsr`、`Zifencei` 在新工具链中通常以显式单字母之外的标准
扩展出现；RVISS 必须在 ISA 身份中分别管理它们，不能从“能执行 CSR/fence.i”
推断所有 Z 扩展都存在。

## RVA23U64/RVA23S64 映射

下表只列对当前 RV64 Linux/CPU 设计有直接影响的集合；完整版本以 RISC-V Profiles
仓库的 `rva23-profile.adoc` 为规范依据。

### RVA23U64 用户级必需集合

| 扩展/能力 | 当前 RVISS | 后续动作 |
| --- | --- | --- |
| RV64I/M/A/F/D/C | 已实现并有 T005/T006/T013/T015 证据 | 保持 Kconfig 分项 |
| `Zicsr` | 已实现 | 保持 `RV_ZICSR` |
| `Zicntr` | 客户机 cycle/time/instret 已实现；硬件身份和 CSR 映射需整理 | 增加显式 `RV_ZICNTR` 契约并回归 |
| `Zihpm` | HPM CSR 硬连线零，未实现 | blocked，不得声明 |
| `Ziccif`/`Ziccrse`/`Ziccamoa`/`Zicclsm` | 当前未作为独立能力建模 | 先定义 cache/coherence/atomic misaligned 语义 |
| `Za64rs`/`Zic64b` | 当前未作为硬件 profile 元数据建模 | 增加 profile 检查，不先伪造指令 |
| `Zihintpause` | 未实现 `pause` 语义/编码 | blocked |
| `Zicbom`/`Zicbop`/`Zicboz` | 无 cache block 指令和 cache 状态 | blocked |
| `Zfhmin` | F/D 已实现，但 half precision 指令未实现 | blocked |
| `Zkt` | constant-time 属性没有运行时语义 | 作为安全属性另立验证，不默认声明 |
| RVA23 新增 `V`/`Zvfhmin`/`Zvbb`/`Zvkt` | 未实现 | blocked |
| `Zihintntl`/`Zicond`/`Zimop`/`Zcmop`/`Zcb`/`Zfa`/`Zawrs` | 未实现 | 按指令组拆分后加入 |
| `Supm` | 无指针掩码 CSR/陷阱/ABI | blocked |

RVA23U64 的可选集合（例如 Zvkng/Zvksg、Zabha、Zacas、Ziccamoc、Zvbc、Zfh、
Zbc、Zicfilp/Zicfiss 和 BF16/向量扩展）也不能因 Linux Kconfig 中有对应优化
选项而宣称实现。

### RVA23S64 特权必需集合

| 扩展/能力 | 当前 RVISS | 后续动作 |
| --- | --- | --- |
| `Zifencei` | `fence.i` 已有有序功能模型中的 no-op 执行路径 | 加 `RV_ZIFENCEI`，指令/身份/测试统一门控 |
| Sv39 | 已实现，T012/T015 已验证 | 保持 `RV_SV39` |
| `Svbare`/`Svade`/`Ssccptr`/`Sstvecd`/`Sstvala`/`Sscounterenw` | 部分固定行为已在 CSR/trap 中存在，但没有 profile 能力元数据 | 逐项审计 CSR WARL/异常后再标记 |
| `Svpbmt` | PTE 高位 PBMT 未解析，PMA/cacheability/ordering 未建模 | 实现 PTE 解码、访问类型和异常/设备顺序后加入 |
| `Svinval` | 无 `sinval.vma` 等指令 | blocked |
| `Svnapot` | 页表只支持 Sv39 4K/2M/1G，未解析 NAPOT PTE | blocked |
| `Sstc` | 当前 S-mode timer 通过 SBI/CLINT，不提供 `stimecmp` 等 CSR | 实现 CSR、中断比较和 SBI 交互后加入 |
| `Sscofpmf` | 无真实 overflow interrupt/HPM 过滤 | blocked |
| `Ssnpm`/`Ssu64xl` | 无对应特权状态/模式建模 | blocked |
| `Sha`（H 依赖） | H 明确不在当前范围 | blocked |

RVA23S64 可选的 Sv48/Sv57、Zkr、Svadu、Sdtrig、Ssstrict、Svvptc、Sspm 等也保留
为后续任务。当前 RVISS 的“有 CSR 宏”不算实现，必须以访问权限、WARL、陷阱和
独立测试为准。

## 非对齐访问契约

RISC-V 基础 ISA 允许实现对非对齐 load/store 提供硬件支持，也允许访问陷阱并由
软件处理。Linux 因此把它作为四选一的内核策略，并通过启动 probe/hwprobe 向用户态
报告速度与可用性。该选项与 `Zicclsm`（RVA23 要求的“可能很慢但必须支持的
misaligned scalar access”）不能混为一谈。

当前 RVISS 在翻译/映射前对 RAM 的非自然对齐 load/store/LR/SC/AMO 报告
misaligned exception；MMIO 还要求设备声明的访问宽度。这个行为必须先作为明确的
`RV_MISALIGNED_TRAP`/严格模式契约固定，再实现可选的跨字节拆分或软件仿真。实现
仿真时必须覆盖：页边界、PMP、MMIO 禁止拆分、LR/SC/AMO 原子性、故障时目的寄存器
不写入，以及 Linux UABI 对用户态的要求。没有这些测试前不能把 Linux 配置改为
`RISCV_EMULATED_UNALIGNED_ACCESS` 或 `RISCV_EFFICIENT_UNALIGNED_ACCESS`。

## PBMT 实现边界

Svpbmt 不是“增加几个 PTE 标志位”就完成。RVISS 需要：

1. 在 Sv39 PTE 遍历中解析 PBMT 编码并检查保留组合。
2. 将 PMA/cacheability/idempotency/ordering 属性传递到最终访问路径。
3. 区分 RAM、MMIO 和不可缓存/强序访问，定义属性冲突及异常优先级。
4. 对页表隐式读取、A/D 更新、PMP 和设备副作用建立独立测试。
5. 通过 Linux `CONFIG_RISCV_ISA_SVPBMT=y` 的启动和至少一个设备映射用例，再考虑
   在 RVISS Kconfig 提供 `RV_SVPBMT`。

在此之前，RVISS DTB 不得声明 `svpbmt`，Linux 固定模板继续保持 `n`。

## RVISS 配置设计

### 已有能力应分项表达

继续保留 `RV_M/RV_A/RV_F/RV_D/RV_C/RV_ZICSR/RV_SV39/RV_PMP`。下一代码切片优先
增加：

- `RV_ZIFENCEI`：门控 `fence.i` 指令、misa/ISA 元数据和非法指令路径；当前有序
  功能模型可以明确说明 no-op，不伪造 cache flush。
- `RV_ZICNTR`：把已验证的用户级 `cycle/time/instret` CSR 视图与 ISA 身份绑定；
  `mcycle/minstret` 和 `mcounteren` 仍是特权架构状态；区分客户机计数器和 RVISS
  事件/预算计数器。
- 明确的非对齐访问策略符号（严格 trap 先作为默认），并在帮助文本中说明它不
  等同 Linux `RISCV_*_UNALIGNED_ACCESS`。

配置预设 `RV64_LINUX` 只能选择这些已实现能力和固定设备；它暂时不能叫
`RV_RVA23`，因为 Zihpm/Zicclsm/Zicbom/Zicboz/Zfhmin/Svpbmt/Svnapot/Sstc 等仍缺失。

### 未实现能力的处理

对于 Zba、Zbb、Zbc、Zicbom、Zicboz、Zawrs、V、Svnapot、Svpbmt、Sstc、Supm、H 等，
在语义实现前不新增可选的“假开关”。必要时可以在 Kconfig 注释、配置检查或能力
报告中明确显示 `unsupported`，并让误配在启动/构建阶段失败。配置选项只有在以下
条件同时满足时才可变成可选：

- 指令/CSR/MMU/异常路径已实现；
- `misa`、DTB 和 Linux 可见 ISA 列表一致；
- 有边界单元测试和至少一个独立参考/真实 Linux 路径；
- 关闭选项时非法指令或 CSR 行为仍符合规范。

## 分阶段实现和验收

### 阶段 1：清单和身份（本任务首先完成）

- 提交本契约文档。
- 审计 `Kconfig`、misa、CSR、DTB ISA 字符串和 `src/target/riscv64` 指令定义。
- 增加 `RV_ZIFENCEI`、`RV_ZICNTR` 和严格非对齐策略的真实配置/测试（若调用方
  审计证明门控安全）。
- 验收 `gc_defconfig` 与 `linux_rv64gc_defconfig` 的配置差异，并保持现有 Linux
  smoke/Spike 矩阵通过。

### 阶段 2：非对齐访问

- 先固定严格 trap 的 Linux 兼容行为和 `Zicclsm` 需求说明。
- 实现可选仿真时覆盖 RAM、页边界、PMP、设备、原子和故障提交。
- 用 Linux `RISCV_PROBE_UNALIGNED_ACCESS`/`RISCV_EMULATED_UNALIGNED_ACCESS`
  两种配置分别启动验证；不以编译通过代替行为验证。

### 阶段 3：Z* 与 cache block

- 按指令组实现 Zba/Zbb/Zbc/Zicond/Zfa/Zimop/Zcmop/Zcb/Zawrs。
- 在 RVISS 仍为功能模型时，先定义 Zicbom/Zicbop/Zicboz 的内存/cache 语义，
  再接入 Linux alternatives；没有 cache 状态时不能只把指令当无条件 no-op。

### 阶段 4：Svpbmt/Svnapot/Sstc

- 各自独立任务和测试；不得共用一个“分页增强”开关掩盖语义差异。
- 完成 Linux 启动、设备访问和 Spike/规范边界验证后，更新 profile 映射。

### 阶段 5：RVA23S64 预设

- 只有必需用户/特权集合完成，才增加 `RV_RVA23S64`；配置生成时检查冲突并输出
  缺失能力。
- 运行 Linux 相关 Kconfig、启动 smoke、Spike 扩展矩阵和故意差异注入。
- 文档明确 RVA23 profile 认证范围和 RVISS 尚未支持的可选扩展。

## 验证命令和证据

每个阶段使用独立 `BUILD_DIR`，保留首个失败。基础命令为：

```sh
make CC=/usr/bin/gcc linux_rv64gc_defconfig
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t020-config check
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t020-gc check
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t020-linux check
python3 tools/linux/run_rviss.py --rviss build/t020-linux/rviss \
  --out build/linux --boots 1 --rounds 1 --timeout 180 \
  --max-instr 4000000000
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t020-gc check-diff DIFF_SUITE=a
```

配置、实际命令、首个失败、Linux 日志哈希和 Spike 统计写入本文件或对应
`docs/validation/` 记录；通过后创建独立 Git 提交。固定 Linux 工件不可被
`make clean` 删除。

## 参考资料

- 固定 Linux 6.11.4：`/home/yang/study_dir/riscv_embedded/linux-6.11.4/arch/riscv/Kconfig`、
  `arch/riscv/Makefile`、`arch/riscv/include/asm/hwcap.h`。
- 当前 Linux 主线清单：<https://github.com/torvalds/linux/blob/master/arch/riscv/Kconfig>。
- RISC-V RVA23 profile：<https://github.com/riscv/riscv-profiles/blob/main/src/rva23-profile.adoc>。
- Linux RISC-V 用户 ABI、`hwprobe`、非对齐和指针掩码说明：
  <https://cdn.kernel.org/doc/html/latest/arch/riscv/uabi.html>。
- RVISS 当前能力边界：`docs/rv64_profile.md`、`docs/linux_boot_baseline.md`、
  `docs/plans/t019_correctness_and_rv64_config.md`。

## 当前状态

文档阶段完成后，RVISS 仍只宣称 I/M/A/F/D/C、Zicsr、现有 Zicntr、Sv39、PMP 和
固定 M/S/U 单 hart 能力。Linux 的 Z*、PBMT、Svnapot、Sstc、V、Supm 和 RVA23
必需但未实现的部分保持 blocked；后续每个能力单独更新此状态和证据。

## 阶段 1a：Zifencei/Zicntr 配置切片（verified，2026-10-13）

已完成的代码行为：

- 新增 `RV_ZIFENCEI`，默认配置打开；`fence.i` 只在该能力存在时进入译码表，
  关闭时同一编码走非法指令异常。当前顺序功能模型没有译码缓存，所以合法的
  `fence.i` 退休时不需要额外宿主缓存操作。
- 新增 `RV_ZICNTR`，默认配置打开；关闭时用户级 `cycle/time/instret` CSR
  访问失败并产生非法指令，机器级 `mcycle/minstret` 仍是特权架构计数器，
  `mcounteren/scounteren` 在关闭用户视图时硬连线为零。内部计数器仍与引擎
  预算保持分离。这没有把 HPM CSR 宣称为 `Zihpm`。
- `RV64_LINUX`、`gc_defconfig` 和固定 Linux RV64GC defconfig 显式选择这两个已
  实现能力；未实现的 PBMT、Svnapot、Z* cache block、V、Sstc 等仍没有假开关。
- 解码和计数器单测分别覆盖能力开启/关闭；关闭测试通过临时修改 `.config` 后
  使用独立 `BUILD_DIR` 构建，完成后工作区配置恢复为 `gc_defconfig`/Linux profile
  验证配置，不把临时配置写入 defconfig。

实际验证：

```text
make CC=/usr/bin/gcc linux_rv64gc_defconfig
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t020-zic check
  19 个单元、25,844 checks，3 个 smoke，CLI/UI 14 checks，全部通过

make CC=/usr/bin/gcc gc_defconfig
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t020-gc check
  19 个单元、25,844 checks，3 个 smoke，CLI/UI 14 checks，全部通过

gc_defconfig + CONFIG_RV_ZICNTR=n
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t020-no-zicntr2 check
  20 个单元、25,897 checks，3 个 smoke，CLI/UI 14 checks，全部通过；
  用户级 cycle/time/instret 为非法指令，机器级计数器和控制 CSR 的特权语义保持

gc_defconfig + CONFIG_RV_ZIFENCEI=n
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t020-no-zifencei check
  19 个单元、24,662 checks，3 个 smoke，CLI/UI 14 checks，全部通过；
  test_decode 的关闭路径验证 fence.i 不再译码

make CC=/usr/bin/gcc linux_rv64gc_defconfig
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t020-linux2 check
python3 tools/linux/run_rviss.py --rviss build/t020-linux2/rviss --out build/linux \
  --boots 1 --rounds 1 --timeout 180 --max-instr 4000000000
  Linux PID1/BusyBox smoke 正常关机，203,623,674 retired，exit 0，
  log SHA256=bcc1a0333425846ed7f095bdf07e1f36c267ef9d2c9af5bc3a2e2a4c8dba259b

make CC=/usr/bin/gcc gc_defconfig
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t020-gc2 check-diff DIFF_SUITE=a
  Spike 对拍 665 cases / 5,156 events 通过，3 种故意差异全部捕获
```

## 阶段 1b：非对齐访问策略（verified，2026-10-13）

已完成两个真实的 RVISS 硬件模式：

- `RV_MISALIGNED_TRAP`：普通数据访问在翻译前产生 load/store-misaligned，
  LR/SC/AMO 也保持自然对齐要求；固定 `RV64_LINUX` 和 Linux defconfig 采用此模式，
  对应 Linux `RISCV_PROBE_UNALIGNED_ACCESS` 让内核陷阱路径负责仿真。
- `RV_MISALIGNED_NATIVE`：普通 RAM load/store 逐字节预检查、逐字节翻译和访问，
  因此跨页不会跳过第二页权限；所有预检查通过后才写 RAM。MMIO 不拆分，LR/SC/AMO
  仍然 misaligned fault。这个模式表达功能模型中的硬件支持，不承诺真实周期性能。

`tests/unit/test_misaligned.c`、`test_fp.c`、`test_step.c` 和 `test_sv39.c` 分别覆盖
两种模式、FP 访问、跨 RAM 尾部、Sv39 映射和原子边界；smoke 在 native 模式使用独立
的 `fault_native.S`，避免把“硬件成功”误判为异常测试失败。

实际验证：

```text
make CC=/usr/bin/gcc gc_defconfig
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t020-misaligned-trap3 check
  严格 trap：20 个单元、25,855 checks，3 个 smoke，CLI/UI 14 checks，全部通过

# 临时把 CONFIG_RV_MISALIGNED_NATIVE=y 写入 .config 并同步 Kconfig
make -j4 CC=/usr/bin/gcc BUILD_DIR=build/t020-misaligned-native check
  native RAM 拆分：20 个单元、25,855 checks，3 个 smoke（fault_native），
  CLI/UI 14 checks，全部通过
```

本阶段尚未实现 Linux `RISCV_SLOW_UNALIGNED_ACCESS` 的性能模型、向量非对齐访问或
MMIO 非对齐语义；PBMT/Svnapot/Sstc/RVA23 仍按上面的阶段计划执行。
