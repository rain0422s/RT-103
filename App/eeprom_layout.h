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
#define EEPROM_CONFIG_VERSION 5
#define EEPROM_TOTAL_SIZE     256

#define EEPROM_OFFSET_CONFIG  0   /* legacy eeprom_config_t migration source */
#define EEPROM_OFFSET_CALIB   32  /* lis2dh12_calib_t (zero-g offset in LSB) */
#define EEPROM_OFFSET_STATS   64  /* config journal starts here */
#define EEPROM_CONFIG_SIZE    32
#define EEPROM_CONFIG_RECORD_MAGIC 0xC05A
#define EEPROM_CONFIG_RECORD_SIZE  48
#define EEPROM_CONFIG_SLOT_COUNT   2
#define EEPROM_OFFSET_CONFIG_SLOT0 64
#define EEPROM_OFFSET_CONFIG_SLOT1 112
#define EEPROM_OFFSET_TEST         224

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
	uint8_t  pose_flags;            /* bit0: roll/pitch neutral pose is valid */
	int8_t   pose_roll_sign;        /* +1/-1: physical right-down roll direction */
	uint8_t  reserved2;
	int16_t  pose_roll_zero_x10;    /* neutral physical roll, degrees * 10 */
	int16_t  pose_pitch_zero_x10;   /* neutral physical pitch, degrees * 10 */
	uint16_t battery_gain_permyriad; /* voltage gain, 10000 = 1.0000 */
	int16_t  battery_offset_mv;     /* voltage offset after gain */
	uint8_t  reserved3[4];          /* keep struct exactly within EEPROM_OFFSET_CALIB */
} eeprom_config_t;

typedef char eeprom_config_must_fit[(sizeof(eeprom_config_t) == EEPROM_CONFIG_SIZE) ? 1 : -1];
typedef char eeprom_config_journal_must_fit[
	(EEPROM_OFFSET_CONFIG_SLOT1 + EEPROM_CONFIG_RECORD_SIZE <= EEPROM_TOTAL_SIZE) ? 1 : -1
];

#endif /* __EEPROM_LAYOUT_H */
