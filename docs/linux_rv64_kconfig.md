# Linux RV64 配置选项速查

本文总结项目固定输入 Linux 6.11.4 的 `arch/riscv/Kconfig` 和
`arch/riscv/Makefile`。它回答的是“Linux 内核要按哪些能力编译和运行”，不是一
份 RV64 ISA 手册，也不是把所有 Linux Kconfig 选项都复制成 RVISS 开关。

Linux 的配置分为三层：

1. **编译目标**：决定内核是 RV32 还是 RV64、是否使用 MMU/FPU/C，以及编译器的
   `-march`/`-mabi`。
2. **运行时 ISA 使用**：内核启动后通过设备树、硬件探测和 alternatives 判断某个
   扩展是否真的可用，再决定是否使用优化路径。
3. **平台和内核策略**：SMP、SBI、设备树、非对齐访问和 DMA 等。这些选项不一定
   是 ISA 扩展。

## 当前 RVISS Linux 工件的实际配置

配置文件是 `build/linux/artifacts/linux.config`，对应固定的 RV64GC、Sv39、单
hart 平台：

| Linux 选项 | 当前值 | 含义 |
| --- | --- | --- |
| `CONFIG_64BIT` | `y` | 选择 64 位内核类型 |
| `CONFIG_ARCH_RV64I` | `y` | 选择 RV64I 基础 ISA，并使用 `lp64` ABI |
| `CONFIG_MMU` | `y` | 启用分页虚拟内存 |
| `CONFIG_PAGE_SIZE_4KB` | `y` | 基础页大小为 4 KiB |
| `CONFIG_PGTABLE_LEVELS` | `5` | 内核可编译五级页表代码；实际使用的 Sv39 由硬件/DTB 决定 |
| `CONFIG_FPU` | `y` | 内核允许 F/D 浮点路径 |
| `CONFIG_RISCV_ISA_C` | `y` | 编译器可以生成 C 压缩指令 |
| `CONFIG_SMP` | `n` | 当前 Linux 工件只启动一个 hart |
| `CONFIG_NONPORTABLE` | `y` | 允许固定平台相关的优化配置 |
| `CONFIG_RISCV_ALTERNATIVE` | `y` | 启动时根据硬件能力替换优化代码 |
| `CONFIG_RISCV_SBI` | `y` | S-mode 内核通过 SBI 使用 M-mode 固件服务 |
| `CONFIG_RISCV_SBI_V01` | `n` | 不启用旧版 SBI v0.1 兼容路径 |
| `CONFIG_RISCV_ISA_FALLBACK` | `n` | 不回退解析旧的 `riscv,isa` DT 属性 |
| `CONFIG_RISCV_PROBE_UNALIGNED_ACCESS` | `y` | 启动探测非对齐访问速度，缺少硬件支持时由内核仿真 |

这组配置由 `configs/linux_rv64gc_defconfig` 生成。`ARCH_RV64I` 并不只代表字母
I：Linux 的 RISC-V Makefile 会组合出基础编译 ISA：

```text
ARCH_RV64I       -> rv64ima
FPU=y            -> rv64imafd
RISCV_ISA_C=y    -> rv64imafdc
```

当工具链版本需要时，Linux 还会追加 `_zicsr_zifencei`；这两个扩展没有独立的
用户可见 Kconfig 选项，而是由工具链兼容逻辑和内核实际使用决定。

## 基础目标和地址翻译

| 选项 | 作用 | RVISS 对应关系 |
| --- | --- | --- |
| `ARCH_RV32I` / `ARCH_RV64I` | 选择 XLEN | RVISS 只支持 RV64，因此只保留 `ARCH_RV64I` 的语义 |
| `MMU` | 启用分页地址翻译 | 对应 Bare/Sv39；RVISS 当前实现 Sv39 |
| `RISCV_M_MODE` | 让 Linux 直接运行在 M-mode | 当前 Linux/OpenSBI 路径不使用 |
| `RISCV_SBI` | S-mode 通过 SBI 请求固件服务 | 对应 OpenSBI + M/S/U 运行环境 |
| `PAGE_SIZE_4KB` | 基础页大小 | 对应 Sv39 的 4 KiB 页 |
| `PGTABLE_LEVELS` | 编译页表代码的最大级数 | 不是“当前 CPU 一定支持 Sv57”；DTB 和运行时探测决定实际模式 |

