# 同步异常端到端：每个故障的 cause/epc/tval、写回抑制及恢复路径。
    .option norvc
    .section .text.start, "ax"
    .globl _start
_start:
    li t0, 4
    csrc misa, t0             # 本用例固定 IALIGN=32，保留原 12 个 fault 断言
    la t0, handler
    csrw mtvec, t0
    li s0, 0                  # 已处理异常数
    li a5, 0x1234             # 失败 load/CSR 的写回哨兵
    li t6, 0x40000000         # 未映射地址

    li s1, 5
    mv s3, t6
    la s2, load_fault
load_fault:
    ld a5, 0(t6)
    li t0, 0x1234
    bne a5, t0, fail
    la s2, load_zero_fault
load_zero_fault:
    ld zero, 0(t6)

    li s1, 7
    la s2, store_fault
store_fault:
    sd a5, 0(t6)

    la t6, datum
    sd a5, 0(t6)              # 成功 store/load 的真实路径
    ld t0, 0(t6)
    bne t0, a5, fail
    li s1, 4
    addi s3, t6, 1
    la s2, load_unaligned
load_unaligned:
    ld a5, 1(t6)
    li t0, 0x1234
    bne a5, t0, fail
    li s1, 6
    la s2, store_unaligned
store_unaligned:
    sd a5, 1(t6)
    ld t0, 0(t6)
    bne t0, a5, fail          # 失败 store 没有改 RAM

    li s1, 11
    li s3, 0
    la s2, ecall_fault
ecall_fault:
    ecall
    li s1, 3
    la s2, breakpoint
    mv s3, s2
breakpoint:
    ebreak
    li s1, 2
    li s3, 0xffffffff
    la s2, illegal
illegal:
    .word 0xffffffff
    la s2, bad_csr
    li s3, 0xfff797f3         # csrrw a5,0xfff,a5
bad_csr:
    .word 0xfff797f3
    li t0, 0x1234
    bne a5, t0, fail

    li s1, 0
    la s2, bad_jal
    addi s3, s2, 2
    li t5, 0x5678
bad_jal:
    .word 0x00200f6f         # jal t5,+2：不能写链接寄存器
    li t0, 0x5678
    bne t5, t0, fail
    la s2, bad_jalr
    addi s3, s2, 2
    addi t5, s2, 3
bad_jalr:
    jalr t5, 0(t5)          # alias + bit0 清零后仍错对齐
    addi t0, s2, 3
    bne t5, t0, fail
    la s2, bad_branch
    addi s3, s2, 2
bad_branch:
    .word 0x00000163         # beq zero,zero,+2
    .word 0x00001163         # bne zero,zero,+2 未取：无异常
    li t0, 12
    bne s0, t0, fail
    li a0, 0
    .word 0x0005006b

    .align 2
handler:
    csrr t0, mcause
    bne t0, s1, fail
    csrr t0, mepc
    bne t0, s2, fail
    csrr t1, mtval
    bne t1, s3, fail
    addi s0, s0, 1
    addi t0, t0, 4
    csrw mepc, t0
    mret
fail:
    li a0, 1
    .word 0x0005006b

    .section .data
    .align 3
datum:
    .dword 0
