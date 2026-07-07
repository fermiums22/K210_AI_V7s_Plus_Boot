    .section .text.boot_jump, "ax", @progbits
    .globl boot_irq_off
    .type boot_irq_off, @function
    .globl boot_jump_to_app
    .type boot_jump_to_app, @function
    .globl boot_flash_load_app_image_on_safe_stack
    .type boot_flash_load_app_image_on_safe_stack, @function

boot_irq_off:
    csrci mstatus, 0x8
    csrw mie, zero
    csrw mip, zero
    ret

/* int boot_flash_load_app_image_on_safe_stack(const boot_app_header_t *hdr)
 * a0 = hdr
 *
 * The ROM/SDK startup stack can live above 0x80100000.  Slot0 app is also
 * loaded at 0x80100000, so a large copy can overwrite the current boot stack
 * before boot_jump_to_app() is reached.  Switch to a small temporary stack just
 * below APP_LOAD_ADDR before copying the app image into RAM.
 */
boot_flash_load_app_image_on_safe_stack:
    li sp, 0x800ff000
    addi sp, sp, -32
    sd ra, 24(sp)
    sd s0, 16(sp)
    sd s1, 8(sp)
    sd s2, 0(sp)
    call boot_flash_load_app_image
    ld s2, 0(sp)
    ld s1, 8(sp)
    ld s0, 16(sp)
    ld ra, 24(sp)
    addi sp, sp, 32
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

    /* The app image has just been copied into executable SRAM.
     * Make data writes visible to instruction fetch before jumping. */
    fence
    fence.i

    li ra, 0
    li sp, 0
    jr t0
