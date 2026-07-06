#pragma once

#include <stdint.h>
#include "boot_config.h"

int boot_flash_read(uint32_t flash_offset, void *dst, uint32_t len);
int boot_flash_read_app_header(uint32_t slot_offset, boot_app_header_t *out);
int boot_flash_load_app_image(const boot_app_header_t *hdr);
uint32_t boot_flash_read_jedec_id(void);
