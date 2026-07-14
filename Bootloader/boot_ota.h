#ifndef BOOT_OTA_H
#define BOOT_OTA_H

#include <stdbool.h>
#include <stdint.h>

bool boot_ota_apply_if_pending(void);
void boot_ota_mark_confirm_timeout(uint32_t detail);

#endif
