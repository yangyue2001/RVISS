# RVISS：RV64GC/Linux 的 AI Coding 开发指导

本文指导 agent 如何推进 RVISS；它不是“已经实现”的功能声明。
目标为：RV64GC、稳定启动 Linux、易于增加扩展、研究 trace、RTL reference、
RVISS 自身与 Spike 对拍，以及可恢复的 checkpoint。当前只做 RV64 功能模型。

## 1. 项目契约：先固定运行环境，再开发功能

### 1.1 默认方案与待锁定内容

平台、固件和 Linux 工件已按用户的本地源码与 FW_PAYLOAD 要求固定，见
[Linux 启动基线](linux_boot_baseline.md)。2026-10-12 的参数选择见
[profile v1](rv64_profile.md)，实现时仍需与固定 Spike/RTL 核对。

| 项目 | 初始方案 | 如何固定 |
| --- | --- | --- |
| CPU | RV64，单 hart，M/S/U | hartid、复位状态、可见能力写入 profile |
| 用户 ISA | I/M/A/F/D/C + Zicsr/Zifencei | 记录每个扩展的规范版本；G 定义见[命名规范](https://docs.riscv.org/reference/isa/v20260120/unpriv/naming.html) |
| 特权架构 | Linux/QEMU 基线固定为 1.12，Bare/Sv39 | 实施前与固定的 Spike 配置核对；不要默默混入更新的可选扩展 |
| 计数器 | 支持 Linux/固件需要的 time/cycle/instret 访问 | 单独定义 Zicntr、M 计数器及访问控制；不假设 GC 包含全部计数器 |
| 平台 | 最小的 QEMU virt 兼容机器，RAM 固定 256 MiB | DTB、MMIO 地址、IRQ、timebase 和输入策略统一维护 |
| 固件 | 固定版本 OpenSBI generic，使用 FW_PAYLOAD | 内嵌 Linux Image 和 DTB；内核内置 initramfs，不混用其它固件交接协议 |
| 系统 | 固定 Linux + BusyBox initramfs | 固定源码、config、工具链、ABI、镜像哈希 |
| 独立 oracle | 固定 commit 的 Spike | 同步扩展、特权版本、PMP、对齐策略和 CSR 能力 |
| 研究输出 | 退休指令、分支、访存、异常、ROI、BBV | 不把功能模型的模拟时间解释为真实 IPC/CPI |

完整 GC 与“启动某个 Linux 镜像”是两个验收目标。F/D 不是所有 Linux 启动方式的普遍前提，
但完整 GC 必须实现 F/D；使用 LP64D 用户态时还需要完整的浮点状态管理。
Sv39 足以作为初始分页目标，Sv48、Zihpm、V、H、Sstc 和 AIA 按实际任务增加。

建议首版还固定：小端、C 启用时 IALIGN=16、16 个 PMP entry、ASIDLEN=0、
硬件按规范更新 PTE A/D、RAM 非对齐数据访问产生异常、MMIO 仅接受设备声明的宽度。
这些都是待 profile 任务核对的实现选择，不是全部 RISC-V 实现必须相同的规则。
若固定的 Spike、固件或目标 RTL 采用其它合法策略，先调整对应 profile 与测试，
不要在执行过程中无记录地改策略。PMA/原子能力也应明确，初始 MMIO 不默认支持 AMO。

### 1.2 当前不做的事情

不开发多 ISA/RV32 层、JIT、通用插件框架、流水线或乱序周期模型。
初始 Linux 链路用 initramfs；磁盘、网络和多 hart 后置。
允许将 trace 交给独立预测器/cache 分析程序，但分析不能改变 RVISS 的功能执行。

### 1.3 三种不同的完成声明

- **实现完成**：代码存在，相关构建和局部测试通过。
- **语义验证完成**：固定配置下，边界用例、架构测试和独立差分通过。
- **系统验收完成**：固定固件/内核/用户态长时间运行，并验证 checkpoint 恢复。

任何状态都要附证据。不能把“有函数”“导出符号”“打印 Linux banner”写成整体完成。

## 2. 2026-10-09 的源码基线

本节为 2026-10-09 的历史审查，当时仅做只读检查与规划。
2026-10-12 的 [T001](plans/t001_baseline.md) 已 verified：修复公共头与直接构建依赖，
源码重建 CLI/ref，minimal 6 个 unit（101 checks）及两项 smoke 通过。
最新支持状态见 [README](../README.md)，固定参数见 [profile v1](rv64_profile.md)；
下表的构建失败已修复，其余语义缺口不能因基线通过视为完成。
随后 [T003](plans/t003_execution_boundary.md) 已验证精确 Bare fault、异常不退休、
独立中断事件、逻辑时间/预算及最小 trace 等价；下表保留历史观察，最新任务为 T004。

| 能力 | 观察到的源码事实 | 当前判断 |
| --- | --- | --- |
| 构建 | 默认 gcc 经 ccache 失败；显式 /usr/bin/gcc 后缺 cpu/interrupt.h | 基线尚不能重新构建，测试未执行 |
| RV64I/系统指令 | instr/i 与 instr/system 有表和执行函数 | 有实现；不能据此宣称一致性测试已通过 |
| M/A/F/D/C | 没有对应执行目录；decode.c 遇到 16 位编码返回 false | 待实现 |
| CSR/特权 | 基本访问表存在；misa 复位为 0；陷阱一律进 M；mtval 清零 | 特权语义尚不完整 |
| MMU/PMP/TLB | satp 非 Bare 时 panic；PMP/TLB 为占位 | Sv39 未实现 |
| 内存 | 未映射读返回 0；写返回 false，虚拟写入口没有传递故障 | 正确异常通路需要优先补齐 |
| 退休 | interp.c 对非法编码也增加 cpu.instret/sim.instret | 需要区分异常与退休 |
| 设备 | CLINT 有指令数节拍；UART 仅输出；PLIC claim/pending 恒为 0 | 不能作为 Linux 完整设备验收 |
| Linux | monitor 固定 minimal 板，缺固件/DTB/内核/initrd 的完整装配流程 | 尚无稳定启动证据 |
| checkpoint | 原生 C 结构 + 全量 RAM；不保存设备和 sim 计数；CPU 不在校验覆盖内 | 有骨架，不能保证 Linux 恢复等价 |
| difftest | regcpy 仅传 GPR/PC/priv；csrcpy 参数与本地 NEMU 的 4096-CSR 数组不同 | 相同符号不等于兼容 ABI |
| 嵌入 API | rviss.h 有声明，但源码没有对应 rviss_create 等实现 | 不得对外宣称 API 已可用 |

工作区未发现 Git 仓库。恢复历史版本的能力尚未建立；建议先建立 Git 基线，
按小切片管理改动，避免仅凭聊天记录或旧 build 产物恢复。

README 中部分“Implemented scope”与上述源码冲突。应优先修正文档状态。
已有文档中“所有观察点只在 ISA 无关文件”“PC 不同即可判断 taken”
“CSR 地址即可决定全部访问权限”“未来 JIT target 不改”均不可作为已证明的约束。

首个实现任务已写在 [t001_baseline.md](plans/t001_baseline.md)。

## 3. 架构：保留少量明确接口

### 3.1 所需边界

```text
monitor / CLI / public API
            |
machine：镜像、DTB、内存图、设备实例、IRQ 接线、时间/输入策略
            |
engine：step / run / stop，处理执行边界
            |
target/riscv64：取指译码、扩展语义、CSR、特权、陷阱、MMU/PMP
            |
物理地址空间：RAM / ROM / MMIO → 设备
            |
执行结果 → trace / 诊断 / difftest 比较
暂停的完整机器状态 ↔ checkpoint / difftest 初始化
```

这描述职责，不要求一次重排目录。沿用现有 include/src 布局。
engine 可以认识 RV64 的公开执行接口；项目只做 RV64，不必假装所有边界都 ISA 无关。
现有 rtl 原语可以复用；不要为未来 JIT 再增加 IR 或承诺原函数指针语义自动支持 JIT。

### 3.2 最先稳定的接口：一次执行的结果

先确定一个小型的执行结果契约，再开发 ISA。下面是字段示意，不是冻结的 C ABI：

```text
StepResult:
  event_seq, retired_total
  outcome = retired | synchronous_trap | interrupt | wait | halted | host_error
  pc_before, pc_after, privilege_before, privilege_after
  raw_instruction, instruction_length, instruction_valid
  decoded_operation, extension, register_writes
  control_flow = kind + taken + target（仅适用于控制流）
  guest_memory_effects
  trap = cause + epc + tval（若存在）
```

规则：

- 同步异常指令不退休；目的寄存器和访存结果只能在访问成功时提交。
  不必建立昂贵的通用事务框架；明确成功/失败返回值即可。
- 规范允许的页表 A/D 等隐式副作用按规则保留，不能把所有故障都粗暴地“整条回滚”。
- 中断单独记录边界。不要把跳入 trap 的 PC 当成当前分支的目标。
- 非法编码也有事件；取指失败时编码可能不完整，用有效位说明。
- run(n)、步数上限和 difftest_exec(n) 必须各自说明 n 是尝试次数、退休数还是执行事件。
  可保留 legacy 行为，但要由 adapter 映射；预算还需限制异常循环。

**分开四种量**：事件序号、累计退休总数、客户机 minstret/cycle、平台逻辑时间。
客户机可以改计数器或使用 mcountinhibit，不能用这些 CSR 作为 trace 唯一 ID 或 timer 唯一来源。
同步异常不增加 instret，ECALL/EBREAK 也如此。[计数器规范](https://docs.riscv.org/reference/isa/v20260120/unpriv/counters.html)

### 3.3 内存访问与 CSR

访存接口传递 access kind、地址、长度和访问上下文，返回数据或结构化 fault。
区分 fetch/load/store/AMO、客户机显式访问、页表访问、设备 DMA、
镜像加载和调试读取；不能全部套用 CPU 的当前特权。
物理层报告未映射/宽度/设备访问失败，RV64 层产生对应 access fault/page fault。

取指先读 16 位，根据编码决定是否再读第二个 halfword；16 位指令不能无条件读 4 字节。
保留原始编码与原始长度；C 展开不能把顺序 PC、链接地址或 tval 改成 32 位展开后的值。

CSR 表负责地址、存储、名称、能力；访问处理负责 WARL/WPRI、委派视图、
counteren、TVM/TW/TSR、FS/SD、硬件与软件 pending、读写副作用。
“加 CSR 只填一行”和“csr[9:8] 比较权限”只适合简单 CSR，不能代替这些规则。
guest CSR 写入与 checkpoint/调试的原始状态恢复使用不同接口。

### 3.4 为 H 留边界，不提前实现 H

H 不是新增 instr/h 目录就结束。它还影响 HS/VS/VU 状态、
CSR 重定向、两阶段翻译、页表遍历中的 G-stage、guest fault、trap 与中断。

现在只做三件事：

1. 集中计算有效访问上下文，不在各指令里散布 cpu.priv/satp 检查。
2. 翻译接口返回 fault 来源与地址；页表 walker 不把所有阶段都硬编码为同一种访问。
3. CSR 解析与存储、trap 路由与最终写 CSR 分开；外部状态格式不暴露 CPU_state 布局。

未来增加 virtualization 状态时扩展这些接口。不要把 H 简化为“PRV_H=2”；
规范中的 VS/VU 是 nominal privilege 与 virtualization 状态的组合。
H 的边界参考已发布的[特权规范 H 章节](https://docs.riscv.org/reference/isa/v20240411/_attachments/riscv-privileged.pdf)；
本阶段不填写虚假的 H 状态或虚假的支持位。

## 4. 开发顺序：尽早对拍，逐步进入 Linux

### 4.1 依赖关系

```text
P0 可复现基线 + 固定 profile
            |
P1 执行结果 / 精确异常 / 最小诊断
            |
P2 Spike 最小对拍（I + RAM + 同步异常）
            |
      +-----+------------------+
      |                        |
P3 M/A/C/F/D             P4 CSR/委派/Sv39/PMP
      |                        |
      +----------+-------------+
                 |
P5 virt + 固件/DTB + 完整 checkpoint
                 |
P6 Linux → 用户态 → 长稳/恢复
                 |
P7 RTL reference 完整集成；P8 研究 trace/ROI/BBV
```

这是依赖图，不是同时写所有模块的指令。
最小 trace、最小 reference 状态传输和短程序 checkpoint 可以提前做，
每个扩展完成就接入 P2 的差分回归。
Linux 工件也可提前在 QEMU 验证，避免等到 P6 才准备。
只在当前目标要求时做 H、其它扩展和模拟速度优化。

### 4.2 里程碑与验收

| 阶段 | 交付 | 必须看到的证据 |
| --- | --- | --- |
| P0 | 修复基线、实际状态表、固定 profile/工具链 | 从源码构建；单测/smoke 真正执行；版本与命令可复现 |
| P1 | step/fault/退休/中断契约、最后 N 条事件 | ECALL/非法编码/访存 fault 不退休；无错误寄存器写入；异常循环有预算 |
| P2 | 固定 Spike adapter、逐事件比较、失败包 | 正常、trap、store 都比较；故意注入差异能被捕获；每次从同一状态开始 |
| P3 | 分组完成 M → A → C → F/D | 每组边界用例 + ISA 测试 + 独立对拍；宣称能力与实际一致 |
| P4 | CSR WARL/委派/返回、Sv39、PMP | M/S/U 测试；page/access fault；4K/2M/1G 页面；counter 权限 |
| P5 | virt 板、固定启动工件、设备、完整 snapshot | QEMU 验证同一工件；裸机/固件跑通；设备状态恢复等价 |
| P6 | OpenSBI→Linux→BusyBox→稳定 shell/自动 workload | 多次冷启动、用户态 FP/原子/分页、timer 和恢复后长稳通过 |
| P7 | 可被实际 RTL harness 加载的 ref | init/同步/执行/trap/store contract 测试及真正 RTL smoke |
| P8 | 分支/访存/BBV/ROI 数据流及离线 reader | trace 开关前后状态等价；语义分类正确；恢复后的序号和 ROI 连续 |

各阶段可拆多个任务。勾选阶段必须引用报告，不能只靠任务清单上的 [x]。
Sv39 先实现正确 walker；TLB 属于加速，可在基线通过后加入并与关闭 TLB 的执行差分。
写 satp 自动 flush 若作为保守实现须记录；它不能替代 sfence.vma、ASID、全局映射规则。

### 4.3 各模块的重点测试

| 模块 | 容易漏掉的边界 |
| --- | --- |
| I/M | x0 写丢弃但 load 仍能 fault；rd/rs 别名；*W 符号扩展；除零、INT_MIN/-1；mulh signed/unsigned；宿主 C 溢出/移位 UB |
| A | LR/SC 地址与宽度、保留失效、失败无 store；AMO.W 返回符号扩展；min/max signed/unsigned；aq/rl；对齐/PMP/page fault |
| C | RV64 编码差异、reserved/hint、寄存器压缩映射、跨页 32 位指令、位于页末的合法 16 位指令、链接 PC +2 |
| F/D | NaN boxing、sNaN/qNaN、signed zero、subnormal、RMM、动态舍入、融合 FMA、转换边界、fflags 累积、FS/SD、上下文切换 |
| CSR/trap | CSRRW rd=x0 的读抑制、CSRRS/RC 源=x0 的写抑制；delegation、全局中断使能的特权关系、xRET/MPRV、tvec vectored、tval/epc |
| Sv39/PMP | canonical VA、非法 PTE、superpage 对齐、SUM/MXR/U、MPRV、A/D、页表访问保护、PMP 优先级/lock/partial overlap、fault 分类 |
| 设备 | MMIO 宽度/offset、UART DLAB/IER/LSR、timer 半字访问、PLIC priority/pending/enable/threshold/claim/complete、M/S context |
| 恢复 | 活跃 timer、中断线、LR reservation、写过的 CSR、FP flags、逻辑时间、输入 cursor、首次恢复后的 fault |

A 的 aq/rl 在单 hart 顺序功能模型中可以不产生额外调度，但必须解释模型边界。
SC 成功/失败、保留集合和异常优先级中的合法实现差异按 profile 与参考对齐，
不能通过无限 skip 或强制改正确结果解决。

F/D 使用固定 SoftFloat 版本，RISC-V 的 NaN/boxing/转换规则仍由 target 处理。
软浮点库会涉及舍入和异常状态，恢复/reset 后必须一致；未来多实例要避免全局状态交叉污染。
参见 [SoftFloat 接口文档](https://www.jhauser.us/arithmetic/SoftFloat-3/doc/SoftFloat.html)。

## 5. Linux：固定一个可重放的启动包

### 5.1 固件交接与工件

用户已选择 OpenSBI FW_PAYLOAD。源码输入在
`/home/yang/study_dir/riscv_embedded`，实际配置、构建命令和工件见
[Linux 启动基线](linux_boot_baseline.md)。BusyBox initramfs 内置到 Linux Image，
Image 与最小 DTB 再内嵌到 fw_payload.bin/elf。
当前固定 OpenSBI 0x80000000、kernel 0x80200000、DTB 搬移到 0x8f000000，
RAM 为 256 MiB。raw loader 将整个 fw_payload.bin 放到 0x80000000；
不要再次单独加载 kernel/initrd 覆盖包内内容。
实现前检查完整映像区间、ELF segment、内核 image_size、保留区和入口。

进入内核时 a0 为 hartid、a1 为 DTB 地址、satp=0；RV64 内核按 boot 文档满足
2 MiB 对齐及固件内存保留约束。[Linux 启动协议](https://docs.kernel.org/arch/riscv/boot.html)
FW_PAYLOAD 的入口、payload offset 和 FDT 搬移由构建参数决定，RVISS loader 必须匹配。
[OpenSBI FW_PAYLOAD](https://github.com/riscv-software-src/opensbi/blob/master/docs/firmware/fw_payload.md)

维护一个启动 manifest：版本/commit、config 哈希、编译器、ISA/ABI、内存布局、
所有镜像 SHA256、bootargs、确定性输入与预算。先在固定 QEMU 上启动相同工件，
不把 QEMU 生成的完整 DTB 原样用于缺少对应设备的 RVISS。
固件、内核或工具链升级另开任务，不在定位故障时同时改变它们。

### 5.2 平台最小能力

- DTB 真实描述 CPU ISA/MMU、RAM、reserved firmware、CLINT、PLIC、UART、
  interrupts/interrupts-extended、timebase-frequency 与 chosen。
  内置 initramfs 不需要 DT 的 linux,initrd-start/end；OpenSBI 动态增加固件保留区。
- 单 hart 仍需要正确的 S 外部中断路径和所描述的 PLIC contexts。
  UART 能打印不等于 Linux 串口驱动可用；DLAB、寄存器和 IRQ 需要测试。
- SBI 服务由真实 OpenSBI 提供。第一版不要在 RVISS 中另写一套 SBI 快捷仿真。
- 一套平台时间/输入接口同时服务 standalone 与 ref：
  standalone 自主确定性推进；RTL ref 使用 DUT/harness 的时间、MMIO 返回和中断输入。
  ref 不再自主推进一个与 DUT 无关的 CLINT。
- 逻辑时间不是周期预测。WFI 初期可按规范作为 NOP，但要检查权限，
  时间需继续前进；以后阻塞/快进到下一确定性事件时也要记录和验证。
- Linux 初始化会探测不存在的 CSR/扩展，预期非法指令由固件/内核处理。
  不将这种探测当作宿主 panic，也不为通过启动而伪造支持位。

### 5.3 “稳定启动”分四级验收

| 级别 | 验收 |
| --- | --- |
| B0 | OpenSBI 完成并进入正确的 S-mode kernel 入口 |
| B1 | Linux 开启 Sv39，进入 PID 1，执行 initramfs 的 /init |
| B2 | BusyBox shell可用；自动输入或 init 脚本运行 syscall、文件、分配/释放、FP/原子、timer workload |
| B3 | 固定工件至少 3 次冷启动；固定 workload 重复运行；多个阶段 checkpoint 恢复后继续并通过同一验收 |

三次是初始回归预算，不能代替后续长稳覆盖。长稳预算明确为 workload 次数、
指令/事件上限和宿主 timeout；启动时记录所有错误与超时。
输出 RVISS_LINUX_OK 后必须有受测的退出渠道，例如 DT 描述的 test finisher
或宿主按测试协议停止，不要求 Linux 执行 NEMU 私有退出指令。

每次故障保存首个差异/异常、最近事件、寄存器/CSR、访存/PTE、设备状态、工件 manifest。
Linux 全部串口日志不必逐字相同：ASLR、随机种子、版本信息可不同；
确定性检查固定输入后比较定义好的架构状态、输出标记和事件摘要。
内核 banner 和脚本 marker 各自只能证明其到达，不证明 ISA 全正确。

## 6. 两种 difftest：共享语义，不混淆平台

```text
验证 RVISS：RVISS DUT → 固定 Spike adapter → 对比器
验证 RTL： RTL commit stream → RVISS reference adapter → 对比器
```

Spike 官方说明其内部 C++ 接口不属于稳定公共 API。
因此固定 commit，C++ 桥隔离在 tools/ 或 adapters/，用 RVISS 的 C 契约连接；
不要假设普通 spike 二进制天然提供 NEMU 的 difftest_* ABI。
[Spike README](https://github.com/riscv-software-src/riscv-isa-sim)

### 6.1 尽早建立 Spike 差分

P2 先覆盖确定的裸机 I 指令、RAM load/store、同步异常，无异步设备。
逐步扩展到每个扩展、特权、页表。文本 commit log 可用于原型诊断，
正式对比还需全量初始状态、寄存器/CSR、store 和 trap；
不能仅比助记符或最后 PC。

两个模型初始化为同一 reset/profile、内存和架构状态。可通过受控启动前导程序
统一初始化，明确跳过的启动区间；不能忽略随机未初始化寄存器。
执行过程中禁止反复把 DUT 状态复制给 REF 来“修正差异”。

### 6.2 明确比较范围和允许差异

优先比较 pc_before/after、raw/length、GPR/FPR 写回、priv、trap、
关键 CSR、成功 RAM store 的 paddr/data/byte mask，以及有需要的页表隐式更新。
MMIO read 若有副作用只能发生一次，采用明确的输入注入/共享设备协议。
对时间/随机/SC/FS dirty 等差异，先判定规范合法性和配置一致性；
比较掩码必须有规则依据、作用范围和回归用例，不能扩大 skip 消除真正 bug。

完整 Linux 与 Spike 对拍需要同一平台/输入/中断边界，不能拿不同 board 的日志强行逐条比。
先做好短程序和特权回归，再扩大系统差分；QEMU 用于独立启动工件与平台交叉检查。

### 6.3 RTL reference 的硬要求

- dlopen 后 init 必须完成内存/CPU/decoder 初始化，不能依赖 monitor/main 先执行。
- 明确执行事件、trap 和多条退休在 RTL commit bundle 中的次序。
  以 RTL 架构提交顺序推进，不按 RTL 周期计数推进 RVISS 指令。
- RTL pending store 与已提交 store 区分；RAM byte mask、拆分写、AMO 和失败 SC 均有协议。
  loader/debug/page-table 写不能混进 guest store-commit 队列。
- 陷阱、中断、读 time/MMIO 等用 DUT/harness 确定的边界和输入。
  guided exec 若必须存在，应局限于声明的外部/实现差异，不覆盖普通算术/访存错误。
- 先检查真实 consumer 的头文件、size/offsetof、配置条件和 dlsym 原型，
  再实现 NEMU 兼容 adapter。当前 NEMU 的 csrcpy 是 4096 项数组，
  regcpy 的 F/H/V 等布局受配置影响，不能只匹配函数名字。
- 新原生 ABI 可用 version/size/features；legacy adapter 保持实际 consumer 契约。
  不能单方面改现有 ABI 或用 variadic 吞掉不兼容调用。
- 添加 dlopen/reset/step/store/error 的 contract test。
  没有真实 RTL harness smoke 前，只宣称接口验证，不能宣称 RTL 集成完成。

失败报告必须包含最近事件、双方字段和第一个不同位置；非一致性应传播为测试失败。
`difftest_status() == 0` 不能仅因空实现而被解释为已通过。

## 7. Trace：只记录确实发生的行为

先实现小型不可变事件输出接口，再接 ring、文本、二进制、counter/BBV。
不需要通用事件总线、线程系统或动态插件。无 sink 时提供可关闭路径，
记录实际开销；运行时一次判断不称为零开销。

- 分支类型/条件/目标来自语义执行结果；taken 不能由 pc_after != snpc 推导，
  条件成立但目标恰好等于 snpc 的分支仍是 taken。
- 保留原始 C 编码/长度；区分 branch/jal/jalr/call/return 和 trap。
- 内存 trace 至少记录 VA、PA、宽度、数据、读写、fault、RAM/MMIO 与访问来源。
  fetch、guest data、页表遍历、DMA、loader/debug 分开标识。
- retire 流只含成功退休；fault/trap 独立事件，通过 event_seq 对齐。
- 格式头放 schema、profile、工件/运行身份、起始 checkpoint，
  记录中保留必要字段。不要每条都重复整个 manifest。
- ROI 支持按 PC/符号/特权/退休区间选取；OS/固件是否纳入明确注明。
- BBV 规定基本块 ID、结束边界、interval 的退休口径、短末段、代码修改后的处理。
  后续 SimPoint/采样恢复另开任务，不能只靠一个计数表宣称已支持。

用途为指令混合、热区、访存局部性、分支序列、预测器准确率和外部性能模型输入。
只靠退休流无法生成真实 wrong-path fetch、cache latency、IPC 或周期数；
若外部模型需要这些信息，另定义模型与假设。
打开/关闭 sink 后比较架构状态和输出；性能差异以宿主模拟速度单独报告。

## 8. Checkpoint：完整状态与可验证恢复

### 8.1 先区分两种目标

- **RVISS 内部恢复**：同一 profile/兼容版本继续执行，不必重跑 Linux。
- **跨模型/RTL 启动快照**：需要明确的导入状态、内存、设备与恢复协议；
  不假设 RVISS 文件能直接装进 Spike 或 RTL。

### 8.2 状态清单

CPU/CSR/FPR、特权、LR/SC 保留、RAM、设备寄存器、pending/claim、
输入 FIFO/cursor、逻辑时间、累计退休数和 event_seq 均需覆盖。
state 清单、reset、export/import、checkpoint、difftest 复用一致的显式状态接口，
不直接 fwrite CPU_state 或包含指针的设备结构。

在指定执行边界暂停：完成当前指令/trap/中断与设备更新，
没有进行中的 MMIO 回调或待提交操作，再保存。
恢复设备后重新计算 IRQ 线；TLB/译码缓存可丢弃重建。
observer/predictor 状态按“连续分析恢复”或“重新 warmup”策略选择；
无法恢复的 observer 不允许悄悄声称统计连续。

### 8.3 格式、失败与等价测试

header + 版本化 section + 4K RAM 页，使用明确字节序和字段长度。
完整状态及元信息都纳入校验；保存时临时文件成功写完后再替换目标。
加载先验证版本、能力、范围、长度、校验和，失败不能留下半恢复的活动机器。
对版本不兼容明确拒绝，不能把未来所有版本兼容当作首版要求。
稀疏/压缩/dirty 增量按测量收益逐步加入，正确性优先。

验收公式：

```text
连续执行 K+N
  == 执行 K → 保存 → 退出 → 新进程恢复 → 执行 N
```

比较完整状态、RAM、设备、事件摘要、串口后缀。恢复点覆盖 timer pending、
S/U 分页、浮点、LR/SC；损坏 CPU/device/RAM 字段应被拒绝。
保存 checkpoint 不会回滚已经发送的宿主输出，应定义输出截断/续写策略。

## 9. Agent 的任务与验证协议

### 9.1 每个任务用这个契约

```text
任务 ID / 对应里程碑：
目标：改变哪一个可观察行为。
输入：相关源码、profile、规范章节、参考实现函数、失败包。
范围：允许改的模块；允许的契约演进。
非目标：不顺手增加的能力。
验收：正例、边界、异常、回归和独立 oracle。
验证命令：当前已有命令；要新增的命令另标“待实现”。
完成证据：命令/配置/版本/结果文件。
```

跨模块任务写到 docs/plans/<id>.md；普通小任务无需每次写完整 PRD/PDD。
任务按 pending → active → implemented → verified 或 blocked 更新。
blocked 写明具体依赖和已做的工作；implemented 不等于 verified。

### 9.2 工作循环

1. 搜索入口、调用方、测试和配置，读取当前任务必要的规则。
2. 固定语义假设；重大歧义询问，内部实现决策记录后继续。
3. 编写能区分错误实现的边界/复现，不复制实现函数生成期望。
4. 完成最小切片，运行相关测试与独立差分。
5. 捕获第一个错误，缩小输入；一次修复一个根因。
6. 按影响范围回归，检查错误传播、配置裁剪和状态恢复。
7. 更新任务证据和必要的行为文档，最终说明未测内容。

构建失败、依赖缺失、skip、timeout 不能写成通过；扩大改动、修改期望、
对 REF 同步 DUT 结果都不能替代查明差异。

### 9.3 验证层与命令

当前可以尝试的命令：

```sh
make CC=/usr/bin/gcc check
```

它在 2026-10-09 审查中失败；2026-10-12 的 T001 已从源码重建并实际通过，
证据见 [T001](plans/t001_baseline.md)。不以 build/ 旧二进制替代重新构建。
今后建议实现入口为 check-isa、check-diff、check-priv、check-checkpoint、
check-ref-api、check-linux；这些目标目前不存在，不能当成现成命令执行。

验证分层：局部边界 → 真实汇编路径 → 固定架构测试集 → Spike 差分 →
固件/特权集成 → Linux 长稳 → RTL consumer。
架构测试版本必须固定；当前上游 ACT 与旧 RISCOF/签名流程不同，
选择实际版本对应的适配方式，不按过时教程猜命令。
[架构测试仓库](https://github.com/riscv/riscv-arch-test)
测试清单、执行数、跳过原因和随机种子持久保存；测试通过仍不是穷尽证明。

有价值的属性检查：trace 开关等价、TLB 开关等价、checkpoint 等价、
reset 后重复等价、非法编码不退休。优先这些跨实现/跨模式性质。
对实现敏感路径按影响增加 sanitizer 或随机差分，不机械地对文档小改跑全部 Linux。

### 9.4 Review 清单

Review 聚焦：规范不符、错误依赖、UB、精确异常、权限/故障归类、
state 漏项、假 ABI、计数/时间混淆、未覆盖边界和无证据 skip。
要求位置、触发条件、预期/实际行为、规范或用例依据。
没有完整的参考或测试证据时写“未确认”，不要写“符合 RISC-V”。

### 9.5 长任务续跑

检查点记录：当前 profile/工件身份、已验证任务、正在改的接口、
确切失败命令、首个差异、未完成项和下一步。
设计决策记录“原因 + 代价 + 证据”，只在确有长期影响时写 ADR。
不把每次聊天全文追加到必读 memory bank；新会话从任务记录和源码继续。

## 10. 参考实现地图与后续第一步

| 工作 | NEMU 参考 | QEMU 参考 |
| --- | --- | --- |
| M/A/C | src/isa/riscv64/instr/rvm、rva、rvc | target/riscv/insn16.decode、insn32.decode、insn_trans |
| F/D | src/isa/riscv64/instr/fp.c、rvf、rvd，engine/interpreter 的软浮点适配 | target/riscv/fpu_helper.c、fpu/softfloat.c |
| CSR/trap | src/isa/riscv64/system/priv.c、intr.c | target/riscv/csr.c、cpu_helper.c、op_helper.c |
| MMU/PMP | src/isa/riscv64/system/mmu.c | target/riscv/cpu_helper.c 的 get_physical_address、pmp.c |
| 平台/DTB | 当前机器/设备配置，不能假定 NEMU 跟 virt 一致 | hw/riscv/virt.c、hw/intc、hw/char |
| 对拍/存储 | src/cpu/difftest/ref.c、isa/riscv64/difftest/ref.c、memory/paddr.c | 不假设 QEMU 带 NEMU difftest ABI |
| 快照/研究 | src/checkpoint、cpu/instr_stat.c | target/riscv/machine.c 的状态字段可供遗漏检查 |

本地根路径：
- NEMU：/home/yang/study_dir/rvbosc/NEMU
- QEMU：/home/yang/soft/qemu/qemu

参考顺序：固定规范 → RVISS 当前路径 → 独立测试/Spike → 对应 NEMU/QEMU 函数。
记录函数和版本，处理参考实现中的配置、专用扩展与旧版本差异。
未来要复制第三方代码时核对文件许可并保留来源；不把整个项目框架当作语义 oracle。

T001 基线及 [T003/P1](plans/t003_execution_boundary.md) 已 verified；
下一步执行 [T004/P2](plans/t004_spike_minimal.md) 最小 Spike 差分，再按语义组开发。
核心原则是：agent 自主完成已授权的局部闭环，人审查关键契约与验收证据；
不要求人逐条批准文件编辑，也不授权 agent 一次重写所有子系统。

本指导的 P0–P8 是当前主线；旧 implementation_plan.md 的 M0–M8/TODO 标签
暂时仅作为历史定位信息，不能机械映射完成状态。T001 整理文档时应注明这一关系。
