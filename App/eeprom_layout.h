/**
 * @file eeprom_layout.h
 * @brief EEPROM (M24C02) address layout and persistent config struct for EMS.
 * Use storage_load_config() / storage_save_config() to read/write.
 */
#ifndef __EEPROM_LAYOUT_H
#define __EEPROM_LAYOUT_H

#include <stdint.h>

/* --- Layout (AT24C02 = 256 bytes) --- */
#define EEPROM_MAGIC          0xE5A1
#define EEPROM_CONFIG_VERSION 1

#define EEPROM_OFFSET_CONFIG  0   /* eeprom_config_t (see below) */
#define EEPROM_OFFSET_CALIB   32  /* future: ADC/sensor calibration */
#define EEPROM_OFFSET_STATS   64  /* future: power-on count, run time */

/**
 * Persistent config saved to EEPROM.
 * Add fields as needed; keep total size small to fit one EEPROM page (8 bytes for AT24C02).
 */
typedef struct {
	uint16_t magic;      /* EEPROM_MAGIC if valid */
	uint8_t  version;   /* EEPROM_CONFIG_VERSION */
	int8_t   ui_select;  /* last selected menu/screen index (e.g. U8G2 list) */
	uint8_t  reserved;  /* alignment / future use */
} eeprom_config_t;

#endif /* __EEPROM_LAYOUT_H */
