#include <fpioa.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sysctl.h>
#include <uarths.h>
#include "k210_flash.h"
#include "kboot_meta_v2.h"
#include "sha256_stream.h"

#define LOG_RX_PIN 4
#define LOG_TX_PIN 5
#define LOG_BAUD 115200u
#define APP_MAGIC 0x4b323130u
#define APP_MAGIC_INV 0xb4cdcedfu
#define APP_LOAD 0x80100000u
#define APP_RAM_END 0x80600000u
#define READ_CHUNK 16384u

typedef struct __attribute__((packed)) app_header {
    uint32_t magic, magic_inv, load_addr, entry_addr;
    uint32_t image_size, image_crc32, flags, reserved;
} app_header_t;

extern void kboot_jump(uintptr_t entry);
static uint8_t s_chunk[READ_CHUNK] __attribute__((aligned(64)));

static void emit_uint(uint64_t value, unsigned base, unsigned width, int zero_pad)
{
    static const char digits[] = "0123456789abcdef";
    char buffer[32]; unsigned count = 0;
    do { buffer[count++] = digits[value % base]; value /= base; } while (value);
    while (count < width) buffer[count++] = zero_pad ? '0' : ' ';
    while (count) uarths_write_byte((uint8_t)buffer[--count]);
}

static void log_line(const char *format, ...)
{
    va_list args; va_start(args, format);
    while (*format) {
        if (*format != '%') { uarths_write_byte((uint8_t)*format++); continue; }
        ++format; int zero = 0; unsigned width = 0;
        if (*format == '0') { zero = 1; ++format; }
        while (*format >= '0' && *format <= '9') width = width * 10u + (unsigned)(*format++ - '0');
        int wide = *format == 'l'; if (wide) ++format;
        char spec = *format ? *format++ : 0;
        if (spec == 's') { const char *s = va_arg(args, const char *); uarths_puts(s ? s : "(null)"); }
        else if (spec == 'c') uarths_write_byte((uint8_t)va_arg(args, int));
        else if (spec == 'u') emit_uint(wide ? va_arg(args, unsigned long) : va_arg(args, unsigned int), 10, width, zero);
        else if (spec == 'x') emit_uint(wide ? va_arg(args, unsigned long) : va_arg(args, unsigned int), 16, width, zero);
        else if (spec == 'd') { int v = va_arg(args, int); if (v < 0) { uarths_write_byte('-'); emit_uint((uint64_t)(-(int64_t)v), 10, width, zero); } else emit_uint((unsigned)v, 10, width, zero); }
        else uarths_write_byte((uint8_t)spec);
    }
    va_end(args); uarths_puts("\r\n");
}

static uint64_t cycles(void)
{
    uint64_t value;
    __asm__ volatile("rdcycle %0" : "=r"(value));
    return value;
}

static void clock_init(void)
{
    sysctl_clock_set_threshold(SYSCTL_THRESHOLD_ACLK, 0);
    sysctl_pll_set_freq(SYSCTL_PLL0, 780000000u);
    sysctl_clock_set_clock_select(SYSCTL_CLOCK_SELECT_ACLK, SYSCTL_SOURCE_PLL0);
}

static void log_init(void)
{
    fpioa_set_function(LOG_RX_PIN, FUNC_UARTHS_RX);
    fpioa_set_function(LOG_TX_PIN, FUNC_UARTHS_TX);
    uarths_init();
    uint32_t divider = sysctl_clock_get_freq(SYSCTL_CLOCK_CPU) / LOG_BAUD;
    if (divider) --divider;
    if (divider > 0xffffu) divider = 0xffffu;
    ((volatile uarths_t *)UARTHS_BASE_ADDR)->div.div = (uint16_t)divider;
}

static void hex_digest(const uint8_t digest[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 15u];
    }
    out[64] = 0;
}

static int header_valid(const app_header_t *h, uint32_t expected_size)
{
    if (h->magic != APP_MAGIC || h->magic_inv != APP_MAGIC_INV ||
        h->load_addr != APP_LOAD || h->image_size < sizeof(*h) ||
        h->image_size > KBOOT_SLOT_BYTES || h->load_addr + h->image_size > APP_RAM_END)
        return 0;
    if (h->entry_addr < APP_LOAD || h->entry_addr >= h->load_addr + h->image_size)
        return 0;
    return expected_size == 0u || h->image_size == expected_size;
}

