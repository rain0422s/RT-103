#ifndef OTA_BOOT_STATE_H
#define OTA_BOOT_STATE_H

#include <stdbool.h>
#include <stdint.h>

#define OTA_BOOT_STATE_MAGIC                0x3153424FUL
#define OTA_BOOT_STATE_VERSION              1U
#define OTA_BOOT_STATE_SIZE                 44U
#define OTA_BOOT_CONFIRM_MAX_ATTEMPTS       2UL
#define OTA_BOOT_STATE_INVALID_SLOT         0xFFFFFFFFUL

typedef enum {
        OTA_SLOT_A = 0,
        OTA_SLOT_B = 1,
} ota_slot_t;

typedef struct {
        uint32_t magic;
        uint16_t version;
        uint16_t header_size;
        uint32_t active_slot;
        uint32_t confirmed_slot;
        uint32_t pending_slot;
        uint32_t accepted_version;
        uint32_t pending_version;
        uint32_t boot_attempts;
        uint32_t sequence;
        uint32_t last_failure_reason;
        uint32_t state_crc32;
} ota_boot_state_t;

_Static_assert(sizeof(ota_boot_state_t) == OTA_BOOT_STATE_SIZE,
               "ota_boot_state_t size must remain fixed");

void ota_boot_state_defaults(ota_boot_state_t *state);
bool ota_boot_state_read(ota_boot_state_t *state);
bool ota_boot_state_write(ota_boot_state_t *state);
bool ota_boot_state_slot_is_valid(uint32_t slot);
uint32_t ota_boot_state_current_slot(void);
uint32_t ota_boot_state_other_slot(uint32_t slot);
uint32_t ota_boot_state_slot_base(uint32_t slot);
bool ota_boot_state_select_boot_slot(ota_boot_state_t *state,
                                     uint32_t *slot,
                                     bool *rolled_back);
bool ota_boot_state_set_pending(uint32_t slot, uint32_t firmware_version);
bool ota_boot_state_confirm_current(uint32_t firmware_version);

#endif
