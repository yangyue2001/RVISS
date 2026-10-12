# 中断端到端测试：CLINT 定时器中断 → 陷阱 → 处理 → mret 返回 → 退出
#
# 覆盖的链路（都是"设备/ISA 解耦"之后的新路径）：
#   CLINT mtime 追上 mtimecmp → 拉高 mtimer 输出线 → cpu_irq_input(MTIP)
#   → mip.MTIP → riscv_query_intr → riscv_raise_trap → mtvec → handler → mret
#
# 期望：handler 恰好进入 1 次，然后以 a0=0 退出（HIT GOOD TRAP）

    .section .text.start, "ax"
    .globl _start
_start:
    la   t0, handler
    csrw mtvec, t0          # 陷阱入口
    li   t0, 0x80           # mie.MTIE (bit 7)
    csrw mie, t0
    li   t0, 0x8            # mstatus.MIE (bit 3)
    csrw mstatus, t0

    li   t0, 0x02004000     # CLINT mtimecmp
    li   t1, 8              # 很小 → mtime 很快追上（每 128 条指令 +1）
    sd   t1, 0(t0)

    li   a1, 0              # 中断计数

1:  j    1b                  # 等中断

    .align 2
handler:
    addi a1, a1, 1          # 记录进入次数
    li   t0, 0x02004000
    li   t1, 0x7fffffff     # 抬高 mtimecmp，抑制后续定时器中断
    sd   t1, 0(t0)

    li   t2, 1
    bne  a1, t2, 2f         # 只期望进入一次
    li   a0, 0              # 成功
    .word 0x0005006b        # nemu_trap（退出码 = a0）

2:  mret                    # 被多次进入时不退出，用 -n 上限兜底（run.sh 会判定失败）
