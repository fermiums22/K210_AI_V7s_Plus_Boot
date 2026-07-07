#include "boot_cmd.h"
#include "boot_flash.h"
#include "log.h"
#include "sd.h"

#include <FreeRTOS.h>
#include <task.h>
#include <filesystem.h>
#include <ff.h>
#include <platform.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysctl.h>
#include <uarths.h>

#define BOOT_CMD_BUF                 512u
#define BOOT_CMD_LINE                192u
#define BOOT_CMD_TIMEOUT_MS          5000u
#define BOOT_CMD_READY_PERIOD_MS     1000u
#define BOOT_SPI3_SCRATCH_OFFSET     0x00F00000u
#define BOOT_SPI3_SCRATCH_SIZE       256u

#define UARTHS_RXDATA_EMPTY_MASK     (1u << 31)

/* These command-mode write helpers are implemented in boot_spi3_rw.c.  Keep
 * them out of boot_flash_read(), because the normal boot image read path uses
 * quad-DMA plus 32-bit byte swapping and is not suitable for byte verify. */
int boot_flash_read_raw(uint32_t flash_offset, void *dst, uint32_t len);
int boot_flash_sector_4k(uint32_t flash_offset);
int boot_flash_program_page(uint32_t flash_offset, const void *src, uint32_t len);

static volatile uarths_t *const REG_UARTHS = (volatile uarths_t *)UARTHS_BASE_ADDR;
static uint8_t s_buf[BOOT_CMD_BUF] __attribute__((aligned(64)));
static uint8_t s_verify[BOOT_CMD_BUF] __attribute__((aligned(64)));

static TickType_t ms_to_ticks_min(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    return ticks ? ticks : 1;
}

static int deadline_expired(TickType_t start, uint32_t timeout_ms)
{
    return (xTaskGetTickCount() - start) >= ms_to_ticks_min(timeout_ms);
}

static void host_puts(const char *s)
{
    uarths_puts(s);
}

static int uarths_try_read_byte(uint8_t *out)
{
    uint32_t raw = *(volatile uint32_t *)&REG_UARTHS->rxdata;
    if (raw & UARTHS_RXDATA_EMPTY_MASK)
        return 0;
    *out = (uint8_t)(raw & 0xffu);
    return 1;
}

static int read_byte_timeout(uint8_t *out, uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    while (!deadline_expired(start, timeout_ms)) {
        if (uarths_try_read_byte(out))
            return 1;
        taskYIELD();
    }
    return 0;
}

static int read_line(char *out, int out_len, uint32_t timeout_ms)
{
    int n = 0;
    uint8_t c;
    while (n < out_len - 1) {
        if (!read_byte_timeout(&c, timeout_ms)) {
            out[n] = 0;
            LOGF("BOOT_CMD_RX_TIMEOUT partial=%s", out);
            return 0;
        }
        if (c == '\n') {
            out[n] = 0;
            return 1;
        }
        if (c != '\r')
            out[n++] = (char)c;
    }
    out[n] = 0;
    LOGF("BOOT_CMD_RX_TOO_LONG %s", out);
    return 0;
}

static void drain_rx(uint32_t quiet_ms)
{
    TickType_t quiet_start = xTaskGetTickCount();
    uint8_t c;
    while (!deadline_expired(quiet_start, quiet_ms)) {
        if (uarths_try_read_byte(&c)) {
            quiet_start = xTaskGetTickCount();
            continue;
        }
        vTaskDelay(ms_to_ticks_min(1));
    }
}

static int wait_magic(uint32_t window_ms)
{
    const char *m1 = "KSD1";
    const char *m2 = "KBOOT1";
    int i1 = 0;
    int i2 = 0;
    TickType_t start = xTaskGetTickCount();
    TickType_t last_ready = start - ms_to_ticks_min(BOOT_CMD_READY_PERIOD_MS);
    uint8_t c;

    while (!deadline_expired(start, window_ms)) {
        if (!uarths_try_read_byte(&c)) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_ready) >= ms_to_ticks_min(BOOT_CMD_READY_PERIOD_MS)) {
                host_puts("KBOOT:READY\n");
                host_puts("KSD:READY\n");
                last_ready = now;
            }
            vTaskDelay(ms_to_ticks_min(5));
            continue;
        }

        if (c == (uint8_t)m1[i1]) {
            i1++;
            if (m1[i1] == 0)
                return 1;
        } else {
            i1 = (c == (uint8_t)m1[0]) ? 1 : 0;
        }

        if (c == (uint8_t)m2[i2]) {
            i2++;
            if (m2[i2] == 0)
                return 1;
        } else {
            i2 = (c == (uint8_t)m2[0]) ? 1 : 0;
        }
    }
    return 0;
}

