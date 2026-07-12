    .section .text.kboot_jump, "ax", @progbits
    .globl kboot_jump
    .type kboot_jump, @function
kboot_jump:
    csrci mstatus, 0x8
    csrw mie, zero
    csrw mip, zero
    mv t0, a0
    fence
    fence.i
    li ra, 0
    li sp, 0
    jr t0
