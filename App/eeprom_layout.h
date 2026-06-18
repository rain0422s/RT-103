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
#define EEPROM_CONFIG_VERSION 4

#define EEPROM_OFFSET_CONFIG  0   /* eeprom_config_t (see below) */
#define EEPROM_OFFSET_CALIB   32  /* lis2dh12_calib_t (zero-g offset in LSB) */
#define EEPROM_OFFSET_STATS   64  /* future: power-on count, run time */

#define EEPROM_CALIB_MAGIC    0xCA1B  /* valid LIS2DH12 calib */

typedef enum {
	CLOCK_DISPLAY_MODE_RTC = 0,
	CLOCK_DISPLAY_MODE_UPTIME = 1,
} clock_display_mode_t;

/** LIS2DH12 zero-g offset in raw LSB (2g HR: 1g ≈ 1024 LSB). Apply: calibrated = raw - offset. */
typedef struct {
	uint16_t magic;   /* EEPROM_CALIB_MAGIC if valid */
	int16_t  offset_x;
	int16_t  offset_y;
	int16_t  offset_z;
} lis2dh12_calib_t;

/**
 * Persistent config saved to EEPROM.
 * Keep total size below EEPROM_OFFSET_CALIB.
 */
typedef struct {
	uint16_t magic;                /* EEPROM_MAGIC if valid */
	uint8_t  version;              /* EEPROM_CONFIG_VERSION */
	int8_t   ui_select;            /* last selected secondary-menu index */
	int8_t   rtc_calib_sec_per_day; /* software RTC correction, seconds/day */
	uint8_t  reserved[3];          /* align following 32-bit field */
	uint32_t rtc_calib_anchor_raw;  /* raw RTC seconds at last calib/time set */
	uint32_t rtc_uptime_seconds;    /* persisted uptime clock when RTC is not set */
	uint8_t  clock_display_mode;    /* clock_display_mode_t */
	uint8_t  reserved2[3];          /* keep struct aligned and below EEPROM_OFFSET_CALIB */
} eeprom_config_t;

#endif /* __EEPROM_LAYOUT_H */
