#pragma once

/* Returns 1 if an update was applied, 0 if no update file was present,
 * negative on error.  On success the caller should reboot or halt. */
int boot_update_try_sd_app_slot0(void);
