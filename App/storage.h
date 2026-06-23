#ifndef __STORAGE_H
#define __STORAGE_H
#include "w25qxx.h"
#include "at24cxx.h"
#include "i2cdev.h"
#include "eeprom_layout.h"
#include "ui_menu_mode.h"
#include "oled.h"
#include "string.h"
#include <stdint.h>

#define STORAGE_BATTERY_HISTORY_MAX_POINTS 288U
#define STORAGE_BATTERY_HISTORY_INTERVAL_SEC 300U
#define STORAGE_BATTERY_HISTORY_WINDOW_SEC 86400U

typedef struct {
    int   i[2];
    float f;
} object_t;

typedef struct {
	uint32_t uptime_s;
	uint16_t millivolts;
	uint8_t percent;
	uint8_t reserved;
} storage_battery_history_point_t;

typedef bool (*storage_menu_register_fn_t)(const char *name, void (*on_long_press)(u8g2_t *pu8g2),
					   uint8_t mode_mask);

/** Flash (W25Qxx): idempotent init + read_id check (0xEF); only when present can LittleFS/flash ops be used. */
void storage_flash_init(void);
bool storage_flash_is_present(void);
/** EEPROM: idempotent init + device detect; only when present can load/save be used. */
void storage_eeprom_init(void);
bool storage_eeprom_is_present(void);
bool storage_eeprom_test(void);
/** 开机后延时 30 秒再执行 LittleFS 与 EEPROM 初始化，执行完自删（FreeRTOS 任务入口） */
void storage_init_task(void *arg);
/** True when flash filesystem is mounted and system data path is ready. */
bool storage_system_ready(void);
/** Shutdown preparation: unmount LittleFS if flash is present. */
void storage_prepare_shutdown(void);

/** EEPROM config: call after storage_eeprom_init; load/save only work when storage_eeprom_is_present(). */
bool storage_load_config(eeprom_config_t *out);
bool storage_save_config(const eeprom_config_t *cfg);
void storage_config_set_defaults(eeprom_config_t *out);
void storage_config_sanitize(eeprom_config_t *cfg);
bool storage_uptime_checkpoint_load(uint32_t *out_seconds);
bool storage_uptime_checkpoint_save(uint32_t seconds);

/** LIS2DH12 calibration (zero-g offset). Load/save to EEPROM_OFFSET_CALIB. */
bool storage_load_lis2dh12_calib(lis2dh12_calib_t *out);
bool storage_save_lis2dh12_calib(const lis2dh12_calib_t *cal);

/** Read boot counter from mounted LittleFS. Returns false if FS not mounted. */
bool storage_get_boot_count(uint32_t *out_boot_count);
/** Load persisted battery history into RAM. Safe to call repeatedly after LittleFS is mounted. */
bool storage_battery_history_init(void);
/** Record one battery point if the configured interval has elapsed. */
bool storage_battery_history_record(uint16_t millivolts, uint8_t percent);
/** Return persisted battery history ordered from oldest to newest. */
uint16_t storage_battery_history_get(storage_battery_history_point_t *out_points,
				     uint16_t max_points);
/** Clear persisted and in-RAM battery history. */
bool storage_battery_history_clear(void);
/** Run LIS2DH12 calibration and save to EEPROM through sensor path. */
bool storage_run_lis2dh12_calibration_save(void);
/** Storage-backed UI long-press action: show boot counter. */
void storage_menu_action_show_boot_count(u8g2_t *pu8g2);
/** Storage-backed UI long-press action: run calibration and save. */
void storage_menu_action_run_calibration(u8g2_t *pu8g2);
/** Register storage-owned menu entries and callbacks. */
void storage_menu_register_items(storage_menu_register_fn_t reg_fn);
#endif
