    .section .text.start, "ax", @progbits
    .globl _start
_start:
    j 1f
    .half 0x0000
    .word 0xdeadbeef
1:
    csrw mideleg, zero
    csrw medeleg, zero
    csrw mie, zero
    csrw mip, zero
    csrr t0, mhartid
    bnez t0, 6f
    la t0, kboot_trap
    csrw mtvec, t0
    li t0, 0x6000
    csrs mstatus, t0
.option push
.option norelax
    la gp, __global_pointer$
.option pop
    li sp, 0x800ff000
    la t0, __bss_start
    la t1, __bss_end
2:
    bgeu t0, t1, 3f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 2b
3:
    call fpioa_init
    call main
4:
    wfi
    j 4b

    .align 2
kboot_trap:
    csrci mstatus, 0x8
5:
    wfi
    j 5b
6:
    wfi
    j 6b

    .globl _init
    .globl _fini
_init:
_fini:
    ret