Linux Kconfig 没有 `CONFIG_SV39`、`CONFIG_SV48` 这种简单的分页模式开关。实际
分页模式由平台硬件、SATP 和设备树的 `mmu-type` 共同决定。因此 RVISS 不应把
`PGTABLE_LEVELS=5` 解释成已经支持 Sv48/Sv57。

## ISA 和扩展相关选项

### 编译直接使用的能力

| 选项 | 含义 | 当前工件 |
| --- | --- | --- |
| `FPU` | 启用浮点保存、恢复和内核浮点代码 | `y`，需要 RVISS 的 F/D |
| `RISCV_ISA_C` | 允许编译器生成 C 指令 | `y`，需要 RVISS 的 C |
| `RISCV_ISA_V` | 启用向量扩展支持 | `n` |
| `RISCV_ISA_V_DEFAULT_ENABLE` | 默认允许用户态使用向量 | 只有 `RISCV_ISA_V=y` 时有效 |
| `RISCV_ISA_V_UCOPY_THRESHOLD` | 向量化用户拷贝的大小阈值 | 只有 V 开启时有效 |
| `RISCV_ISA_V_PREEMPTIVE` | 内核向量代码是否允许抢占 | 只有 V 开启时有效 |

Linux 的 `-march` 由 `rv64ima`、`FPU`、`RISCV_ISA_C` 和 `RISCV_ISA_V` 组合得到。
因此 Linux 没有单独的 `CONFIG_RISCV_ISA_M`、`CONFIG_RISCV_ISA_A`、
`CONFIG_RISCV_ISA_F` 或 `CONFIG_RISCV_ISA_D`；它们分别由基础目标和 `FPU` 间接
确定。

### 启动探测后使用的扩展

这些选项通常表示“内核可以在启动时探测到扩展后使用优化”，不表示所有运行 Linux
的 CPU 都必须实现该扩展：

| 选项 | 扩展用途 | 当前工件 |
| --- | --- | --- |
| `RISCV_ISA_SVNAPOT` | Svnapot，大于基础页的自然对齐连续页映射 | `n` |
| `RISCV_ISA_SVPBMT` | Svpbmt，PTE 页面内存类型 | `n` |
| `RISCV_ISA_ZAWRS` | 等待存储，优化忙等循环 | `n` |
| `RISCV_ISA_ZBA` | 地址生成相关位操作 | `n` |
| `RISCV_ISA_ZBB` | 基础位操作 | `n` |
| `RISCV_ISA_ZBC` | 无进位乘法，可优化 CRC | `n` |
| `RISCV_ISA_ZICBOM` | 非一致 DMA 的 cache block 管理 | `n` |
| `RISCV_ISA_ZICBOZ` | `cbo.zero`，优化清零 | `n` |

工具链检测项 `TOOLCHAIN_HAS_V`、`TOOLCHAIN_HAS_ZBB`、`TOOLCHAIN_HAS_ZBC` 和
`TOOLCHAIN_HAS_VECTOR_CRYPTO` 只说明编译器/汇编器是否能生成相应指令，不能证明
CPU 实现了这些扩展。

`TOOLCHAIN_NEEDS_EXPLICIT_ZICSR_ZIFENCEI` 和
`TOOLCHAIN_NEEDS_OLD_ISA_SPEC` 是工具链兼容选项，也不是 CPU 能力开关。RVISS
仍应在自己的 ISA profile 中单独描述 `Zicsr` 和 `Zifencei`。

## 非对齐访问选项

Linux 用一个互斥 choice 选择非对齐访问策略：

| 选项 | 含义 |
| --- | --- |
| `RISCV_PROBE_UNALIGNED_ACCESS` | 启动时探测速度；不支持时陷阱进入内核并仿真 |
| `RISCV_EMULATED_UNALIGNED_ACCESS` | 明确要求缺少硬件支持时由内核仿真 |
| `RISCV_SLOW_UNALIGNED_ACCESS` | 假定硬件支持，但速度较慢；依赖 `NONPORTABLE` |
| `RISCV_EFFICIENT_UNALIGNED_ACCESS` | 假定硬件快速支持，并启用更激进的优化；依赖 `NONPORTABLE` |

