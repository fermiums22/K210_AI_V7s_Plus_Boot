#include "boot_cmd.h"
#include "boot_flash.h"
#include "log.h"
#include "sd.h"

#include <FreeRTOS.h>
#include <task.h>
#include <filesystem.h>
#include <ff.h>
#include <platform.h>
#include <stdarg.h>
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
#define BOOT_APP_SLOT0_OFFSET        0x00100000u
#define BOOT_SD_DEFAULT_PATH         "0:/app_slot0.bin"
#define BOOT_SD_DEFAULT_READ_LEN     256u
#define BOOT_SD_MAX_READ_LEN         512u
#define BOOT_SD_FLASH_PROGRESS_STEP  65536u

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

static uint32_t elapsed_ms(TickType_t start)
{
    return (uint32_t)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS);
}

static unsigned long speed_kib_s(uint32_t bytes, uint32_t ms)
{
    if (!ms)
        return 0;
    return (unsigned long)(((bytes / 1024u) * 1000u) / ms);
}

static void host_puts(const char *s)
{
    uarths_puts(s);
}

static void host_printf(const char *fmt, ...)
{
    char line[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    host_puts(line);
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

static void hex_dump_line_prefixed(const char *prefix, const uint8_t *data, uint32_t len)
{
    char line[192];
    uint32_t pos = 0;
    pos += snprintf(line + pos, sizeof(line) - pos, "%s", prefix);
    for (uint32_t i = 0; i < len && pos + 4 < sizeof(line); i++)
        pos += snprintf(line + pos, sizeof(line) - pos, " %02x", data[i]);
    snprintf(line + pos, sizeof(line) - pos, "\n");
    host_puts(line);
}

static void hex_dump_line(const uint8_t *data, uint32_t len)
{
    hex_dump_line_prefixed("KBOOT:SPI3_DATA", data, len);
}

static void sd_normalize_path(const char *arg, char *out, size_t out_size)
{
    const char *path = (arg && arg[0]) ? arg : BOOT_SD_DEFAULT_PATH;

    if (strncmp(path, "/fs/", 4) == 0) {
        snprintf(out, out_size, "%s", path);
    } else if (strncmp(path, "0:/", 3) == 0) {
        snprintf(out, out_size, "/fs/%s", path + 3);
    } else if (path[0] == '/') {
        snprintf(out, out_size, "/fs%s", path);
    } else {
        snprintf(out, out_size, "/fs/%s", path);
    }
}

static bool sd_mount_debug(void)
{
    LOG("BOOT_SD_MOUNT_BEGIN");
    host_puts("KBOOT:SD_MOUNT_BEGIN\n");
    host_puts("KBOOT:SD_LOWLEVEL_VIA sd_mount\n");

    bool ok = sd_mount();

    LOGF("BOOT_SD_MOUNT_RESULT ok=%u", ok ? 1u : 0u);
    host_printf("KBOOT:SD_MOUNT_RESULT ok=%u\n", ok ? 1u : 0u);
    return ok;
}

static void sd_mount_command(void)
{
    LOG("BOOT_CMD SD_MOUNT");
    host_puts("KBOOT:SD_DEBUG_BEGIN op=mount\n");
    (void)sd_mount_debug();
    host_puts("KBOOT:SD_DEBUG_END op=mount\n");
}

static void sd_read_command(const char *line)
{
    char raw_path[128] = BOOT_SD_DEFAULT_PATH;
    char fs_path[160];
    unsigned long len = BOOT_SD_DEFAULT_READ_LEN;

    int narg = sscanf(line, "SD_READ %127s %lu", raw_path, &len);
    if (narg < 1) {
        snprintf(raw_path, sizeof(raw_path), "%s", BOOT_SD_DEFAULT_PATH);
        len = BOOT_SD_DEFAULT_READ_LEN;
    } else if (narg < 2) {
        len = BOOT_SD_DEFAULT_READ_LEN;
    }

    if (len == 0 || len > BOOT_SD_MAX_READ_LEN) {
        host_printf("KBOOT:SD_READ_FAIL bad-len=%lu max=%lu\n",
                    len, (unsigned long)BOOT_SD_MAX_READ_LEN);
        return;
    }

    sd_normalize_path(raw_path, fs_path, sizeof(fs_path));

    LOGF("BOOT_CMD SD_READ path=%s len=%lu", fs_path, len);
    host_printf("KBOOT:SD_DEBUG_BEGIN op=read file=%s len=%lu\n",
                raw_path, len);

    if (!sd_mount_debug()) {
        host_puts("KBOOT:SD_READ_FAIL mount\n");
        host_puts("KBOOT:SD_DEBUG_END op=read\n");
        return;
    }

    host_printf("KBOOT:SD_OPEN_BEGIN path=%s\n", fs_path);
    handle_t f = filesystem_file_open(fs_path, FILE_ACCESS_READ, FILE_MODE_OPEN_EXISTING);
    if (!f) {
        LOGF("BOOT_SD_OPEN_FAIL path=%s", fs_path);
        host_printf("KBOOT:SD_OPEN_RESULT ok=0 path=%s\n", fs_path);
        host_puts("KBOOT:SD_READ_FAIL open\n");
        host_puts("KBOOT:SD_DEBUG_END op=read\n");
        return;
    }

    host_printf("KBOOT:SD_OPEN_RESULT ok=1 path=%s\n", fs_path);
    memset(s_buf, 0, len);

    host_printf("KBOOT:SD_READ_BEGIN len=%lu\n", len);
    int got = filesystem_file_read(f, s_buf, (int)len);
    filesystem_file_close(f);

    host_printf("KBOOT:SD_READ_RESULT requested=%lu got=%d\n", len, got);
    if (got <= 0) {
        host_puts("KBOOT:SD_READ_FAIL read\n");
        host_puts("KBOOT:SD_DEBUG_END op=read\n");
        return;
    }

    hex_dump_line_prefixed("KBOOT:SD_DATA", s_buf, (uint32_t)got);
    host_puts("KBOOT:SD_READ_OK\n");
    host_puts("KBOOT:SD_DEBUG_END op=read\n");
}

static bool sd_rw_probe(void)
{
    if (!sd_mount_debug())
        return false;

    const char *fs_path = "/fs/0/boot_selftest.bin";
    const char *fat_path = "0:/boot_selftest.bin";
    uint8_t wr[64];
    uint8_t rd[64];

    for (uint32_t i = 0; i < sizeof(wr); i++)
        wr[i] = (uint8_t)((i * 37u + 11u) & 0xffu);
    memset(rd, 0, sizeof(rd));

    host_printf("KBOOT:SD_UNLINK_BEGIN path=%s\n", fat_path);
    f_unlink(fat_path);
    host_printf("KBOOT:SD_OPEN_BEGIN path=%s mode=write\n", fs_path);
    handle_t f = filesystem_file_open(fs_path, FILE_ACCESS_WRITE, FILE_MODE_CREATE_ALWAYS);
    if (!f) {
        host_puts("KBOOT:SD_OPEN_RESULT ok=0 mode=write\n");
        return false;
    }
    host_puts("KBOOT:SD_OPEN_RESULT ok=1 mode=write\n");

    host_printf("KBOOT:SD_WRITE_BEGIN len=%lu\n", (unsigned long)sizeof(wr));
    int n = filesystem_file_write(f, wr, sizeof(wr));
    filesystem_file_close(f);
    host_printf("KBOOT:SD_WRITE_RESULT requested=%lu got=%d\n",
                (unsigned long)sizeof(wr), n);
    if (n != (int)sizeof(wr))
        return false;

    host_printf("KBOOT:SD_OPEN_BEGIN path=%s mode=read\n", fs_path);
    f = filesystem_file_open(fs_path, FILE_ACCESS_READ, FILE_MODE_OPEN_EXISTING);
    if (!f) {
        host_puts("KBOOT:SD_OPEN_RESULT ok=0 mode=read\n");
        return false;
    }
    host_puts("KBOOT:SD_OPEN_RESULT ok=1 mode=read\n");

    host_printf("KBOOT:SD_READ_BEGIN len=%lu\n", (unsigned long)sizeof(rd));
    n = filesystem_file_read(f, rd, sizeof(rd));
    filesystem_file_close(f);
    host_printf("KBOOT:SD_READ_RESULT requested=%lu got=%d\n",
                (unsigned long)sizeof(rd), n);
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

static uint32_t parse_u32_token(const char *s, uint32_t def)
{
    char *end = NULL;
    unsigned long v;
    if (!s || !s[0])
        return def;
    v = strtoul(s, &end, 0);
    if (end == s)
        return def;
    return (uint32_t)v;
}

static void sd_flash_progress(const char *tag, uint32_t bytes, uint32_t ms)
{
    host_printf("KBOOT:SD_FLASH_%s bytes=%lu ms=%lu speed=%luKiB/s\n",
                tag, (unsigned long)bytes, (unsigned long)ms,
                speed_kib_s(bytes, ms));
}

static void sd_flash_command(const char *line)
{
    char raw_path[128] = BOOT_SD_DEFAULT_PATH;
    char fs_path[160];
    char off_s[32] = {0};
    char max_s[32] = {0};
    char verify_s[16] = {0};
    uint32_t flash_off = BOOT_APP_SLOT0_OFFSET;
    uint32_t max_len = 0;
    uint32_t verify = 0;
    uint32_t written = 0;
    uint32_t next_progress = BOOT_SD_FLASH_PROGRESS_STEP;
    TickType_t start;
    int narg;

    narg = sscanf(line, "SD_FLASH %127s %31s %31s %15s",
                  raw_path, off_s, max_s, verify_s);
    if (narg >= 2)
        flash_off = parse_u32_token(off_s, BOOT_APP_SLOT0_OFFSET);
    if (narg >= 3)
        max_len = parse_u32_token(max_s, 0u);
    if (narg >= 4)
        verify = parse_u32_token(verify_s, 0u) ? 1u : 0u;

    if ((flash_off & 0xfffu) != 0) {
        host_printf("KBOOT:SD_FLASH_FAIL bad-off-align off=0x%08lx\n",
                    (unsigned long)flash_off);
        return;
    }

    sd_normalize_path(raw_path, fs_path, sizeof(fs_path));
    LOGF("BOOT_CMD SD_FLASH path=%s off=0x%08lx max=%lu verify=%lu",
         fs_path, (unsigned long)flash_off, (unsigned long)max_len,
         (unsigned long)verify);

    host_printf("KBOOT:SD_FLASH_BEGIN file=%s off=0x%08lx max=%lu verify=%lu chunk=%lu\n",
                raw_path, (unsigned long)flash_off, (unsigned long)max_len,
                (unsigned long)verify, (unsigned long)sizeof(s_buf));

    if (!sd_mount_debug()) {
        host_puts("KBOOT:SD_FLASH_FAIL mount\n");
        return;
    }

    host_printf("KBOOT:SD_OPEN_BEGIN path=%s mode=read\n", fs_path);
    handle_t f = filesystem_file_open(fs_path, FILE_ACCESS_READ, FILE_MODE_OPEN_EXISTING);
    if (!f) {
        host_printf("KBOOT:SD_OPEN_RESULT ok=0 path=%s\n", fs_path);
        host_puts("KBOOT:SD_FLASH_FAIL open\n");
        return;
    }
    host_printf("KBOOT:SD_OPEN_RESULT ok=1 path=%s\n", fs_path);
    host_puts("KBOOT:SD_FLASH_READ_BEGIN\n");

    start = xTaskGetTickCount();
    for (;;) {
        uint32_t ask = sizeof(s_buf);
        if (max_len && written + ask > max_len)
            ask = max_len - written;
        if (ask == 0)
            break;

        int got = filesystem_file_read(f, s_buf, (int)ask);
        if (got < 0) {
            filesystem_file_close(f);
            host_printf("KBOOT:SD_FLASH_FAIL read got=%d\n", got);
            return;
        }
        if (got == 0)
            break;

        uint32_t pos = 0;
        while (pos < (uint32_t)got) {
            uint32_t cur_off = flash_off + written + pos;
            uint32_t page_room;
            uint32_t page_len;
            int rc;

            if ((cur_off & 0xfffu) == 0) {
                host_printf("KBOOT:SD_FLASH_ERASE off=0x%08lx\n",
                            (unsigned long)cur_off);
                rc = boot_flash_sector_4k(cur_off);
                if (rc != 0) {
                    filesystem_file_close(f);
                    host_printf("KBOOT:SD_FLASH_FAIL erase off=0x%08lx rc=%d\n",
                                (unsigned long)cur_off, rc);
                    return;
                }
            }

            page_room = 256u - (cur_off & 0xffu);
            page_len = (uint32_t)got - pos;
            if (page_len > page_room)
                page_len = page_room;

            rc = boot_flash_program_page(cur_off, &s_buf[pos], page_len);
            if (rc != 0) {
                filesystem_file_close(f);
                host_printf("KBOOT:SD_FLASH_FAIL prog off=0x%08lx len=%lu rc=%d\n",
                            (unsigned long)cur_off, (unsigned long)page_len, rc);
                return;
            }

            pos += page_len;
        }

        written += (uint32_t)got;
        while (written >= next_progress) {
            sd_flash_progress("PROGRESS", written, elapsed_ms(start));
            next_progress += BOOT_SD_FLASH_PROGRESS_STEP;
        }

        if ((uint32_t)got < ask)
            break;
    }
    filesystem_file_close(f);

    if (written == 0) {
        host_puts("KBOOT:SD_FLASH_FAIL empty\n");
        return;
    }

    sd_flash_progress("WRITE_OK", written, elapsed_ms(start));

    if (verify) {
        uint32_t checked = 0;
        uint32_t next_verify_progress = BOOT_SD_FLASH_PROGRESS_STEP;
        TickType_t verify_start;

        host_puts("KBOOT:SD_FLASH_VERIFY_BEGIN\n");
        f = filesystem_file_open(fs_path, FILE_ACCESS_READ, FILE_MODE_OPEN_EXISTING);
        if (!f) {
            host_puts("KBOOT:SD_FLASH_FAIL verify-open\n");
            return;
        }

        verify_start = xTaskGetTickCount();
        while (checked < written) {
            uint32_t ask = sizeof(s_buf);
            if (checked + ask > written)
                ask = written - checked;

            int got = filesystem_file_read(f, s_buf, (int)ask);
            if (got != (int)ask) {
                filesystem_file_close(f);
                host_printf("KBOOT:SD_FLASH_VERIFY_FAIL read checked=%lu got=%d ask=%lu\n",
                            (unsigned long)checked, got, (unsigned long)ask);
                return;
            }

            int rc = boot_flash_read_raw(flash_off + checked, s_verify, ask);
            if (rc != 0) {
                filesystem_file_close(f);
                host_printf("KBOOT:SD_FLASH_VERIFY_FAIL flash-read off=0x%08lx rc=%d\n",
                            (unsigned long)(flash_off + checked), rc);
                return;
            }

            if (memcmp(s_buf, s_verify, ask) != 0) {
                uint32_t bad = 0;
                while (bad < ask && s_buf[bad] == s_verify[bad])
                    bad++;
                filesystem_file_close(f);
                host_printf("KBOOT:SD_FLASH_VERIFY_FAIL off=0x%08lx sd=%02x flash=%02x\n",
                            (unsigned long)(flash_off + checked + bad),
                            s_buf[bad], s_verify[bad]);
                return;
            }

            checked += ask;
            while (checked >= next_verify_progress) {
                sd_flash_progress("VERIFY_PROGRESS", checked, elapsed_ms(verify_start));
                next_verify_progress += BOOT_SD_FLASH_PROGRESS_STEP;
            }
        }
        filesystem_file_close(f);
        sd_flash_progress("VERIFY_OK", checked, elapsed_ms(verify_start));
    }

    host_printf("KBOOT:SD_FLASH_OK file=%s off=0x%08lx bytes=%lu verify=%lu\n",
                raw_path, (unsigned long)flash_off, (unsigned long)written,
                (unsigned long)verify);
}

static void flash_sd_command(const char *line)
{
    char off_s[32] = {0};
    char len_s[32] = {0};
    char raw_path[128] = BOOT_SD_DEFAULT_PATH;
    char fs_path[160];
    uint32_t flash_off;
    uint32_t len;
    uint32_t copied = 0;
    TickType_t start;

    if (sscanf(line, "FLASH_SD %31s %31s %127s", off_s, len_s, raw_path) < 3) {
        host_puts("KBOOT:FLASH_SD_FAIL args\n");
        return;
    }

    flash_off = parse_u32_token(off_s, 0);
    len = parse_u32_token(len_s, 0);
    if ((flash_off & 0xfffu) != 0 || len == 0) {
        host_printf("KBOOT:FLASH_SD_FAIL bad-args off=0x%08lx len=%lu\n",
                    (unsigned long)flash_off, (unsigned long)len);
        return;
    }

    sd_normalize_path(raw_path, fs_path, sizeof(fs_path));
    LOGF("BOOT_CMD FLASH_SD off=0x%08lx len=%lu path=%s",
         (unsigned long)flash_off, (unsigned long)len, fs_path);
    host_printf("KBOOT:FLASH_SD_BEGIN off=0x%08lx len=%lu file=%s chunk=%lu\n",
                (unsigned long)flash_off, (unsigned long)len, raw_path,
                (unsigned long)sizeof(s_buf));

    if (!sd_mount_debug()) {
        host_puts("KBOOT:FLASH_SD_FAIL mount\n");
        return;
    }

    host_printf("KBOOT:SD_OPEN_BEGIN path=%s mode=write\n", fs_path);
    handle_t f = filesystem_file_open(fs_path, FILE_ACCESS_WRITE, FILE_MODE_CREATE_ALWAYS);
    if (!f) {
        host_printf("KBOOT:SD_OPEN_RESULT ok=0 path=%s\n", fs_path);
        host_puts("KBOOT:FLASH_SD_FAIL open\n");
        return;
    }
    host_printf("KBOOT:SD_OPEN_RESULT ok=1 path=%s\n", fs_path);

    start = xTaskGetTickCount();
    while (copied < len) {
        uint32_t ask = len - copied;
        int rc;
        int wr;

        if (ask > sizeof(s_buf))
            ask = sizeof(s_buf);

        rc = boot_flash_read_raw(flash_off + copied, s_buf, ask);
        if (rc != 0) {
            filesystem_file_close(f);
            host_printf("KBOOT:FLASH_SD_FAIL flash-read off=0x%08lx rc=%d\n",
                        (unsigned long)(flash_off + copied), rc);
            return;
        }

        wr = filesystem_file_write(f, s_buf, ask);
        if (wr != (int)ask) {
            filesystem_file_close(f);
            host_printf("KBOOT:FLASH_SD_FAIL sd-write copied=%lu wr=%d ask=%lu\n",
                        (unsigned long)copied, wr, (unsigned long)ask);
            return;
        }

        copied += ask;
        if ((copied % BOOT_SD_FLASH_PROGRESS_STEP) == 0 || copied == len)
            host_printf("KBOOT:FLASH_SD_PROGRESS bytes=%lu ms=%lu\n",
                        (unsigned long)copied, (unsigned long)elapsed_ms(start));
    }

    filesystem_file_close(f);
    host_printf("KBOOT:FLASH_SD_OK file=%s off=0x%08lx bytes=%lu ms=%lu\n",
                raw_path, (unsigned long)flash_off, (unsigned long)copied,
                (unsigned long)elapsed_ms(start));
}

static void help_command(void)
{
    host_puts("KBOOT:HELP HELP SELFTEST SPI3_ID SPI3_READ <off> <len> SPI3_RW [off] SD_MOUNT SD_LS SD_READ [path] [len] SD_TEST SD_FLASH [path] [off] [max_len] [verify] FLASH_SD <off> <len> <path> RESET DONE\n");
    host_puts("KBOOT:HELP sd-default=0:/app_slot0.bin sd-read-max=512 sd-flash-default-off=0x00100000\n");
    host_puts("KBOOT:HELP sd-flash-max_len=0 means until EOF, verify=0 fastest, verify=1 readback-check\n");
    host_puts("KBOOT:HELP scratch-default=0x00F00000 rw-erases-4k-sector\n");
    host_puts("KBOOT:HELP_END\n");
}

static void sd_test_command(void)
{
    LOG("BOOT_CMD SD_TEST");
    host_puts("KBOOT:SD_DEBUG_BEGIN op=rw-test\n");
    host_puts(sd_rw_probe() ? "KBOOT:SD_OK rw-64\n" : "KBOOT:SD_FAIL rw\n");
    host_puts("KBOOT:SD_DEBUG_END op=rw-test\n");
}

static void sd_ls_command(void)
{
    find_find_data_t fd;

    LOG("BOOT_CMD SD_LS");
    host_puts("KBOOT:SD_DEBUG_BEGIN op=ls\n");
    if (!sd_mount_debug()) {
        host_puts("KBOOT:SD_LS_FAIL mount\n");
        host_puts("KBOOT:SD_DEBUG_END op=ls\n");
        return;
    }

    handle_t h = filesystem_find_first("/fs/0/", "*", &fd);
    if (!h) {
        host_puts("KBOOT:SD_LS_EMPTY\n");
        host_puts("KBOOT:SD_DEBUG_END op=ls\n");
        return;
    }

    do {
        host_printf("KBOOT:SD_LS_ENTRY %s\n", fd.filename);
    } while (filesystem_find_next(h, &fd));
    filesystem_find_close(h);
    host_puts("KBOOT:SD_LS_OK\n");
    host_puts("KBOOT:SD_DEBUG_END op=ls\n");
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
    host_puts("KBOOT:TEST SD_RW SKIP use-SD_MOUNT-or-SD_READ\n");
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
        if (strcmp(line, "SD_MOUNT") == 0) { sd_mount_command(); continue; }
        if (strcmp(line, "SD_LS") == 0) { sd_ls_command(); continue; }
        if (strncmp(line, "SD_FLASH", 8) == 0) { sd_flash_command(line); continue; }
        if (strncmp(line, "FLASH_SD", 8) == 0) { flash_sd_command(line); continue; }
        if (strncmp(line, "SD_READ", 7) == 0) { sd_read_command(line); continue; }
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
