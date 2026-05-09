#ifndef __STORAGE_H
#define __STORAGE_H
#include "w25qxx.h"
#include "at24cxx.h"
#include "i2cdev.h"
#include "eeprom_layout.h"
#include "oled.h"
#include "string.h"
typedef struct {
    int   i[2];
    float f;
} object_t;
typedef bool (*storage_menu_register_fn_t)(const char *name, void (*on_long_press)(u8g2_t *pu8g2));

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

/** LIS2DH12 calibration (zero-g offset). Load/save to EEPROM_OFFSET_CALIB. */
bool storage_load_lis2dh12_calib(lis2dh12_calib_t *out);
bool storage_save_lis2dh12_calib(const lis2dh12_calib_t *cal);

/** Read boot counter from mounted LittleFS. Returns false if FS not mounted. */
bool storage_get_boot_count(uint32_t *out_boot_count);
/** Run LIS2DH12 calibration and save to EEPROM through sensor path. */
void storage_run_lis2dh12_calibration_save(void);
/** Storage-backed UI long-press action: show boot counter. */
void storage_menu_action_show_boot_count(u8g2_t *pu8g2);
/** Storage-backed UI long-press action: run calibration and save. */
void storage_menu_action_run_calibration(u8g2_t *pu8g2);
/** Register storage-owned menu entries and callbacks. */
void storage_menu_register_items(storage_menu_register_fn_t reg_fn);
#endif