static bool sd_rw_probe(void)
{
    if (!sd_mount())
        return false;

    const char *fs_path = "/fs/0/boot_selftest.bin";
    const char *fat_path = "0:/boot_selftest.bin";
    uint8_t wr[64];
    uint8_t rd[64];

    for (uint32_t i = 0; i < sizeof(wr); i++)
        wr[i] = (uint8_t)((i * 37u + 11u) & 0xffu);
    memset(rd, 0, sizeof(rd));

    f_unlink(fat_path);
    handle_t f = filesystem_file_open(fs_path, FILE_ACCESS_WRITE, FILE_MODE_CREATE_ALWAYS);
    if (!f)
        return false;
    int n = filesystem_file_write(f, wr, sizeof(wr));
    filesystem_file_close(f);
    if (n != (int)sizeof(wr))
        return false;

    f = filesystem_file_open(fs_path, FILE_ACCESS_READ, FILE_MODE_OPEN_EXISTING);
    if (!f)
        return false;
    n = filesystem_file_read(f, rd, sizeof(rd));
    filesystem_file_close(f);
    if (n != (int)sizeof(rd))
        return false;

    return memcmp(wr, rd, sizeof(wr)) == 0;
}

static bool spi3_rw_probe(uint32_t offset, char *detail, size_t detail_size)
{
    if ((offset & 0xfffu) != 0) {
        snprintf(detail, detail_size, "bad-align-0x%08lx", (unsigned long)offset);
        return false;
    }

    for (uint32_t i = 0; i < BOOT_SPI3_SCRATCH_SIZE; i++)
        s_buf[i] = (uint8_t)((i * 17u + 0x5au) & 0xffu);
    memset(s_verify, 0, BOOT_SPI3_SCRATCH_SIZE);

    int rc = boot_flash_sector_4k(offset);
    if (rc != 0) {
        snprintf(detail, detail_size, "erase-rc=%d", rc);
        return false;
    }

    rc = boot_flash_program_page(offset, s_buf, BOOT_SPI3_SCRATCH_SIZE);
    if (rc != 0) {
        snprintf(detail, detail_size, "prog-rc=%d", rc);
        return false;
    }

    rc = boot_flash_read_raw(offset, s_verify, BOOT_SPI3_SCRATCH_SIZE);
    if (rc != 0) {
        snprintf(detail, detail_size, "read-rc=%d", rc);
        return false;
    }

    if (memcmp(s_buf, s_verify, BOOT_SPI3_SCRATCH_SIZE) != 0) {
        uint32_t bad = 0;
        while (bad < BOOT_SPI3_SCRATCH_SIZE && s_buf[bad] == s_verify[bad])
            bad++;
        snprintf(detail, detail_size, "verify-bad-%lu-wr=%02x-rd=%02x",
                 (unsigned long)bad, s_buf[bad], s_verify[bad]);
        return false;
    }

    snprintf(detail, detail_size, "offset=0x%08lx size=%lu",
             (unsigned long)offset, (unsigned long)BOOT_SPI3_SCRATCH_SIZE);
    return true;
}

static void hex_dump_line(const uint8_t *data, uint32_t len)
{
    char line[192];
    uint32_t pos = 0;
    pos += snprintf(line + pos, sizeof(line) - pos, "KBOOT:SPI3_DATA");
    for (uint32_t i = 0; i < len && pos + 4 < sizeof(line); i++)
        pos += snprintf(line + pos, sizeof(line) - pos, " %02x", data[i]);
    snprintf(line + pos, sizeof(line) - pos, "\n");
    host_puts(line);
}

static void help_command(void)
{
    host_puts("KBOOT:HELP HELP SELFTEST SD_TEST SPI3_ID SPI3_READ <off> <len> SPI3_RW [off] RESET DONE\n");
    host_puts("KBOOT:HELP scratch-default=0x00F00000 rw-erases-4k-sector\n");
    host_puts("KBOOT:HELP_END\n");
}

static void sd_test_command(void)
{
    LOG("BOOT_CMD SD_TEST");
    host_puts(sd_rw_probe() ? "KBOOT:SD_OK rw-64\n" : "KBOOT:SD_FAIL rw\n");
}

static void spi3_id_command(void)
{
    uint32_t jedec = boot_flash_read_jedec_id();
    char line[64];
    snprintf(line, sizeof(line), "KBOOT:SPI3_ID 0x%06lx\n", (unsigned long)jedec);
    host_puts(line);
}

