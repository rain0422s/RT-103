#ifndef __STORAGE_H
#define __STORAGE_H
#include "w25qxx.h"
#include "at24cxx.h"
#include "i2cdev.h"
#include "eeprom_layout.h"
#include "string.h"
typedef struct {
    int   i[2];
    float f;
} object_t;

/** Flash (W25Qxx): idempotent init + read_id check (0xEF); only when present can LittleFS/flash ops be used. */
void storage_flash_init(void);
bool storage_flash_is_present(void);
/** EEPROM: idempotent init + device detect; only when present can load/save be used. */
void storage_eeprom_init(void);
bool storage_eeprom_is_present(void);
bool storage_eeprom_test(void);
/** 开机后延时 30 秒再执行 LittleFS 与 EEPROM 初始化，执行完自删（FreeRTOS 任务入口） */
void storage_init_task(void *arg);

/** EEPROM config: call after storage_eeprom_init; load/save only work when storage_eeprom_is_present(). */
bool storage_load_config(eeprom_config_t *out);
bool storage_save_config(const eeprom_config_t *cfg);

/** LIS2DH12 calibration (zero-g offset). Load/save to EEPROM_OFFSET_CALIB. */
bool storage_load_lis2dh12_calib(lis2dh12_calib_t *out);
bool storage_save_lis2dh12_calib(const lis2dh12_calib_t *cal);
#endif