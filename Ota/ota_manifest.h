#ifndef OTA_MANIFEST_H
#define OTA_MANIFEST_H

#include <stdbool.h>
#include <stdint.h>

#define OTA_MANIFEST_MAGIC   0x3141544FUL
#define OTA_MANIFEST_VERSION 2U
#define OTA_MANIFEST_SIZE    68U

#ifndef OTA_FW_VERSION
#define OTA_FW_VERSION       1UL
#endif

typedef enum {
        OTA_MANIFEST_STATE_IDLE = 0,
        OTA_MANIFEST_STATE_RECEIVING = 1,
        OTA_MANIFEST_STATE_STAGED = 2,
        OTA_MANIFEST_STATE_PENDING = 3,
        OTA_MANIFEST_STATE_APPLIED = 4,
        OTA_MANIFEST_STATE_ERROR = 5,
} ota_manifest_state_t;

typedef enum {
        OTA_FAIL_NONE = 0,
        OTA_FAIL_HEADER_INVALID = 1,
        OTA_FAIL_IMAGE_CRC_MISMATCH = 2,
        OTA_FAIL_FLASH_ERASE_FAILED = 3,
        OTA_FAIL_FLASH_PROGRAM_FAILED = 4,
        OTA_FAIL_APP_CRC_MISMATCH = 5,
        OTA_FAIL_VERSION_ROLLBACK = 6,
        OTA_FAIL_BOOT_CONFIRM_TIMEOUT = 7,
        OTA_FAIL_BOOT_STATE_WRITE_FAILED = 8,
} ota_failure_reason_t;

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
        uint32_t firmware_version;
        uint32_t min_allowed_version;
        uint32_t target_slot;
        uint32_t failure_reason;
        uint32_t failure_detail;
        uint32_t checkpoint_size;
        uint32_t binary_sequence;
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
void ota_manifest_set_failure(ota_manifest_t *manifest,
                              ota_failure_reason_t reason,
                              uint32_t detail);

#endif
