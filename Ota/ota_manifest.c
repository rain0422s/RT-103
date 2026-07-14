#include "ota_manifest.h"

#include "ota_boot_state.h"
#include "ota_crc32.h"
#include "ota_layout.h"
#include <string.h>

static bool ota_manifest_state_is_known(uint32_t state)
{
        return state >= OTA_MANIFEST_STATE_IDLE &&
               state <= OTA_MANIFEST_STATE_ERROR;
}

static bool ota_manifest_failure_is_known(uint32_t reason)
{
        return reason >= OTA_FAIL_NONE &&
               reason <= OTA_FAIL_BOOT_STATE_WRITE_FAILED;
}

void ota_manifest_init(ota_manifest_t *manifest)
{
        uint32_t slot;

        if (manifest == 0)
                return;
        memset(manifest, 0, sizeof(*manifest));
        manifest->magic = OTA_MANIFEST_MAGIC;
        manifest->version = OTA_MANIFEST_VERSION;
        manifest->header_size = (uint16_t)sizeof(*manifest);
        manifest->state = OTA_MANIFEST_STATE_IDLE;
        manifest->firmware_version = OTA_FW_VERSION;
        manifest->min_allowed_version = 0UL;
        slot = ota_boot_state_current_slot();
        manifest->target_slot = slot;
        manifest->target_app_addr = ota_boot_state_slot_base(slot);
        manifest->target_app_max_size = OTA_APP_SLOT_SIZE;
        manifest->failure_reason = OTA_FAIL_NONE;
        manifest->checkpoint_size = OTA_RESUME_CHECKPOINT_SIZE;
        ota_manifest_finalize(manifest);
}

uint32_t ota_manifest_crc(const ota_manifest_t *manifest)
{
        ota_manifest_t copy;

        if (manifest == 0)
                return 0UL;
        copy = *manifest;
        copy.manifest_crc32 = 0UL;
        return ota_crc32_compute(&copy, (uint32_t)sizeof(copy));
}

void ota_manifest_finalize(ota_manifest_t *manifest)
{
        if (manifest == 0)
                return;
        manifest->manifest_crc32 = 0UL;
        manifest->manifest_crc32 = ota_manifest_crc(manifest);
}

bool ota_manifest_is_valid(const ota_manifest_t *manifest)
{
        if (manifest == 0)
                return false;
        if (manifest->magic != OTA_MANIFEST_MAGIC)
                return false;
        if (manifest->version != OTA_MANIFEST_VERSION)
                return false;
        if (manifest->header_size != (uint16_t)sizeof(*manifest))
                return false;
        if (!ota_manifest_state_is_known(manifest->state))
                return false;
        if (!ota_manifest_failure_is_known(manifest->failure_reason))
                return false;
        if (manifest->target_app_addr !=
            ota_boot_state_slot_base(manifest->target_slot))
                return false;
        if (manifest->target_app_max_size != OTA_APP_SLOT_SIZE)
                return false;
        if (manifest->checkpoint_size != OTA_RESUME_CHECKPOINT_SIZE)
                return false;
        if (manifest->image_size > OTA_APP_SLOT_SIZE)
                return false;
        if (manifest->received_size > manifest->image_size)
                return false;
        if (manifest->manifest_crc32 != ota_manifest_crc(manifest))
                return false;
        return true;
}

bool ota_manifest_is_pending(const ota_manifest_t *manifest)
{
        return ota_manifest_is_valid(manifest) &&
               manifest->state == OTA_MANIFEST_STATE_PENDING &&
               manifest->image_size > 0UL &&
               manifest->received_size == manifest->image_size;
}

void ota_manifest_set_state(ota_manifest_t *manifest, ota_manifest_state_t state)
{
        if (manifest == 0)
                return;
        manifest->state = (uint32_t)state;
        ota_manifest_finalize(manifest);
}

void ota_manifest_set_failure(ota_manifest_t *manifest,
                              ota_failure_reason_t reason,
                              uint32_t detail)
{
        if (manifest == 0)
                return;
        manifest->failure_reason = (uint32_t)reason;
        manifest->failure_detail = detail;
        if (reason != OTA_FAIL_NONE)
                manifest->state = OTA_MANIFEST_STATE_ERROR;
        ota_manifest_finalize(manifest);
}
