#include "ota_crc32.h"

#include <stdint.h>

uint32_t ota_crc32_begin(void)
{
        return 0xFFFFFFFFUL;
}

uint32_t ota_crc32_update(uint32_t crc, const void *data, uint32_t len)
{
        const uint8_t *bytes = (const uint8_t *)data;

        if (bytes == 0)
                return crc;
        for (uint32_t i = 0; i < len; i++) {
                crc ^= bytes[i];
                for (uint8_t bit = 0; bit < 8U; bit++) {
                        if ((crc & 1UL) != 0UL)
                                crc = (crc >> 1U) ^ 0xEDB88320UL;
                        else
                                crc >>= 1U;
                }
        }
        return crc;
}

uint32_t ota_crc32_finish(uint32_t crc)
{
        return crc ^ 0xFFFFFFFFUL;
}

uint32_t ota_crc32_compute(const void *data, uint32_t len)
{
        return ota_crc32_finish(ota_crc32_update(ota_crc32_begin(), data, len));
}
