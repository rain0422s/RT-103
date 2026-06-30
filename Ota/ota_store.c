#include "ota_store.h"

#include "ota_layout.h"
#include "w25qxx.h"
#include <stddef.h>

static bool ota_store_lock(void)
{
        return w25qxx_lock();
}

static void ota_store_unlock(void)
{
        w25qxx_unlock();
}

static bool ota_store_range_ok(uint32_t offset, uint32_t len)
{
        if (offset > OTA_IMAGE_SLOT_SIZE)
                return false;
        if (len > OTA_IMAGE_SLOT_SIZE - offset)
                return false;
        return true;
}

bool ota_store_read_manifest(ota_manifest_t *manifest)
{
        bool ok;

        if (manifest == NULL)
                return false;
        if (!ota_store_lock())
                return false;
        ok = w25qxx_read((uint8_t *)manifest, OTA_MANIFEST_ADDR,
                         (uint32_t)sizeof(*manifest)) == W25Qx_OK;
        ota_store_unlock();
        return ok;
}

bool ota_store_write_manifest(const ota_manifest_t *manifest)
{
        ota_manifest_t copy;
        bool ok;

        if (manifest == NULL)
                return false;
        if (!ota_store_lock())
                return false;

        copy = *manifest;
        ota_manifest_finalize(&copy);
        if (w25qxx_erase_block(OTA_MANIFEST_ADDR) != W25Qx_OK) {
                ota_store_unlock();
                return false;
        }
        ok = w25qxx_write((uint8_t *)&copy, OTA_MANIFEST_ADDR,
                          (uint32_t)sizeof(copy)) == W25Qx_OK;
        ota_store_unlock();
        return ok;
}

bool ota_store_clear_manifest(void)
{
        bool ok;

        if (!ota_store_lock())
                return false;
        ok = w25qxx_erase_block(OTA_MANIFEST_ADDR) == W25Qx_OK;
        ota_store_unlock();
        return ok;
}

bool ota_store_erase_image_slot(uint32_t image_size)
{
        uint32_t erase_size;
        bool ok = true;

        if (image_size == 0UL || image_size > OTA_IMAGE_SLOT_SIZE)
                return false;
        if (!ota_store_lock())
                return false;

        erase_size = (image_size + OTA_W25Q_SECTOR_SIZE - 1UL) &
                     ~(OTA_W25Q_SECTOR_SIZE - 1UL);
        for (uint32_t off = 0; off < erase_size; off += OTA_W25Q_SECTOR_SIZE) {
                if (w25qxx_erase_block(OTA_IMAGE_ADDR + off) != W25Qx_OK) {
                        ok = false;
                        break;
                }
        }
        ota_store_unlock();
        return ok;
}

bool ota_store_write_image(uint32_t offset, const uint8_t *data, uint32_t len)
{
        bool ok;

        if (!ota_store_range_ok(offset, len))
                return false;
        if (len == 0UL)
                return true;
        if (data == NULL)
                return false;
        if (!ota_store_lock())
                return false;
        ok = w25qxx_write((uint8_t *)data, OTA_IMAGE_ADDR + offset, len) == W25Qx_OK;
        ota_store_unlock();
        return ok;
}

bool ota_store_read_image(uint32_t offset, uint8_t *data, uint32_t len)
{
        bool ok;

        if (!ota_store_range_ok(offset, len))
                return false;
        if (len == 0UL)
                return true;
        if (data == NULL)
                return false;
        if (!ota_store_lock())
                return false;
        ok = w25qxx_read(data, OTA_IMAGE_ADDR + offset, len) == W25Qx_OK;
        ota_store_unlock();
        return ok;
}