static void spi3_read_command(const char *line)
{
    unsigned long off = 0;
    unsigned long len = 0;
    if (sscanf(line, "SPI3_READ %lx %lu", &off, &len) != 2 &&
        sscanf(line, "SPI3_READ 0x%lx %lu", &off, &len) != 2) {
        host_puts("KBOOT:ERR args\n");
        return;
    }
    if (len == 0 || len > 64u || (len & 3u) != 0) {
        host_puts("KBOOT:ERR len-use-4..64-multiple-of-4\n");
        return;
    }
    memset(s_buf, 0, len);
    int rc = boot_flash_read((uint32_t)off, s_buf, (uint32_t)len);
    if (rc != 0) {
        char out[64];
        snprintf(out, sizeof(out), "KBOOT:SPI3_READ_FAIL rc=%d\n", rc);
        host_puts(out);
        return;
    }
    hex_dump_line(s_buf, (uint32_t)len);
    host_puts("KBOOT:SPI3_READ_OK\n");
}

static void spi3_rw_command(const char *line)
{
    unsigned long off = BOOT_SPI3_SCRATCH_OFFSET;
    char detail[96];
    (void)sscanf(line, "SPI3_RW %lx", &off);
    detail[0] = 0;
    if (spi3_rw_probe((uint32_t)off, detail, sizeof(detail))) {
        char out[160];
        snprintf(out, sizeof(out), "KBOOT:SPI3_RW_OK %s\n", detail);
        host_puts(out);
    } else {
        char out[160];
        snprintf(out, sizeof(out), "KBOOT:SPI3_RW_FAIL %s\n", detail);
        host_puts(out);
    }
}

static void selftest_command(void)
{
    char detail[96];
    uint32_t jedec = boot_flash_read_jedec_id();
    host_puts("KBOOT:TEST_BEGIN\n");
    host_puts("KBOOT:TEST CMD PASS command-loop\n");
    if (sd_rw_probe())
        host_puts("KBOOT:TEST SD_RW PASS 64-bytes\n");
    else
        host_puts("KBOOT:TEST SD_RW FAIL rw\n");
    {
        char out[80];
        snprintf(out, sizeof(out), "KBOOT:TEST SPI3_ID %s 0x%06lx\n",
                 jedec == 0xffffffffu ? "FAIL" : "PASS", (unsigned long)jedec);
        host_puts(out);
    }
    if (spi3_rw_probe(BOOT_SPI3_SCRATCH_OFFSET, detail, sizeof(detail))) {
        char out[160];
        snprintf(out, sizeof(out), "KBOOT:TEST SPI3_RW PASS %s\n", detail);
        host_puts(out);
    } else {
        char out[160];
        snprintf(out, sizeof(out), "KBOOT:TEST SPI3_RW FAIL %s\n", detail);
        host_puts(out);
    }
    host_puts("KBOOT:TEST_END\n");
}

static void reset_command(void)
{
    host_puts("KBOOT:RESETTING\n");
    vTaskDelay(ms_to_ticks_min(100));
    sysctl_reset(SYSCTL_RESET_SOC);
    for (;;)
        vTaskDelay(ms_to_ticks_min(1000));
}

static void command_loop(void)
{
    for (;;) {
        char line[BOOT_CMD_LINE];
        host_puts("KBOOT:CMD\n");
        if (!read_line(line, sizeof(line), BOOT_CMD_TIMEOUT_MS)) {
            host_puts("KBOOT:TIMEOUT\n");
            return;
        }

        LOGF("BOOT_CMD_RX %s", line);
        if (strcmp(line, "DONE") == 0) { host_puts("KBOOT:DONE\n"); return; }
        if (strcmp(line, "HELP") == 0) { help_command(); continue; }
        if (strcmp(line, "SELFTEST") == 0) { selftest_command(); continue; }
        if (strcmp(line, "SD_TEST") == 0) { sd_test_command(); continue; }
        if (strcmp(line, "SPI3_ID") == 0) { spi3_id_command(); continue; }
        if (strncmp(line, "SPI3_READ", 9) == 0) { spi3_read_command(line); continue; }
        if (strncmp(line, "SPI3_RW", 7) == 0) { spi3_rw_command(line); continue; }
        if (strcmp(line, "RESET") == 0) { reset_command(); }

        host_puts("KBOOT:ERR command\n");
    }
}

void boot_cmd_service_run(uint32_t reason)
{
    LOGF("BOOT_CMD_SERVICE reason=0x%08lx", (unsigned long)reason);
    host_puts("KBOOT:BOOTMODE\n");
    host_puts("KBOOT:USE KSD1 or KBOOT1 then HELP\n");

    for (;;) {
        if (!wait_magic(1000))
            continue;
        drain_rx(20);
        host_puts("KBOOT:HELLO\n");
        LOG("BOOT_CMD_CONNECTED");
        command_loop();
    }
}
