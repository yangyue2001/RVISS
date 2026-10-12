# T003：P1 精确执行边界与 Bare 访问故障

状态：verified（2026-10-12）。依赖 [T001 verified](t001_baseline.md)；
[T002](t002_linux_artifacts.md) 固定工件继续保留，本任务不运行 Linux 验收。

## 目标和输入

交付可观察的单步结果：正常退休、同步陷阱、异步中断分别报告；
失败 load 不写目的寄存器，失败 store 不改变 RAM，异常不计入退休。
异常循环仍受预算限制，平台时间与客户机计数器分离。

输入为 [profile v1](../rv64_profile.md)、engine/interp、Decode、RTL 访存原语、
paddr/vaddr、decode、I/system exec、CSR 访问和 trap；调用方包括 monitor/ui、
difftest_exec/raise_intr 和当前 checkpoint。规范为 Priv-v1.12 的 mcause/mtval、
异常优先级和 Zicntr 2.0 的退休规则。规范是本任务的预期依据；Spike 差分在 P2。

## 已复现的首个差异

T001 的 [诊断源码](../../build/baseline/t001/p1_probe.c) 用正常的取指/译码/执行路径，
RAM 1 MiB、mtvec=0x80000100、max_instr=1；
[实际输出](../../build/baseline/t001/p1-probe-run.log)：

| 输入 | 实际 | 期望 |
| --- | --- | --- |
| 0xffffffff 非法编码 | cause=2，但 cpu/sim.instret=1，engine_exec(1) 返回 0 | 同步 trap，不退休；返回值/事件预算正确 |
| ld x5,0(x6)，x6=0x40000000 未映射 | x5 从 0xdeadbeef 变为 0，PC 顺序推进，无 trap | load access fault，x5 保持原值 |
| ld x0,0(x6)，同一未映射地址 | 无 trap | x0 写丢弃也必须报告 load access fault |
| PC=0x40000000 未映射取指 | 按编码 0 产生 illegal cause=2 | instruction access fault cause=1，编码有效性明确 |

探针进程返回 0 只表示诊断成功运行，不表示上述语义正确。

## 范围与契约

- 定义小型不可变单步结果，至少包含 event_seq、累计退休数、outcome、
  pc_before/after、priv_before/after、raw/length/valid，以及 trap cause/epc/tval。
  成功 store 的地址/宽度/数据与失败访存信息可由结果给最小诊断使用。
  不建立通用事务/插件框架。
- fetch/load/store 的成功/失败沿 paddr → RV64 访问路径 → 执行函数显式传递；
  RV64 集中产生 fault，物理层不注入 CPU trap。loader/debug 访问与客户机访问分开。
  处理未映射、越界和非自然对齐 RAM 访问；故障不得伪退休或提交目的寄存器。
- 取指先读 16 bits，再决定长度。C 尚未实现时，16 位编码报告非法，
  不无条件读跨边界的第二个 halfword；32 位指令第二段故障保留起始 epc 和故障 tval。
  处理 IALIGN=32 的控制流对齐，错误 JAL/JALR 不提交链接寄存器。
- ECALL、EBREAK、非法编码、非法 CSR 和访存故障标明同步 trap；
  当前 M trap 路由保留，trap 信息传入 tval。异步中断发生在独立执行边界，
  不把它算成一条退休指令或上一条分支的结果。
- event_seq、累计退休数、客户机退休计数和逻辑时间独立存储；
  按 profile 计数。完整 counter CSR / mcountinhibit 权限在 P4 单独实现，
  此项先解除 timer/预算对客户机计数的依赖。
- engine_exec(n)、--max-instr、UI si 和 difftest_exec(n) 保留 legacy 指令尝试预算，
  通过 adapter 映射单步事件，修复终止轮返回少 1；不默默改为退休预算。
  纯中断事件不消费指令尝试预算，另用明确事件上限防止无指令进展的循环。
  更新各调用方注释/契约；不改变外部 difftest 符号原型或假定 ABI 已可用。
- 最小最近事件诊断为只读观察者；checkpoint 尚不能保证完整恢复，
  新增状态的保存缺口必须显式记录，禁止以旧格式声称恢复等价。

非目标：M/A/C/F/D、Sv39/PMP walker、CSR WARL/完整委派和 xRET 权限、
真实 WFI 暂停、设备全宽度/offset 校验、PLIC/UART 输入、完整 checkpoint、
Spike bridge、Linux 和 RTL 集成。现有成功 MMIO 行为及 smoke 保持可运行。

## 验收与检查点

