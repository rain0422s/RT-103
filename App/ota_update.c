#include "ota_update.h"

#include "main.h"
#include "ota_crc32.h"
#include "ota_layout.h"
#include "ota_manifest.h"
#include "ota_store.h"
#include "storage.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OTA_APPLY_REPLY_DRAIN_MS 20U
#define OTA_NVIC_IRQ_WORDS       8U

typedef void (*ota_boot_entry_t)(void);

typedef struct {
	bool active;
	bool staged;
	ota_manifest_t manifest;
	uint32_t running_crc;
} ota_rx_state_t;

static ota_rx_state_t s_ota;
static uint8_t s_ota_chunk[OTA_TRANSFER_MAX_DATA_LEN];

static void ota_reply(ota_reply_fn reply, void *ctx, const char *text)
{
	if (reply != 0)
		reply(text, ctx);
}

static bool ota_parse_u32_dec(const char *s, uint32_t *out)
{
	uint32_t value = 0;
	bool has_digit = false;

	if (s == 0 || out == 0)
		return false;
	while (*s >= '0' && *s <= '9') {
		const uint32_t digit = (uint32_t)(*s - '0');
		if (value > (UINT32_MAX - digit) / 10UL)
			return false;
		has_digit = true;
		value = value * 10UL + digit;
		s++;
	}
	if (!has_digit || (*s != '\0' && *s != ' '))
		return false;
	*out = value;
	return true;
}

static int ota_hex_value(char ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	return -1;
}

static bool ota_parse_u32_hex(const char *s, uint32_t *out)
{
	uint32_t value = 0;
	unsigned int digits = 0;

	if (s == 0 || out == 0)
		return false;
	while (*s != '\0' && *s != ' ') {
		int v = ota_hex_value(*s);
		if (v < 0 || digits >= 8U)
			return false;
		value = (value << 4U) | (uint32_t)v;
		digits++;
		s++;
	}
	if (digits == 0U)
		return false;
	*out = value;
	return true;
}

static const char *ota_arg_value(const char *line, const char *key)
{
	const size_t key_len = strlen(key);
	const char *p = line;

	while (p != 0 && *p != '\0') {
		while (*p == ' ')
			p++;
		if (strncmp(p, key, key_len) == 0 && p[key_len] == '=')
			return &p[key_len + 1U];
		p = strchr(p, ' ');
		if (p == 0)
			break;
		p++;
	}
	return 0;
}

static bool ota_decode_hex(const char *hex, uint8_t *out, uint32_t len)
{
	if (hex == 0 || out == 0)
		return false;
	for (uint32_t i = 0; i < len; i++) {
		int hi = ota_hex_value(hex[i * 2U]);
		int lo;
		if (hi < 0)
			return false;
		lo = ota_hex_value(hex[i * 2U + 1U]);
		if (lo < 0)
			return false;
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return hex[len * 2U] == '\0' || hex[len * 2U] == ' ';
}

static bool ota_flash_ready(void)
{
	storage_flash_init();
	return storage_flash_is_present();
}

static bool ota_bootloader_is_valid(void)
{
	const uint32_t msp = *(volatile uint32_t *)OTA_BOOTLOADER_BASE;
	const uint32_t reset = *(volatile uint32_t *)(OTA_BOOTLOADER_BASE + 4UL);

	if (msp < OTA_SRAM_BASE || msp > OTA_SRAM_END)
		return false;
	if (reset < OTA_BOOTLOADER_BASE || reset >= OTA_APP_BASE)
		return false;
	if ((reset & 1UL) == 0UL)
		return false;
	return true;
}

static void ota_switch_to_hsi(void)
{
	uint32_t timeout;

	RCC->CR |= RCC_CR_HSION;
	timeout = 0xFFFFUL;
	while ((RCC->CR & RCC_CR_HSIRDY) == 0UL && timeout-- > 0UL) {
	}

	RCC->CFGR &= ~RCC_CFGR_SW;
	timeout = 0xFFFFUL;
	while ((RCC->CFGR & RCC_CFGR_SWS) != 0UL && timeout-- > 0UL) {
	}

	RCC->CR &= ~RCC_CR_PLLON;
	timeout = 0xFFFFUL;
	while ((RCC->CR & RCC_CR_PLLRDY) != 0UL && timeout-- > 0UL) {
	}
	RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_CSSON);
}

