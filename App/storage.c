#include "storage.h"
#include "power_key.h"
#include "rtc_clock.h"
#include "sensor.h"
#include "display.h"
#include "lfs.h"
#include "lfs_port.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include <stdio.h>

/* Shared EEPROM client for config load/save and test */
static soft_i2c_t s_eeprom_bus = {
	.SCL_Port = GPIOB,
	.SCL_Pin  = GPIO_PIN_0,
	.SDA_Port = GPIOC,
	.SDA_Pin  = GPIO_PIN_5,
	.Interval = 6,
};
/* M24C02 EEPROM client: static storage (BSS), set at load time — no heap, no fragmentation. */
static struct i2c_cli s_m24c02;
static bool s_eeprom_inited;   /* init (bus + client) has been run */
static bool s_eeprom_present;  /* device responded on I2C; only then allow load/save */

static void storage_config_set_defaults(eeprom_config_t *out)
{
	if (out == NULL)
		return;
	memset(out, 0, sizeof(*out));
	out->magic = EEPROM_MAGIC;
	out->version = EEPROM_CONFIG_VERSION;
	out->ui_select = 0;
	out->rtc_calib_sec_per_day = 0;
	out->rtc_calib_anchor_raw = 0;
	out->rtc_uptime_seconds = 0;
	out->clock_display_mode = CLOCK_DISPLAY_MODE_UPTIME;
}

static void storage_config_sanitize(eeprom_config_t *cfg)
{
	if (cfg == NULL)
		return;
	if (cfg->clock_display_mode != CLOCK_DISPLAY_MODE_RTC &&
	    cfg->clock_display_mode != CLOCK_DISPLAY_MODE_UPTIME) {
		cfg->clock_display_mode = CLOCK_DISPLAY_MODE_UPTIME;
	}
}

/** Idempotent: init I2C bus and M24C02 client, then detect device. If not present, EEPROM load/save APIs return false. */
void storage_eeprom_init(void)
{
	if (s_eeprom_inited)
		return;
	s_m24c02.bus = &s_eeprom_bus;
	s_m24c02.drv = &swi2c_drv;
	s_m24c02.dev = AT24CXX_DEV;
	s_m24c02.ops = I2C_DEV_7BIT | I2C_REG_8BIT;
	swi2c_drv.init(&s_eeprom_bus);
	i2cdrv_detector(&s_eeprom_bus, s_m24c02.drv);
	s_eeprom_present = i2cdev_check(&s_m24c02);
	s_eeprom_inited = true;
}

bool storage_eeprom_is_present(void)
{
	return s_eeprom_inited && s_eeprom_present;
}

bool storage_load_config(eeprom_config_t *out)
{
	if (!out || !s_eeprom_inited || !s_eeprom_present)
		return false;
	if (!at24cxx_read(s_m24c02, EEPROM_OFFSET_CONFIG, (uint8_t *)out, sizeof(eeprom_config_t)))
		return false;
	if (out->magic != EEPROM_MAGIC) {
		storage_config_set_defaults(out);
		return false; /* no valid data, use defaults already set */
	}
	if (out->version == 2u) {
		out->version = EEPROM_CONFIG_VERSION;
		out->rtc_uptime_seconds = 0;
		out->clock_display_mode = CLOCK_DISPLAY_MODE_UPTIME;
		return true;
	}
	if (out->version == 3u) {
		out->version = EEPROM_CONFIG_VERSION;
		out->clock_display_mode = CLOCK_DISPLAY_MODE_UPTIME;
		storage_config_sanitize(out);
		return true;
	}
	if (out->version != EEPROM_CONFIG_VERSION) {
		storage_config_set_defaults(out);
		return false; /* no valid data, use defaults already set */
	}
	storage_config_sanitize(out);
	return true;
}

bool storage_save_config(const eeprom_config_t *cfg)
{
	if (!cfg || !s_eeprom_inited || !s_eeprom_present)
		return false;
	return at24cxx_write(s_m24c02, EEPROM_OFFSET_CONFIG, (uint8_t *)cfg, sizeof(eeprom_config_t));
}

bool storage_load_lis2dh12_calib(lis2dh12_calib_t *out)
{
	if (!out || !s_eeprom_inited || !s_eeprom_present)
		return false;
	if (!at24cxx_read(s_m24c02, EEPROM_OFFSET_CALIB, (uint8_t *)out, sizeof(lis2dh12_calib_t)))
		return false;
	if (out->magic != EEPROM_CALIB_MAGIC) {
		out->magic   = EEPROM_CALIB_MAGIC;
		out->offset_x = out->offset_y = out->offset_z = 0;
		return false;
	}
	return true;
}

bool storage_save_lis2dh12_calib(const lis2dh12_calib_t *cal)
{
	if (!cal || !s_eeprom_inited || !s_eeprom_present)
		return false;
	return at24cxx_write(s_m24c02, EEPROM_OFFSET_CALIB, (uint8_t *)cal, sizeof(lis2dh12_calib_t));
}

bool storage_get_boot_count(uint32_t *out_boot_count)
{
	if (out_boot_count == NULL)
		return false;
	if (lfs_get() == NULL)
		return false;
	*out_boot_count = lfs_get_boot_count();
	return true;
}

bool storage_run_lis2dh12_calibration_save(void)
{
	return sensor_lis2dh12_calibrate_and_save();
}

