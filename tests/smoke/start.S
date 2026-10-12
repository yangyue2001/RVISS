# 冒烟测试程序：UART 打印一行字，然后 nemu_trap 正常退出。
# 覆盖：lui/auipc/addi、lbu/sb（MMIO）、beqz、jal、nemu_trap

    .section .text.start, "ax"
    .globl _start
_start:
    li   t0, 0x10000000      # UART THR 地址
    la   t1, msg

1:  lbu  t2, 0(t1)
    beqz t2, 2f
    sb   t2, 0(t0)           # 写 UART → 宿主 stdout
    addi t1, t1, 1
    j    1b

2:  li   a0, 0               # 退出码 0 = HIT GOOD TRAP
    .word 0x0005006b         # nemu_trap（rs1 位域 = a0）

    .section .rodata
msg:
    .asciz "Hello from RVISS!\n"
