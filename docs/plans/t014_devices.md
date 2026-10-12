# T014：固定 virt 设备与 OpenSBI 实际执行

状态：verified（2026-10-12），依赖 T013 verified（d319684）。

目标：固定 T002 FW_PAYLOAD 在原始 RAM/入口/DTB 不变的条件下执行，提供
CLINT 32/64 位访问、UART DLAB/FIFO/中断/真实宿主输入、PLIC 两个 M/S
context/source10/仲裁/claim/complete 和 test finisher。C 核心；设备不知道
CPU CSR 或板级地址。IRQ 接线、宿主输入与 shutdown 由板级负责。

规则：MMIO 在副作用前检查声明的 offset/width；UART 字节寄存器，16字节
FIFO、四档接收阈值，确定性接收 timeout；即时 TX，THRE/TEMT 与 IRQ 语义。
PLIC sources1..31，priority/threshold低3位 WARL，ID0不可用，level gateway
最多一个未完成请求；claim 忽略 threshold、按最高 priority/最小ID选择 enabled
pending，完成仅接受该 context 当前 enabled ID，level仍高时重新挂起。
CLINT msip32、mtime/mtimecmp64及两个32位半寄存器，time每128逻辑ticks+1。
平台 idle 时间快进如被真实启动性能阻碍而需要，另立检查点验证，保持事件、
退休、客户机 counters 与逻辑时间分离；不用墙钟驱动客户机 timer。

规范依据：Priv1.12 mtime；官方 PLIC spec（固定本次读取内容哈希）；
TI PC16550D 寄存器定义。QEMU 本地 hw/intc/sifive_plic.c、riscv_aclint.c、
hw/char/serial.c 和 hw/misc/sifive_test.c 仅实现参考。采用固定 DTB map。

验收：设备实例 unit 覆盖宽度/偏移无副作用、半寄存器、DLAB、FIFO/超时、
IRQ读清与claim/complete、优先级/tie/threshold、多context竞争与反复level；
真实指令经过集中 MMIO 路径的异常/IRQ/finisher；GC/minimal make check、
UBSan设备unit，保留原核心Spike差分。固件运行保存预算、日志、首阻塞状态。
阶段通过则提交，继续 T015 固定 Linux PID1/BusyBox/100/1000轮验收。
不改变固定工件，不宣称完整 checkpoint/RTL ABI，不添加虚拟 SBI 快捷执行。

检查点：首次100M预算在Linux serial_putc等待LSR.TEMT自旋，旧UART仅THRE；
修复后相同工件实际进入PID1及用户态probe（全部标记），300M预算仍未结束
初始workload10。120s WFI快进试跑完成workload10、BusyBox与串口命令，但
额外workload1超时，尚未通过验收。记录不能作为稳定启动完成。

已引入 legal WFI wait_request 和设备deadline；只在没有任何 individually
enabled IRQ时快进，global MIE/SIE不参与WFI恢复条件。快进仅影响逻辑ticks
与设备mtime，独立记录idle_ticks/idle_advances，不增加尝试/退休/counters。
CLINT、UART deadline callback独立，未注册则保留NOP。真实输入显式
--uart-stdin仅batch，宿主poll不驱动客户机时钟，FIFO容量提供backpressure。
后续需补充输入事件重放、设备unit与实际整链路证据，再提交。

PLIC 规范固定 blob a850f76c96457d28fef115e6617dd1e34db5e11c，SHA256
7209e2d8b8266a642954d90a71a661146327c620c4d2ac959c27aa338ae1176a；
TI SNLS378C RevC 2015 §8.4/8.6 寄存器与 IRQ 规则。功能 UART 即时TX，
不模拟电气波形/真实波特率 TX延迟；RX timeout按divisor/LCR与逻辑timebase。

设备与输入 unit 476+32 checks，通过真实 S 外部中断、宽RBR fault无消费、
WFI计数分离、TW fault、真实SW finisher退休与haltPC。GC/minimal17 unit、
3 smoke、CLI14通过；UBSan设备476/输入32无诊断。GC固定Spike8949/74887、
minimal2203/5255重放/差异注入通过。

第一完整端到端1轮实际验收：PID1、初始workload10、BusyBox真实串口命令、
额外workload1、SBI正常关机全通过，134.785s，203680985次尝试，
203681407events，1534894212逻辑ticks；106次idle推进1331212805ticks。
--uart-record/replay按执行event序号精确记录/重放字节，迟到/容量不符报宿主
错误，不静默移动输入时序。完整重放还在执行，验收报告留作独立证据。

实际性能调查（-pg，同一固件10M尝试）PMP检查占52.72%样本，译码7.74%。
长跑的指令量显著，后续仅在该热点做有独立回归的局部改进，不添加TLB或
改变页表/权限规则。源输入、期望标记、workload轮数不变。

最终完整输入重放通过：124.176s，全部guest计数与原记录相同；去除宿主
performance行后的完整输出相同。证据 t014_linux_{replay_,}1.json、
t014_replay_equivalence.json、t014_checks.json。设备完整归档报告含实际命令/
身份/边界。未实现 test7777 reboot，未宣称full16550电气/奇偶/框架错误注入。
T014已verified并待提交，继续 T015性能检查点与相同固定Linux100/1000验收。
