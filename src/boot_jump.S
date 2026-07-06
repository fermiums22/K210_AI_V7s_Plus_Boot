    .section .text.boot_jump, "ax", @progbits
    .globl boot_jump_to_app
    .type boot_jump_to_app, @function

/* void boot_jump_to_app(uintptr_t entry)
 * a0 = entry address
 * Disable interrupts, set mtvec to entry for a clean trap base until app _start
 * overwrites it, clear ra/sp to avoid accidental return, jump.
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
