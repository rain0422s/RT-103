#include "boot_ota.h"

#include "boot_flash.h"
#include "ota_boot_state.h"
#include "ota_crc32.h"
#include "ota_layout.h"
#include "ota_manifest.h"
#include "ota_store.h"
#include <stdint.h>

#define BOOT_OTA_CHUNK_SIZE 256U
#define BOOT_OTA_VECTOR_SIZE 8U

#ifndef BOOT_DIAG_UART
#define BOOT_DIAG_UART 0
#endif

#if BOOT_DIAG_UART
void boot_diag_puts(const char *text);
#else
static void boot_diag_puts(const char *text) { (void)text; }
#endif

static uint8_t s_boot_ota_buf[BOOT_OTA_CHUNK_SIZE];

static void boot_ota_mark(ota_manifest_t *manifest, ota_manifest_state_t state)
{
	ota_manifest_set_state(manifest, state);
	(void)ota_store_write_manifest(manifest);
}

static void boot_ota_mark_failure(ota_manifest_t *manifest,
				  ota_failure_reason_t reason,
				  uint32_t detail)
{
	ota_manifest_set_failure(manifest, reason, detail);
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

static uint32_t boot_ota_load_le32(const uint8_t *data)
{
	return (uint32_t)data[0] |
	       ((uint32_t)data[1] << 8U) |
	       ((uint32_t)data[2] << 16U) |
	       ((uint32_t)data[3] << 24U);
}

static bool boot_ota_image_header_ok(const ota_manifest_t *manifest,
				     uint32_t app_base)
{
	uint32_t msp;
	uint32_t reset;

	if (manifest->image_size < BOOT_OTA_VECTOR_SIZE)
		return false;
	if (!ota_store_read_image(0UL, s_boot_ota_buf, BOOT_OTA_VECTOR_SIZE))
		return false;

	msp = boot_ota_load_le32(&s_boot_ota_buf[0]);
	reset = boot_ota_load_le32(&s_boot_ota_buf[4]);
	if (msp < OTA_SRAM_BASE || msp > OTA_SRAM_END)
		return false;
	if (reset < app_base || reset >= app_base + OTA_APP_SLOT_SIZE)
		return false;
	if ((reset & 1UL) == 0UL)
		return false;
	return true;
}

static bool boot_ota_program_app(const ota_manifest_t *manifest,
				 uint32_t app_base)
{
	uint32_t remaining = manifest->image_size;
	uint32_t offset = 0;

	while (remaining > 0UL) {
		uint32_t chunk = remaining > BOOT_OTA_CHUNK_SIZE ?
				 BOOT_OTA_CHUNK_SIZE : remaining;
		if (!ota_store_read_image(offset, s_boot_ota_buf, chunk))
			return false;
		if (!boot_flash_program(app_base + offset, s_boot_ota_buf, chunk))
			return false;
		offset += chunk;
		remaining -= chunk;
	}
	return true;
}

static bool boot_ota_app_crc_ok(const ota_manifest_t *manifest,
				uint32_t app_base)
{
	const uint8_t *app = (const uint8_t *)app_base;
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

void boot_ota_mark_confirm_timeout(uint32_t detail)
{
	ota_manifest_t manifest;

	if (!ota_store_read_manifest(&manifest))
		return;
	if (!ota_manifest_is_valid(&manifest))
		return;
	boot_ota_mark_failure(&manifest, OTA_FAIL_BOOT_CONFIRM_TIMEOUT, detail);
}

bool boot_ota_apply_if_pending(void)
{
	ota_manifest_t manifest;
	ota_boot_state_t boot_state;
	uint32_t app_base;

	if (!ota_store_read_manifest(&manifest)) {
		boot_diag_puts("m\r\n");
		return false;
	}
	boot_diag_puts("M\r\n");
	if (!ota_manifest_is_pending(&manifest)) {
		boot_diag_puts("I\r\n");
		return false;
	}
	(void)ota_boot_state_read(&boot_state);
	if (manifest.firmware_version < boot_state.accepted_version ||
	    boot_state.accepted_version < manifest.min_allowed_version) {
		boot_diag_puts("W\r\n");
		boot_ota_mark_failure(&manifest, OTA_FAIL_VERSION_ROLLBACK,
				      boot_state.accepted_version);
		return false;
	}
	app_base = ota_boot_state_slot_base(manifest.target_slot);
	if (app_base == 0UL) {
		boot_diag_puts("T\r\n");
		boot_ota_mark_failure(&manifest, OTA_FAIL_HEADER_INVALID,
				      manifest.target_slot);
		return false;
	}
	boot_diag_puts("H\r\n");
	if (!boot_ota_image_header_ok(&manifest, app_base)) {
		boot_diag_puts("h\r\n");
		boot_ota_mark_failure(&manifest, OTA_FAIL_HEADER_INVALID,
				      app_base);
		return false;
	}
	boot_diag_puts("R\r\n");
	if (!boot_ota_image_crc_ok(&manifest)) {
		boot_diag_puts("r\r\n");
		boot_ota_mark_failure(&manifest,
				      OTA_FAIL_IMAGE_CRC_MISMATCH,
				      manifest.image_crc32);
		return false;
	}
	if (!boot_flash_erase_slot(app_base, manifest.image_size)) {
		boot_diag_puts("e\r\n");
		boot_ota_mark_failure(&manifest,
				      OTA_FAIL_FLASH_ERASE_FAILED, app_base);
		return false;
	}
	boot_diag_puts("P\r\n");
	if (!boot_ota_program_app(&manifest, app_base)) {
		boot_diag_puts("p\r\n");
		boot_ota_mark_failure(&manifest,
				      OTA_FAIL_FLASH_PROGRAM_FAILED, app_base);
		return false;
	}
	boot_diag_puts("V\r\n");
	if (!boot_ota_app_crc_ok(&manifest, app_base)) {
		boot_diag_puts("v\r\n");
		boot_ota_mark_failure(&manifest,
				      OTA_FAIL_APP_CRC_MISMATCH,
				      manifest.image_crc32);
		return false;
	}
	if (!ota_boot_state_set_pending(manifest.target_slot,
					manifest.firmware_version)) {
		boot_diag_puts("b\r\n");
		boot_ota_mark_failure(&manifest,
				      OTA_FAIL_BOOT_STATE_WRITE_FAILED,
				      manifest.target_slot);
		return false;
	}
	boot_ota_mark(&manifest, OTA_MANIFEST_STATE_APPLIED);
	return true;
}
