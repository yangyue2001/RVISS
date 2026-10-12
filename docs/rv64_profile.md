# RVISS 单 hart RV64 profile v1

状态：目标参数已固定（2026-10-12），实现能力以 README 和逐项任务证据为准。
这是 RVISS 执行环境契约，不是 RVA 平台 profile 或规范认证声明。
与固定 Spike 的配置存在差异时，先记录/测试合法策略再修订版本，不临时扩大 skip。

## 规范与能力

非特权指令使用官方 [Ratified-IMAFDQC 快照](https://github.com/riscv/riscv-isa-manual/tree/Ratified-IMAFDQC)：
RV64I 2.1、M 2.0、A 2.1、F/D 2.2、C 2.0、Zicsr/Zifencei 2.0。
[模块版本表](https://raw.githubusercontent.com/riscv/riscv-isa-manual/Ratified-IMAFDQC/src/preface.tex)
列出了这些版本。Zicntr 单独采用 2.0，取
[官方 v20260120 计数器章节](https://docs.riscv.org/reference/isa/v20260120/unpriv/counters.html)，
不因该章节来源更新特权版本或加入其它扩展。
M/S/U、CSR/trap、Sv39/PMP 固定为
[Priv-v1.12（20211203）](https://github.com/riscv/riscv-isa-manual/tree/Priv-v1.12)。
本次公共 cause 定义按该版本
[Machine 章节 mcause 表](https://raw.githubusercontent.com/riscv/riscv-isa-manual/Priv-v1.12/src/machine.tex)
核对；H 和可选扩展的 cause 不在本次公共头内。

| 参数 | 固定目标 | T003 后当前源码 |
| --- | --- | --- |
| CPU | RV64，小端，hartid=0，单 hart，复位进入 M；GPR 清零作为确定性 reset 策略 | 已有；现有测试只覆盖部分状态 |
| ISA | I/M/A/F/D/C + Zicsr/Zifencei，另提供 Zicntr | I/CSR 部分实现；M/A/F/D/C 和 counter CSR 缺失；misa=0，禁止置虚假支持位 |
| 特权 | M/S/U；无 H、V、Sstc、AIA、Sv48、Zihpm | M trap 骨架；委派/权限未完整 |
| 地址空间 | Bare/Sv39，ASIDLEN=0，物理地址宽度 56 位；硬件更新 PTE A/D | 当前只支持 Bare；Sv39/PMP 未实现 |
| PMP | 16 entries，粒度 4 bytes，OFF/TOR/NA4/NAPOT，reset OFF | 占位 |
| 指令对齐 | C 启用后 IALIGN=16；C 实现前 IALIGN=32 | PC/JAL/JALR/取分支检查 IALIGN=32；16 位分段取指，第二段 fault 精确保留 epc/tval；C 未实现 |
| 数据对齐 | RAM 的非自然对齐 load/store/LR/SC/AMO 抛 misaligned；在翻译前检查 | I load/store 在翻译前拒绝非自然对齐，含 MMIO；LR/SC/AMO 未实现 |
| MMIO | 只接受设备声明的 offset/宽度，未映射产生 access fault；不支持 AMO | 全访问区间及 callback 存在性已检查；未映射显式 fault，设备内部 offset/宽度规则仍未完成 |
| 平台 | 固定 Linux virt，RAM 256 MiB；地址、IRQ 和 DTB 见 linux_boot_baseline.md | minimal 默认 RAM 128 MiB，基本地址相同，设备功能与 finisher 不完整 |

csr_machine.h / csr_supervisor.h 中其它扩展的位宏只是一组既有编码定义，
不代表支持这些扩展；WARL/可见位须按本 profile 显式处理。
当前公共 cause 头声明 page fault 编号，同样不代表页表 walker 已实现。

## 时间、计数与预算

目标采用确定性逻辑时间，不读取宿主墙钟来驱动设备；timebase 为 10 MHz。
执行边界的事件序号、累计退休总数、客户机 cycle/instret 和平台逻辑时间分开。
客户机改 CSR 或 mcountinhibit 不影响事件序号、预算或 timer。
正常退休才增加累计退休数；同步异常（含 ECALL/EBREAK）不退休。

目标步进策略：每个执行事件（指令或独立中断边界）推进一个逻辑 tick，
每 128 ticks 推进 mtime 一次；客户机 cycle 按该逻辑步进计数，受其访问/抑制规则控制。
这是功能模型的时间策略，不对应真实硬件周期/CPI。WFI 暂停后的平台时间推进在
设备集成任务中实现，并须另留验收证据。单 hart 不给 AMO aq/rl 虚构宿主并行语义。

T003 已实现上述事件/退休/平台时间分离。engine_step 返回一个事件；
暂停或预算耗尽时返回 false、outcome=stopped，不生成新事件。
engine_exec(n)、si n、--max-instr n 和 difftest_exec(n) 均为指令尝试预算，
含同步异常、fetch fault 和 host error，不含独立中断。终止轮也返回其尝试数。
--max-events 限制总事件；0 表示不限。engine_exec 每次调用另限至多 2*n+1 个
事件（算术溢出时饱和），异常循环按尝试数终止。外部 difftest_raise_intr 走
同一计数入口，无指令尝试/退休，暂停或预算耗尽时不注入；这只验证本地 adapter，
不表示 ABI 初始化或真实 consumer 已可用。
cpu.instret/cycle 仍未接入 counter CSR 和 mcountinhibit；本任务用状态接口写入
这些字段验证平台独立性，不能宣称客户机已能执行 counter CSR。
自定义 nemu_trap 成功执行时计入退休；WFI 仍为 NOP，无 wait outcome。

本轮异常策略：ECALL tval=0；EBREAK tval=故障 PC；非法指令/CSR tval 为已取得的
原始低 ILEN=32 位（16 位编码用低 16 位）；地址 fault tval 为故障虚拟地址。
不支持长于 ILEN 的指令，结果 length 最多 4，valid 表示这些原始字节已取得，
并非扩展已支持。错误 JAL/JALR 在写链接前 trap，仅取分支检查目标对齐。
数据 misaligned 在翻译/映射检查前报告。中断在取指前选择独立边界；
设备在事件结束 tick/update IRQ，下一边界响应。全部仍路由 M，委派/完整优先级
规则后续实现。有效非 Bare 访问报告 host error；PMP 未实现。

## 工具与独立参考

T001 实测：宿主与 riscv64-linux-gnu GCC 11.4.0（Ubuntu
11.4.0-1ubuntu1~22.04）、GNU Make 4.3、bison 3.8.2、flex 2.6.4。
构建使用 /usr/bin/gcc，未验证 PATH 的 ccache 包装器或其它工具链。
smoke 编译为 rv64im/lp64，实际用例不包含 M 指令；此参数不证明支持 M。
Linux/BusyBox 用 lp64d；固件、工件及工具身份继续使用固定 manifest。

spike 不在 PATH。本次找到 nanhu/ready-to-run/riscv64-spike-so：
SHA256 403e524f2d3492d13bd4924d2aacdc3564d19159b89ba16de3516ff7f03200eb，
是 x86-64 共享对象，来源 commit/profile/ABI 尚未确认，未加载或执行。
本机 ibex/vendor/riscv-isa-sim 只有 helper/test 文件，不是完整 Spike 源码；
其 git rev-parse 返回的是 Ibex 仓库提交，不能作为 Spike 版本。
T004 已取得官方 Spike da2f415bb62bec3e93b33b379d787b4262aade9f，归档 SHA256
与源码核对见 docs/validation/t004_spike_manifest.json。独立进程 bridge 已通过
29 cases / 343 events 的 I/RAM/同步异常差分和 3 种故意差异注入。
本阶段关闭 PMP/异步设备，共同输入固定 mstatus.UXL/SXL=RV64；不比较 misa
能力身份、mcycle 计时或 counter 写入。此边界见 T004，不代表目标 GC/Sv39/PMP
已实现，也没有使用上述来源未确认 .so。
