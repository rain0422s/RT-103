#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#include <stdbool.h>
#include <stdint.h>

bool boot_app_is_valid(void);
bool boot_app_is_valid_at(uint32_t app_base);
void boot_jump_to_app(void);
void boot_jump_to_slot(uint32_t app_base);

#endif
