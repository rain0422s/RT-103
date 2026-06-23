#include "ota_manifest.h"

#include "ota_crc32.h"
#include "ota_layout.h"
#include <string.h>

void ota_manifest_init(ota_manifest_t *manifest)
{
        if (manifest == 0)
                return;
        memset(manifest, 0, sizeof(*manifest));
        manifest->magic = OTA_MANIFEST_MAGIC;
        manifest->version = OTA_MANIFEST_VERSION;
        manifest->header_size = (uint16_t)sizeof(*manifest);
        manifest->state = OTA_MANIFEST_STATE_IDLE;
        manifest->target_app_addr = OTA_APP_BASE;
        manifest->target_app_max_size = OTA_APP_SIZE;
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
        if (manifest->target_app_addr != OTA_APP_BASE)
                return false;
        if (manifest->target_app_max_size != OTA_APP_SIZE)
                return false;
        if (manifest->image_size > OTA_APP_SIZE)
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
