#pragma once

#include <stdint.h>

extern uint32_t boot_decision_reset_status_raw;

uint32_t boot_decision_get_reason(void);