`RISCV_MISALIGNED` 是内部实现符号，不是用户直接选择的策略。该组配置描述的是
Linux 如何使用非对齐访问，和 RISC-V ISA 是否允许某种访问不能简单画等号。RVISS
当前默认严格检查并产生 misaligned exception，因此不能仅因为 Linux 选择了
`RISCV_PROBE_UNALIGNED_ACCESS` 就宣称 RVISS 已实现硬件非对齐访问。

## 单 hart、多 hart 和平台选项

| 选项 | 含义 | 当前 RVISS/Linux 基线 |
| --- | --- | --- |
| `SMP` | 内核多 CPU 支持 | `n` |
| `NR_CPUS` | 最大 CPU 数；只有 SMP 开启时有意义 | 当前不适用 |
| `HOTPLUG_CPU` | CPU 热插拔 | 当前不适用 |
| `NUMA` | NUMA 内存和调度 | 当前不适用 |
| `RISCV_ALTERNATIVE_EARLY` | 更早执行 alternatives 替换 | 按平台需要 |
| `RISCV_DMA_NONCOHERENT` | 非一致 DMA 的缓存同步路径 | 当前不启用 |
| `RISCV_NONSTANDARD_CACHE_OPS` | 非标准 cache 操作回调 | 当前不启用 |
| `RISCV_ISA_FALLBACK` | 兼容旧版 DT `riscv,isa` 字符串 | 固定 DT 使用新属性，设为 `n` |
| `RISCV_SBI_V01` | SBI v0.1 兼容 | `n` |

`RISCV_TIMER`、`RISCV_INTC`、`RISCV_APLIC`、`RISCV_IMSIC` 等属于中断/时钟驱动
配置，不是 ISA 扩展。`OF`、`OF_FLATTREE` 等属于设备树支持，也不能映射成
`RVISS_ISA_*`。

## 对 RVISS 配置的结论

Linux 配置不能机械转换成 RVISS 的同名开关。RVISS 只有在指令、CSR、异常、MMU、
设备可见行为和测试都完成后，才应提供对应能力开关：

- 当前 RV64 Linux 基线：I/M/A/F/D/C、Zicsr、Zifencei、Zicntr、M/S/U、PMP、Sv39。
- 当前 Linux 工件关闭：V、Svnapot、Svpbmt、Zawrs、Zba、Zbb、Zbc、Zicbom、Zicboz。
- `SMP`、SBI、非对齐访问、设备树和 alternatives 应作为运行环境/策略配置单独
  管理，不能混进 `misa` 扩展位。
- `RVA23` 不是 Linux Kconfig 的一个总开关。只有 RVA23 所需的每个用户态和特权
  扩展都实现并通过验证后，RVISS 才能提供相应 profile 预设。

更完整的 Linux/RVA23/RVISS 映射和后续实现顺序见
[`docs/plans/t020_linux_rv64_isa_config.md`](plans/t020_linux_rv64_isa_config.md)。

## 来源和复核命令

固定源码：`/home/yang/study_dir/riscv_embedded/linux-6.11.4`；工作区构建副本：
`build/linux/source/linux`。复核配置和编译 ISA 的命令：

```sh
rg -n '^config (RISCV|ARCH_RV|MMU|FPU|SMP|NONPORTABLE|PGTABLE|PAGE_SIZE|CMODEL)' \
  build/linux/source/linux/arch/riscv/Kconfig
sed -n '60,100p' build/linux/source/linux/arch/riscv/Makefile
rg -n '^(CONFIG_(64BIT|ARCH_RV64I|MMU|FPU|RISCV_|SMP|PAGE_SIZE|PGTABLE|NONPORTABLE))' \
  build/linux/artifacts/linux.config
```

Linux 主线的对应入口是 [`arch/riscv/Kconfig`](https://github.com/torvalds/linux/blob/master/arch/riscv/Kconfig)。
