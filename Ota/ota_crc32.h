#ifndef OTA_CRC32_H
#define OTA_CRC32_H

#include <stdint.h>

uint32_t ota_crc32_begin(void);
uint32_t ota_crc32_update(uint32_t crc, const void *data, uint32_t len);
uint32_t ota_crc32_finish(uint32_t crc);
uint32_t ota_crc32_compute(const void *data, uint32_t len);

#endif