static int load_and_verify(uint8_t slot, const kboot_meta_v2_t *meta, int factory)
{
    const uint32_t flash = slot ? KBOOT_SLOT_B_OFFSET : KBOOT_SLOT_A_OFFSET;
    app_header_t h;
    int rc = k210_flash_read(flash, &h, sizeof(h));
    if (rc || !header_valid(&h, factory ? 0u : meta->image_size[slot])) return -10 + rc;

    sha256_stream_t sha;
    sha256_stream_init(&sha);
    uint64_t start = cycles();
    for (uint32_t offset = 0; offset < h.image_size;) {
        uint32_t count = h.image_size - offset;
        if (count > sizeof(s_chunk)) count = sizeof(s_chunk);
        rc = k210_flash_read(flash + offset, s_chunk, count);
        if (rc) return -20 + rc;
        memcpy((void *)(uintptr_t)(APP_LOAD + offset), s_chunk, count);
        sha256_stream_update(&sha, s_chunk, count);
        offset += count;
    }
    uint8_t digest[32];
    char digest_hex[65];
    sha256_stream_final(&sha, digest);
    hex_digest(digest, digest_hex);
    uint64_t elapsed_cycles = cycles() - start;
    uint64_t elapsed_ms = elapsed_cycles / 390000u;
    if (!elapsed_ms) elapsed_ms = 1;
    log_line("KBOOT:LOAD slot=%c bytes=%lu ms=%lu KiB/s=%lu sha256=%s",
           slot ? 'B' : 'A', (unsigned long)h.image_size, (unsigned long)elapsed_ms,
           (unsigned long)(((uint64_t)h.image_size * 1000u) / elapsed_ms / 1024u), digest_hex);
    if (!factory && memcmp(digest, meta->image_sha256[slot], sizeof(digest))) return -30;
    if (factory) log_line("KBOOT:FACTORY_SLOT_A_NO_METADATA");
    log_line("KBOOT:VERIFY_OK slot=%c entry=0x%08lx", slot ? 'B' : 'A',
           (unsigned long)h.entry_addr);
    kboot_jump(h.entry_addr);
    return -40;
}

int main(void)
{
    clock_init();
    log_init();
    log_line("KBOOT:V2_START sd=absent");

    kboot_meta_v2_t meta;
    int meta_rc = kboot_meta_v2_load(&meta);
    if (meta_rc < 0) {
        log_line("KBOOT:HALT metadata_read rc=%d", meta_rc);
        for (;;) __asm__ volatile("wfi");
    }

    uint8_t slot;
    int factory = meta_rc == 1;
    if (factory) {
        slot = 0;
    } else if (meta.pending_slot != KBOOT_SLOT_NONE) {
        if (meta.boot_attempts == 0u) {
            slot = meta.pending_slot;
            meta.active_slot = slot;
            meta.boot_attempts = 1u;
            int rc = kboot_meta_v2_append(&meta);
            if (rc) {
                log_line("KBOOT:HALT arm_pending rc=%d", rc);
                for (;;) __asm__ volatile("wfi");
            }
            log_line("KBOOT:PENDING_ONCE slot=%c generation=%lu",
                   slot ? 'B' : 'A', (unsigned long)meta.generation);
        } else {
            slot = meta.confirmed_slot;
            meta.active_slot = slot;
            meta.pending_slot = KBOOT_SLOT_NONE;
            meta.boot_attempts = 0u;
            int rc = kboot_meta_v2_append(&meta);
            if (rc) {
                log_line("KBOOT:HALT rollback_record rc=%d", rc);
                for (;;) __asm__ volatile("wfi");
            }
            log_line("KBOOT:ROLLBACK slot=%c generation=%lu",
                   slot ? 'B' : 'A', (unsigned long)meta.generation);
        }
    } else {
        slot = meta.confirmed_slot;
        if (meta.active_slot != slot) {
            log_line("KBOOT:HALT active_not_confirmed active=%u confirmed=%u",
                   meta.active_slot, meta.confirmed_slot);
            for (;;) __asm__ volatile("wfi");
        }
    }

    int rc = load_and_verify(slot, &meta, factory);
    log_line("KBOOT:HALT load_verify rc=%d slot=%c", rc, slot ? 'B' : 'A');
    for (;;) __asm__ volatile("wfi");
}
