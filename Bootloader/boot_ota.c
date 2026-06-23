#include "boot_ota.h"

#include "boot_flash.h"
#include "ota_crc32.h"
#include "ota_layout.h"
#include "ota_manifest.h"
#include "ota_store.h"
#include <stdint.h>

#define BOOT_OTA_CHUNK_SIZE 256U

static uint8_t s_boot_ota_buf[BOOT_OTA_CHUNK_SIZE];

static void boot_ota_mark(ota_manifest_t *manifest, ota_manifest_state_t state)
{
	ota_manifest_set_state(manifest, state);
	(void)ota_store_write_manifest(manifest);
}

static bool boot_ota_image_crc_ok(const ota_manifest_t *manifest)
{
	uint32_t crc = ota_crc32_begin();
	uint32_t remaining = manifest->image_size;
	uint32_t offset = 0;

	while (remaining > 0UL) {
		uint32_t chunk = remaining > BOOT_OTA_CHUNK_SIZE ?
				 BOOT_OTA_CHUNK_SIZE : remaining;
		if (!ota_store_read_image(offset, s_boot_ota_buf, chunk))
			return false;
		crc = ota_crc32_update(crc, s_boot_ota_buf, chunk);
		offset += chunk;
		remaining -= chunk;
	}
	return ota_crc32_finish(crc) == manifest->image_crc32;
}

static bool boot_ota_program_app(const ota_manifest_t *manifest)
{
	uint32_t remaining = manifest->image_size;
	uint32_t offset = 0;

	if (!boot_flash_erase_app(manifest->image_size))
		return false;
	while (remaining > 0UL) {
		uint32_t chunk = remaining > BOOT_OTA_CHUNK_SIZE ?
				 BOOT_OTA_CHUNK_SIZE : remaining;
		if (!ota_store_read_image(offset, s_boot_ota_buf, chunk))
			return false;
		if (!boot_flash_program(OTA_APP_BASE + offset, s_boot_ota_buf, chunk))
			return false;
		offset += chunk;
		remaining -= chunk;
	}
	return true;
}

static bool boot_ota_app_crc_ok(const ota_manifest_t *manifest)
{
	const uint8_t *app = (const uint8_t *)OTA_APP_BASE;
	uint32_t crc = ota_crc32_begin();
	uint32_t remaining = manifest->image_size;
	uint32_t offset = 0;

	while (remaining > 0UL) {
		uint32_t chunk = remaining > BOOT_OTA_CHUNK_SIZE ?
				 BOOT_OTA_CHUNK_SIZE : remaining;
		crc = ota_crc32_update(crc, &app[offset], chunk);
		offset += chunk;
		remaining -= chunk;
	}
	return ota_crc32_finish(crc) == manifest->image_crc32;
}

bool boot_ota_apply_if_pending(void)
{
	ota_manifest_t manifest;

	if (!ota_store_read_manifest(&manifest))
		return false;
	if (!ota_manifest_is_pending(&manifest))
		return false;
	if (!boot_ota_image_crc_ok(&manifest)) {
		boot_ota_mark(&manifest, OTA_MANIFEST_STATE_ERROR);
		return false;
	}
	if (!boot_ota_program_app(&manifest)) {
		boot_ota_mark(&manifest, OTA_MANIFEST_STATE_ERROR);
		return false;
	}
	if (!boot_ota_app_crc_ok(&manifest)) {
		boot_ota_mark(&manifest, OTA_MANIFEST_STATE_ERROR);
		return false;
	}
	boot_ota_mark(&manifest, OTA_MANIFEST_STATE_APPLIED);
	return true;
}