static void ota_jump_to_bootloader(void)
{
	const uint32_t boot_msp = *(volatile uint32_t *)OTA_BOOTLOADER_BASE;
	const uint32_t boot_reset =
		*(volatile uint32_t *)(OTA_BOOTLOADER_BASE + 4UL);
	const ota_boot_entry_t boot_entry = (ota_boot_entry_t)boot_reset;

	__HAL_RCC_GPIOB_CLK_ENABLE();
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);

	__disable_irq();
	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL = 0;
	SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk | SCB_ICSR_PENDSTCLR_Msk;
	for (uint32_t i = 0; i < OTA_NVIC_IRQ_WORDS; i++) {
		NVIC->ICER[i] = 0xFFFFFFFFUL;
		NVIC->ICPR[i] = 0xFFFFFFFFUL;
	}
	ota_switch_to_hsi();
	SCB->VTOR = OTA_BOOTLOADER_BASE;
	__set_BASEPRI(0U);
	__set_FAULTMASK(0U);
	__set_MSP(boot_msp);
	__set_PSP(0U);
	__set_CONTROL(0U);
	__DSB();
	__ISB();
	__enable_irq();
	boot_entry();

	while (1) {
	}
}

static bool ota_begin(char *line, ota_reply_fn reply, void *ctx)
{
	uint32_t size;
	uint32_t crc;

	if (!ota_parse_u32_dec(ota_arg_value(line, "size"), &size) ||
	    !ota_parse_u32_hex(ota_arg_value(line, "crc"), &crc) ||
	    size == 0UL || size > OTA_APP_SIZE) {
		ota_reply(reply, ctx, "ERR OTA BEGIN");
		return true;
	}
	if (!ota_flash_ready() || !ota_store_clear_manifest() ||
	    !ota_store_erase_image_slot(size)) {
		ota_reply(reply, ctx, "ERR OTA FLASH");
		return true;
	}

	memset(&s_ota, 0, sizeof(s_ota));
	ota_manifest_init(&s_ota.manifest);
	s_ota.manifest.state = OTA_MANIFEST_STATE_RECEIVING;
	s_ota.manifest.image_size = size;
	s_ota.manifest.image_crc32 = crc;
	s_ota.manifest.received_size = 0UL;
	ota_manifest_finalize(&s_ota.manifest);
	if (!ota_store_write_manifest(&s_ota.manifest)) {
		ota_reply(reply, ctx, "ERR OTA MANIFEST");
		return true;
	}

	s_ota.active = true;
	s_ota.staged = false;
	s_ota.running_crc = ota_crc32_begin();
	ota_reply(reply, ctx, "OK OTA BEGIN");
	return true;
}

static bool ota_data(char *line, ota_reply_fn reply, void *ctx)
{
	uint32_t off;
	uint32_t len;
	uint32_t crc;
	const char *hex;
	uint32_t chunk_crc;

	if (!s_ota.active ||
	    !ota_parse_u32_dec(ota_arg_value(line, "off"), &off) ||
	    !ota_parse_u32_dec(ota_arg_value(line, "len"), &len) ||
	    !ota_parse_u32_hex(ota_arg_value(line, "crc"), &crc)) {
		ota_reply(reply, ctx, "ERR OTA DATA");
		return true;
	}

	hex = ota_arg_value(line, "hex");
	if (hex == 0 || len == 0UL || len > OTA_TRANSFER_MAX_DATA_LEN ||
	    off != s_ota.manifest.received_size ||
	    off > s_ota.manifest.image_size ||
	    len > s_ota.manifest.image_size - off ||
	    !ota_decode_hex(hex, s_ota_chunk, len)) {
		ota_reply(reply, ctx, "ERR OTA DATA");
		return true;
	}

	chunk_crc = ota_crc32_compute(s_ota_chunk, len);
	if (chunk_crc != crc) {
		ota_reply(reply, ctx, "ERR OTA CRC");
		return true;
	}
	if (!ota_store_write_image(off, s_ota_chunk, len)) {
		ota_reply(reply, ctx, "ERR OTA WRITE");
		return true;
	}

	s_ota.running_crc = ota_crc32_update(s_ota.running_crc, s_ota_chunk, len);
	s_ota.manifest.received_size += len;
	ota_manifest_finalize(&s_ota.manifest);

	ota_reply(reply, ctx, "OK OTA DATA");
	return true;
}

