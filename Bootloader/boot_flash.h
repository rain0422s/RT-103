#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stdbool.h>
#include <stdint.h>

bool boot_flash_erase_app(uint32_t image_size);
bool boot_flash_erase_slot(uint32_t app_base, uint32_t image_size);
bool boot_flash_program(uint32_t address, const uint8_t *data, uint32_t len);

#endif
