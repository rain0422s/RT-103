#ifndef OTA_MANIFEST_H
#define OTA_MANIFEST_H

#include <stdbool.h>
#include <stdint.h>

#define OTA_MANIFEST_MAGIC   0x3141544FUL
#define OTA_MANIFEST_VERSION 1U
#define OTA_MANIFEST_SIZE    40U

typedef enum {
        OTA_MANIFEST_STATE_IDLE = 0,
        OTA_MANIFEST_STATE_RECEIVING = 1,
        OTA_MANIFEST_STATE_STAGED = 2,
        OTA_MANIFEST_STATE_PENDING = 3,
        OTA_MANIFEST_STATE_APPLIED = 4,
        OTA_MANIFEST_STATE_ERROR = 5,
} ota_manifest_state_t;

typedef struct {
        uint32_t magic;
        uint16_t version;
        uint16_t header_size;
        uint32_t state;
        uint32_t image_size;
        uint32_t image_crc32;
        uint32_t received_size;
        uint32_t target_app_addr;
        uint32_t target_app_max_size;
        uint32_t sequence;
        uint32_t manifest_crc32;
} ota_manifest_t;

_Static_assert(sizeof(ota_manifest_t) == OTA_MANIFEST_SIZE,
               "ota_manifest_t size must remain fixed");

void ota_manifest_init(ota_manifest_t *manifest);
uint32_t ota_manifest_crc(const ota_manifest_t *manifest);
void ota_manifest_finalize(ota_manifest_t *manifest);
bool ota_manifest_is_valid(const ota_manifest_t *manifest);
bool ota_manifest_is_pending(const ota_manifest_t *manifest);
void ota_manifest_set_state(ota_manifest_t *manifest, ota_manifest_state_t state);

#endif
