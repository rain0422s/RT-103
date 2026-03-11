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

void spi_flash_test();
void i2c_eeprom_test();
/** 开机后延时 30 秒再执行 LittleFS 与 EEPROM 初始化，执行完自删（FreeRTOS 任务入口） */
void storage_init_task(void *arg);

/** EEPROM config: call after EEPROM bus is ready (e.g. after storage_eeprom_init or storage_init_task). */
void storage_eeprom_init(void);
bool storage_load_config(eeprom_config_t *out);
bool storage_save_config(const eeprom_config_t *cfg);
#endif