void storage_menu_action_show_boot_count(u8g2_t *pu8g2)
{
	char line[24] = "boot:--";
	uint32_t boot_count = 0;

	if (storage_get_boot_count(&boot_count)) {
		(void)snprintf(line, sizeof(line), "boot:%lu", (unsigned long)boot_count);
	}
	display_show_text_feedback(pu8g2, "Boot Count", line, 0);
}

void storage_menu_action_run_calibration(u8g2_t *pu8g2)
{
	if (storage_run_lis2dh12_calibration_save())
		display_show_text_feedback(pu8g2, "Calibration", "Saved to EEPROM", 0);
	else
		display_show_text_feedback(pu8g2, "Calibration", "Failed", 0);
}

void storage_menu_register_items(storage_menu_register_fn_t reg_fn)
{
	if (reg_fn == NULL)
		return;
	(void)reg_fn("boot", storage_menu_action_show_boot_count, UI_MENU_MODE_ALL);
	(void)reg_fn("calib", storage_menu_action_run_calibration, UI_MENU_MODE_ALL);
}

/** EEPROM test: backup one small block in the stats area, write a pattern, verify, then restore backup. */
bool storage_eeprom_test(void)
{
	uint8_t backup[8];
	uint8_t pattern[8] = { 'E', 'E', 'P', 'R', 0x12, 0x34, 0x56, 0x78 };
	uint8_t verify[8];

	if (!s_eeprom_inited || !s_eeprom_present) {
		DBG_PRINTF("M24C02 EEPROM not ready\n");
		return false;
	}

	if (!at24cxx_read(s_m24c02, EEPROM_OFFSET_STATS, backup, sizeof(backup))) {
		DBG_PRINTF("M24C02 EEPROM test: backup read failed\n");
		return false;
	}

	if (!at24cxx_write(s_m24c02, EEPROM_OFFSET_STATS, pattern, sizeof(pattern))) {
		DBG_PRINTF("M24C02 EEPROM test: pattern write failed\n");
		return false;
	}

	if (!at24cxx_read(s_m24c02, EEPROM_OFFSET_STATS, verify, sizeof(verify))) {
		DBG_PRINTF("M24C02 EEPROM test: verify read failed\n");
		(void)at24cxx_write(s_m24c02, EEPROM_OFFSET_STATS, backup, sizeof(backup));
		return false;
	}

	if (memcmp(pattern, verify, sizeof(pattern)) != 0) {
		DBG_PRINTF("M24C02 EEPROM test: verify mismatch\n");
		(void)at24cxx_write(s_m24c02, EEPROM_OFFSET_STATS, backup, sizeof(backup));
		return false;
	}

	if (!at24cxx_write(s_m24c02, EEPROM_OFFSET_STATS, backup, sizeof(backup))) {
		DBG_PRINTF("M24C02 EEPROM test: restore failed\n");
		return false;
	}

	DBG_PRINTF("M24C02 EEPROM OK\n");
	return true;
}

/* ==================== W25Qxx SPI Flash init & presence ==================== */

/* W25Q16JV PDF: "Read JEDEC ID (9Fh)" / Identification table. 9Fh returns 3 bytes: id[0]=MF, id[1]=Memory Type, id[2]=Capacity; Device ID = 4015h (id[1]:id[2]). */
#define W25QXX_MANUFACTURER_ID  0xEFU   /* Winbond */
#define W25Q16_DEVICE_ID_H      0x40U   /* Memory Type */
#define W25Q16_DEVICE_ID_L      0x15U   /* Capacity 16Mbit → Device ID 4015h */

static bool s_flash_inited;
static bool s_flash_present;

/** Idempotent: reset W25Qxx, read JEDEC ID (9Fh); if MF=0xEF and Device ID=4015h (0x40,0x15) for W25Q16JV, flash is present. */
void storage_flash_init(void)
{
	if (s_flash_inited)
		return;
	w25qxx_reset();
	uint8_t id[3];
	w25qxx_read_jedec_id(id);
	s_flash_present = (id[0] == W25QXX_MANUFACTURER_ID && id[1] == W25Q16_DEVICE_ID_H && id[2] == W25Q16_DEVICE_ID_L);
	s_flash_inited = true;
	if (s_flash_present) {
		DBG_PRINTF("W25Q16 flash OK (ID EF 40 15)\n");
	}
}

bool storage_flash_is_present(void)
{
	return s_flash_inited && s_flash_present;
}

bool storage_system_ready(void)
{
	return storage_flash_is_present() && (lfs_get() != NULL);
}

void storage_prepare_shutdown(void)
{
	storage_eeprom_init();
	if (storage_eeprom_is_present()) {
		eeprom_config_t cfg;

		storage_config_set_defaults(&cfg);
		(void)storage_load_config(&cfg);
		cfg.magic = EEPROM_MAGIC;
		cfg.version = EEPROM_CONFIG_VERSION;
		cfg.rtc_uptime_seconds = rtc_clock_get_uptime_seconds(&cfg);
		(void)storage_save_config(&cfg);
	}
	if (storage_flash_is_present()) {
		lfs_unmount_fs();
	}
}

void storage_init_task(void *arg)
{

	(void)arg;

	storage_eeprom_init();   /* init bus + detect M24C02; if not present, load/save APIs return false */
	/* Mount flash FS ASAP so UI can enter system quickly and read boot_count. */
	storage_flash_init();   /* reset + read_id; if present (0xEF), allow LittleFS */
	if (storage_flash_is_present())
		lfs_first_run();
	// storage_eeprom_test();
	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
