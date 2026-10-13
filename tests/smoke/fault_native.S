    .option norvc
    .section .text.start, "ax"
    .globl _start
_start:
    la t1, datum
    ld t2, 0(t1)
    ld t3, 1(t1)
    li t4, 0x0011223344556677
    bne t3, t4, fail
    sd t2, 1(t1)
    ld t3, 1(t1)
    bne t3, t2, fail
    li a0, 0
    .word 0x0005006b
fail:
    li a0, 1
    .word 0x0005006b

    .section .data
    .align 3
datum:
    .dword 0x1122334455667788