- [x] 实施前核对接口及所有调用方，记录预算映射和 trap 优先级。
- [x] 正常短序列真实执行：PC/GPR/RAM、退休数、事件号及返回条数正确。
- [x] 非法编码、ECALL/EBREAK、非法 CSR 各不退休；cause/epc/tval 正确。
- [x] 未映射 load（含 rd=x0）、store、fetch；RAM 边界、对齐与第二 halfword 故障。
- [x] 故障无目的寄存器写入、无显式 RAM 写入；已成功 store 可准确观察。
- [x] timer 中断为独立边界；异常/中断循环在事件和尝试预算下终止。
- [x] CLI/UI/ref 调用方预算一致，停机最后一轮计数正确；原 unit/smoke 回归。
- [x] 观察者开关前后架构状态/RAM/输出等价；诊断给出首个事件差异。
- [x] 保存命令/结果/首差异，更新 README/profile/T003；写下一项 P2 契约。

已有验证入口：make CC=/usr/bin/gcc check。
新增测试：tests/unit/test_step.c（执行/异常/预算）、真实汇编 fault.S 和
tests/cli/test_step.py。check-isa/check-diff/check-priv 等目标尚不存在。
不改原测试期望，不复制实现公式作为 oracle，不通过 skip 获得通过。

实施前：基线重新通过（make CC=/usr/bin/gcc check，101 checks 和两个 smoke）。
修改前归档为 build/baseline/t003/source-before.tar.gz。
实施选择：target 的一步只执行一个架构事件；引擎负责事件/退休/尝试/逻辑时间。
--max-instr/engine_exec/si/difftest_exec 的 n 均保持指令尝试口径；新增
--max-events 是独立全局事件上限。engine_exec(n) 的本次调用另限至多 2*n+1
个事件（溢出时饱和），防止纯中断事件无进展。非法编码记录原始低 ILEN=32 位，
取指只收到首 halfword 时 raw_bytes=2、instruction_valid=false。
非 Bare 有效访问尚未实现时报告 host_error，而不制造客户机 page fault；M 模式
取指不受 satp 影响，数据按 MPRV/MPP 计算有效特权。MMIO 完整设备合法宽度规则后置。

## 实现与真实验证

第一次新增回归保留了原 25 个断言，在修改前得到 16 个失败：
[regression-before.log](../../build/baseline/t003/regression-before.log)。
同一批原断言修改后通过，新增用例后 test_step 共 628 checks；既有 6 个 unit
101 checks 全部保留。没有改原期望、扩 skip 或关闭检查。

核心变化：

- include/cpu/step.h 和 target/riscv64/step.c 定义单事件提交。
  正常执行才退休；ECALL/EBREAK/非法 CSR/取指与数据 fault 统一带 cause/epc/tval。
  引擎不再把“decode false”直接当 illegal，也不在上一条指令结果中混入中断。
- paddr 的 checked 接口返回失败；vaddr/MemAccess → RTL → Decode 明确传递结果。
  RV64/mmu/access.c 集中有效特权、对齐和 Bare 访问；范围计算拒绝溢出/跨区间。
  load 成功才写 rd，JAL/JALR 对齐通过才写链接。store 全范围验证后才写 RAM。
- 引擎管理 attempts/event_seq/累计退休/逻辑 ticks，客户机 instret/cycle 另存。
  CLINT 每 128 ticks 推进 mtime；事件结束更新设备线，下一步才接中断。
  普通停止不会多执行；终止自定义退出指令也返回正确尝试数。
- engine_recent 复制最近 16 个不可变结果；开关仅影响记录，debug trace 读取已完成
  结果。monitor 报预算/宿主错误时给事件序号、PC、raw、cause 和 tval。

执行命令：

```sh
make CC=/usr/bin/gcc check
python3 build/baseline/t003/verify.py
```

两者实际退出 0。verify.py 从无 .config、生成头、旧工具或对象的源码副本开始，
隔离目录 /tmp/rviss-t003-verify-pvhupnjf，依次执行：

```sh
make -j4 CC=/usr/bin/gcc BUILD_DIR=/tmp/rviss-t003-verify-pvhupnjf/out minimal_defconfig
make -j4 CC=/usr/bin/gcc BUILD_DIR=/tmp/rviss-t003-verify-pvhupnjf/out check
make -j4 CC=/usr/bin/gcc BUILD_DIR=/tmp/rviss-t003-verify-pvhupnjf/out ref_defconfig
make -j4 CC=/usr/bin/gcc BUILD_DIR=/tmp/rviss-t003-verify-pvhupnjf/out check
make -j4 CC=/usr/bin/gcc BUILD_DIR=/tmp/rviss-t003-verify-pvhupnjf/out debug_defconfig
make -j4 CC=/usr/bin/gcc BUILD_DIR=/tmp/rviss-t003-verify-pvhupnjf/out check
```