static bool ota_end(ota_reply_fn reply, void *ctx)
{
	uint32_t final_crc;

	if (!s_ota.active || s_ota.manifest.received_size != s_ota.manifest.image_size) {
		ota_reply(reply, ctx, "ERR OTA END");
		return true;
	}

	final_crc = ota_crc32_finish(s_ota.running_crc);
	if (final_crc != s_ota.manifest.image_crc32) {
		ota_manifest_set_state(&s_ota.manifest, OTA_MANIFEST_STATE_ERROR);
		(void)ota_store_write_manifest(&s_ota.manifest);
		ota_reply(reply, ctx, "ERR OTA IMAGECRC");
		return true;
	}

	ota_manifest_set_state(&s_ota.manifest, OTA_MANIFEST_STATE_STAGED);
	if (!ota_store_write_manifest(&s_ota.manifest)) {
		ota_reply(reply, ctx, "ERR OTA MANIFEST");
		return true;
	}

	s_ota.staged = true;
	ota_reply(reply, ctx, "OK OTA END");
	return true;
}

static bool ota_apply(ota_reply_fn reply, void *ctx)
{
	if (!ota_bootloader_is_valid()) {
		ota_reply(reply, ctx, "ERR OTA BOOT");
		return true;
	}
	if (!s_ota.staged || !ota_manifest_is_valid(&s_ota.manifest)) {
		ota_reply(reply, ctx, "ERR OTA APPLY");
		return true;
	}

	ota_manifest_set_state(&s_ota.manifest, OTA_MANIFEST_STATE_PENDING);
	if (!ota_store_write_manifest(&s_ota.manifest)) {
		ota_reply(reply, ctx, "ERR OTA MANIFEST");
		return true;
	}

	ota_reply(reply, ctx, "OK OTA APPLY");
	HAL_Delay(OTA_APPLY_REPLY_DRAIN_MS);
	ota_jump_to_bootloader();
	return true;
}

bool ota_command_process(char *line, ota_reply_fn reply, void *ctx)
{
	char status[64];

	if (line == 0)
		return false;
	if (strncmp(line, "OTA BEGIN ", 10) == 0)
		return ota_begin(line, reply, ctx);
	if (strncmp(line, "OTA DATA ", 9) == 0)
		return ota_data(line, reply, ctx);
	if (strcmp(line, "OTA END") == 0)
		return ota_end(reply, ctx);
	if (strcmp(line, "OTA APPLY") == 0)
		return ota_apply(reply, ctx);
	if (strcmp(line, "OTA ABORT") == 0) {
		memset(&s_ota, 0, sizeof(s_ota));
		if (ota_flash_ready())
			(void)ota_store_clear_manifest();
		ota_reply(reply, ctx, "OK OTA ABORT");
		return true;
	}
	if (strcmp(line, "OTA STATUS?") == 0) {
		(void)snprintf(status, sizeof(status), "OK OTA %s %lu/%lu",
			       s_ota.staged ? "staged" : (s_ota.active ? "receiving" : "idle"),
			       (unsigned long)s_ota.manifest.received_size,
			       (unsigned long)s_ota.manifest.image_size);
		ota_reply(reply, ctx, status);
		return true;
	}
	return false;
}
