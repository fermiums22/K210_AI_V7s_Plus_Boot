    .section .text.boot_jump, "ax", @progbits
    .globl boot_irq_off
    .type boot_irq_off, @function
    .globl boot_jump_to_app
    .type boot_jump_to_app, @function

boot_irq_off:
    csrci mstatus, 0x8
    csrw mie, zero
    csrw mip, zero
    ret

/* void boot_jump_to_app(uintptr_t entry)
 * a0 = entry address
 */
boot_jump_to_app:
    csrci mstatus, 0x8
    csrw mie, zero
    csrw mip, zero
    csrw mtvec, a0
    mv t0, a0
    li ra, 0
    li sp, 0
    jr t0