全退出 0；原工作区 .config/生成头哈希与 T001 相同，未切换配置或删除 Linux 工件。
完整命令/退出码为 [results.json](../../build/baseline/t003/results.json)，
[配置与二进制哈希](../../build/baseline/t003/hashes.json)。
最终源码/文档备份 source-after.tar.gz，修改对照 implementation.diff，摘要 summary.json。

| 验证 | 实际结果 |
| --- | --- |
| minimal（隔离 + 原工作区） | 7 unit / 729 checks、3 smoke、CLI/UI 14 checks，全部通过 |
| ref | 新共享库构建；同样 7 unit / 729 checks 通过；此配置按既有规则不运行 CLI smoke |
| debug | 7 unit / 729 checks、3 smoke、CLI/UI 14 checks，全部通过 |
| fault 汇编 | 12 次 trap 核对 cause/epc/tval 和失败写回；202 退休、214 尝试/事件/ticks |
| timer 汇编 | 1033 退休/尝试，1034 事件/ticks；独立中断恰好一次 |
| trace 运行期开关 | UART/fault/timer 的统计及成功退出相同；UART 字节均为 Hello from RVISS! 换行 |
| 最近事件开关 | 同一 RAM store/load、MMIO 输出、ECALL、handler/退出序列 CPU/Sim/RAM/输出字节相同；副本修改不影响记录 |

test_step 还覆盖 7 种 load 的符号/零扩展、4 种 store 的结果元信息、rd=x0、rd=rs1、
失败不提交、aligned 跨 RAM 末尾、56-bit PA 上界、物理访问溢出、16 位编码不多取、
32-bit 第二段 fault、JAL/JALR/取与不取分支、vectored interrupt、暂停/零预算、
全局事件/尝试限制、fault loop、guest counter 写入不影响 timer，以及本地 ref adapter。
原 fault 25 项回归的首差异和 CLI 原始结果保留；没有用日志 banner 代替状态断言。

日志：[minimal](../../build/baseline/t003/clean-minimal-check.log)、
[ref](../../build/baseline/t003/clean-ref-check.log)、
[debug](../../build/baseline/t003/clean-debug-check.log)、
[工作区](../../build/baseline/t003/workspace-final-check.log)、
[fault trace](../../build/baseline/t003/fault-trace.log)、
[timer trace](../../build/baseline/t003/irq-trace.log)。

## 兼容性与未验证项

--max-instr 和 legacy adapters 的 n 仍是尝试数；异常不再伪退休，统计字段改为
retired/attempts/events/logical_ticks。--max-events 是新增独立限制。
engine_step 停止时 false/STOPPED，无新事件；halted 与 retired 标志可同时为真。
外部 difftest_raise_intr 经引擎计数，暂停/预算耗尽时不注入；不改符号原型。
ref unit 使用显式 mem/CPU/decoder 初始化，只验证这两个 adapter 的本地行为，
不验证 dlopen/difftest_init/全量同步/store_commit，也不宣称嵌入或 RTL ABI 可用。

EBREAK 使用允许的 informative mtval=PC；非法指令保留取得的低 ILEN 位。
当前不支持 C/长指令，结果的 length 最多 4；valid 只表示这些字节已取得。
非自然对齐 I 数据访问（含 MMIO）在翻译前检查；设备内部宽度/offset 校验未补齐。
只测试当前 M 路由和 M 中断；S/U 委派、全局使能关系、完整 xRET/CSR WARL/counter
权限/抑制均未完成。本次 fault.S 实际执行 mret 恢复，但不等于完整 xRET 已合规。

CPU_state 新增 cycle 改变原生 checkpoint 头尺寸。旧布局不兼容；格式现有
header_size 检查会拒绝该布局，本轮未运行旧文件迁移/损坏恢复验收。
新格式仍不保存 SimState、事件号、平台时间或设备，CPU 不在校验覆盖内；保存/
加载在启用日志的配置明确给限制提示。完整恢复等价没有实现/验证，
不以 CPU/RAM roundtrip 代替。

Spike 差分、完整 ISA/特权一致性、Sv39/PMP、Linux、RTL consumer、sanitizer 和其它
工具链均未运行。T002 的 QEMU/Linux 工件本次未重建或重跑；RVISS 尚未启动 Linux。
旧 architecture/coding_style 中的 JIT/RTL 唯一边界及退休计时描述已注明过时。

当前检查点：T003 的限定行为 verified。下一项契约已写为
[T004/P2 最小 Spike 差分](t004_spike_minimal.md)，状态 pending。
