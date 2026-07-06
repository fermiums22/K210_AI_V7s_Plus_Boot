#pragma once

#include <stdint.h>
#include "boot_config.h"

extern uint32_t boot_decision_reset_status_raw;
extern boot_app_header_t boot_decision_app_header;

uint32_t boot_decision_get_reason(void);
