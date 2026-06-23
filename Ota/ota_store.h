#ifndef OTA_STORE_H
#define OTA_STORE_H

#include "ota_manifest.h"
#include <stdbool.h>
#include <stdint.h>

bool ota_store_read_manifest(ota_manifest_t *manifest);
bool ota_store_write_manifest(const ota_manifest_t *manifest);
bool ota_store_clear_manifest(void);
bool ota_store_erase_image_slot(uint32_t image_size);
bool ota_store_write_image(uint32_t offset, const uint8_t *data, uint32_t len);
bool ota_store_read_image(uint32_t offset, uint8_t *data, uint32_t len);

#endif
