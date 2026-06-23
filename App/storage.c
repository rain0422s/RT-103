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
#include <stddef.h>
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

#define BATTERY_HISTORY_FILE_PATH      "battery_hist"
#define BATTERY_HISTORY_TEMP_FILE_PATH "battery_hist.tmp"
#define UPTIME_CHECKPOINT_FILE_PATH      "uptime_ckpt"
#define UPTIME_CHECKPOINT_TEMP_FILE_PATH "uptime_ckpt.tmp"
#define BATTERY_HISTORY_MAGIC     0x42544831u
#define BATTERY_HISTORY_VERSION   2u
#define BATTERY_HISTORY_LEGACY_VERSION 1u
#define BATTERY_HISTORY_LEGACY_MAX_POINTS 96U
#define UPTIME_CHECKPOINT_MAGIC   0x55505431u
#define UPTIME_CHECKPOINT_VERSION 1u
#define STORAGE_FLASH_BOOT_RETRY_COUNT 5U
#define STORAGE_FLASH_BOOT_RETRY_MS    200U

typedef struct {
	uint32_t magic;
	uint16_t version;
	uint16_t count;
	uint16_t next;
	uint16_t reserved;
	storage_battery_history_point_t points[STORAGE_BATTERY_HISTORY_MAX_POINTS];
} battery_history_file_t;

typedef struct {
	uint32_t magic;
	uint16_t version;
	uint16_t count;
	uint16_t next;
	uint16_t reserved;
	storage_battery_history_point_t points[BATTERY_HISTORY_LEGACY_MAX_POINTS];
} battery_history_legacy_file_t;

typedef struct {
	uint32_t magic;
	uint16_t version;
	uint16_t reserved;
	uint32_t seconds;
} uptime_checkpoint_file_t;

typedef struct {
	uint16_t magic;
	uint8_t version;
	uint8_t reserved;
	uint32_t sequence;
	uint32_t crc;
	eeprom_config_t config;
	uint8_t padding[4];
} eeprom_config_record_t;

typedef char eeprom_record_must_fit[
	(sizeof(eeprom_config_record_t) == EEPROM_CONFIG_RECORD_SIZE) ? 1 : -1
];

static battery_history_file_t s_battery_history;
static bool s_battery_history_loaded;
static uint32_t s_battery_history_last_uptime_s;
static bool s_battery_history_has_last;

void storage_config_set_defaults(eeprom_config_t *out)
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
	out->pose_flags = 0;
	out->pose_roll_sign = 1;
	out->pose_roll_zero_x10 = 0;
	out->pose_pitch_zero_x10 = 0;
	out->battery_gain_permyriad = 10000U;
	out->battery_offset_mv = 0;
}

void storage_config_sanitize(eeprom_config_t *cfg)
{
	if (cfg == NULL)
		return;
	if (cfg->clock_display_mode != CLOCK_DISPLAY_MODE_RTC &&
	    cfg->clock_display_mode != CLOCK_DISPLAY_MODE_UPTIME) {
		cfg->clock_display_mode = CLOCK_DISPLAY_MODE_UPTIME;
	}
	if (cfg->pose_roll_sign != -1 && cfg->pose_roll_sign != 1)
		cfg->pose_roll_sign = 1;
	cfg->pose_flags &= 0x01U;
	if (cfg->battery_gain_permyriad < 8000U ||
	    cfg->battery_gain_permyriad > 12000U) {
		cfg->battery_gain_permyriad = 10000U;
	}
	if (cfg->battery_offset_mv < -500 || cfg->battery_offset_mv > 500)
		cfg->battery_offset_mv = 0;
}

static uint32_t storage_crc32_update(uint32_t crc, const void *data, size_t len)
{
	const uint8_t *p = (const uint8_t *)data;

	while (len-- > 0U) {
		crc ^= (uint32_t)(*p++);
		for (uint8_t i = 0; i < 8U; i++) {
			if ((crc & 1U) != 0U)
				crc = (crc >> 1) ^ 0xEDB88320UL;
			else
				crc >>= 1;
		}
	}
	return crc;
}

static uint32_t storage_config_record_crc(const eeprom_config_record_t *record)
{
	uint32_t crc = 0xFFFFFFFFUL;

	crc = storage_crc32_update(crc, &record->magic, sizeof(record->magic));
	crc = storage_crc32_update(crc, &record->version, sizeof(record->version));
	crc = storage_crc32_update(crc, &record->reserved, sizeof(record->reserved));
	crc = storage_crc32_update(crc, &record->sequence, sizeof(record->sequence));
	crc = storage_crc32_update(crc, &record->config, sizeof(record->config));
	return ~crc;
}

static bool storage_config_record_valid(const eeprom_config_record_t *record)
{
	if (record == NULL)
		return false;
	if (record->magic != EEPROM_CONFIG_RECORD_MAGIC ||
	    record->version != EEPROM_CONFIG_VERSION ||
	    record->config.magic != EEPROM_MAGIC ||
	    record->config.version != EEPROM_CONFIG_VERSION) {
		return false;
	}
	return record->crc == storage_config_record_crc(record);
}

static bool storage_read_config_record(uint16_t offset, eeprom_config_record_t *record)
{
	if (record == NULL)
		return false;
	if (!at24cxx_read(s_m24c02, offset, (uint8_t *)record, sizeof(*record)))
		return false;
	return storage_config_record_valid(record);
}

static uint16_t storage_next_config_slot(uint16_t latest_offset)
{
	return (latest_offset == EEPROM_OFFSET_CONFIG_SLOT0) ?
	       EEPROM_OFFSET_CONFIG_SLOT1 : EEPROM_OFFSET_CONFIG_SLOT0;
}

static bool storage_select_latest_config_record(eeprom_config_record_t *record,
						uint16_t *out_offset)
{
	eeprom_config_record_t r0;
	eeprom_config_record_t r1;
	const bool valid0 = storage_read_config_record(EEPROM_OFFSET_CONFIG_SLOT0, &r0);
	const bool valid1 = storage_read_config_record(EEPROM_OFFSET_CONFIG_SLOT1, &r1);

	if (!valid0 && !valid1)
		return false;
	if (valid0 && (!valid1 || r0.sequence >= r1.sequence)) {
		if (record != NULL)
			*record = r0;
		if (out_offset != NULL)
			*out_offset = EEPROM_OFFSET_CONFIG_SLOT0;
		return true;
	}
	if (record != NULL)
		*record = r1;
	if (out_offset != NULL)
		*out_offset = EEPROM_OFFSET_CONFIG_SLOT1;
	return true;
}

static bool storage_upgrade_loaded_config(eeprom_config_t *out)
{
	if (out == NULL)
		return false;
	if (out->magic != EEPROM_MAGIC) {
		storage_config_set_defaults(out);
		return false;
	}
	if (out->version == 2u) {
		out->version = EEPROM_CONFIG_VERSION;
		out->rtc_uptime_seconds = 0;
		out->clock_display_mode = CLOCK_DISPLAY_MODE_UPTIME;
		out->pose_flags = 0;
		out->pose_roll_sign = 1;
		out->pose_roll_zero_x10 = 0;
		out->pose_pitch_zero_x10 = 0;
		out->battery_gain_permyriad = 10000U;
		out->battery_offset_mv = 0;
		storage_config_sanitize(out);
		return true;
	}
	if (out->version == 3u) {
		out->version = EEPROM_CONFIG_VERSION;
		out->clock_display_mode = CLOCK_DISPLAY_MODE_UPTIME;
		out->pose_flags = 0;
		out->pose_roll_sign = 1;
		out->pose_roll_zero_x10 = 0;
		out->pose_pitch_zero_x10 = 0;
		out->battery_gain_permyriad = 10000U;
		out->battery_offset_mv = 0;
		storage_config_sanitize(out);
		return true;
	}
	if (out->version == 4u) {
		out->version = EEPROM_CONFIG_VERSION;
		out->pose_flags = 0;
		out->pose_roll_sign = 1;
		out->pose_roll_zero_x10 = 0;
		out->pose_pitch_zero_x10 = 0;
		out->battery_gain_permyriad = 10000U;
		out->battery_offset_mv = 0;
		storage_config_sanitize(out);
		return true;
	}
	if (out->version != EEPROM_CONFIG_VERSION) {
		storage_config_set_defaults(out);
		return false;
	}
	storage_config_sanitize(out);
	return true;
}

static void storage_battery_history_set_empty(void)
{
	memset(&s_battery_history, 0, sizeof(s_battery_history));
	s_battery_history.magic = BATTERY_HISTORY_MAGIC;
	s_battery_history.version = BATTERY_HISTORY_VERSION;
	s_battery_history.count = 0;
	s_battery_history.next = 0;
	s_battery_history_loaded = true;
	s_battery_history_last_uptime_s = 0;
	s_battery_history_has_last = false;
}

static bool storage_battery_history_valid(const battery_history_file_t *hist)
{
	return hist != NULL &&
	       hist->magic == BATTERY_HISTORY_MAGIC &&
	       hist->version == BATTERY_HISTORY_VERSION &&
	       hist->count <= STORAGE_BATTERY_HISTORY_MAX_POINTS &&
	       hist->next < STORAGE_BATTERY_HISTORY_MAX_POINTS;
}

static bool storage_battery_history_legacy_valid(const battery_history_legacy_file_t *hist)
{
	return hist != NULL &&
	       hist->magic == BATTERY_HISTORY_MAGIC &&
	       hist->version == BATTERY_HISTORY_LEGACY_VERSION &&
	       hist->count <= BATTERY_HISTORY_LEGACY_MAX_POINTS &&
	       hist->next < BATTERY_HISTORY_LEGACY_MAX_POINTS;
}

static void storage_battery_history_migrate_legacy(const battery_history_legacy_file_t *old)
{
	battery_history_legacy_file_t copy;
	uint16_t first;

	if (!storage_battery_history_legacy_valid(old)) {
		storage_battery_history_set_empty();
		return;
	}
	copy = *old;
	old = &copy;
	storage_battery_history_set_empty();
	if (old->count == 0U)
		return;

	if (old->count >= BATTERY_HISTORY_LEGACY_MAX_POINTS)
		first = old->next;
	else
		first = 0U;

	for (uint16_t i = 0; i < old->count; i++) {
		const uint16_t idx = (uint16_t)((first + i) %
					       BATTERY_HISTORY_LEGACY_MAX_POINTS);

		s_battery_history.points[i] = old->points[idx];
	}
	s_battery_history.count = old->count;
	s_battery_history.next = old->count;
	if (s_battery_history.next >= STORAGE_BATTERY_HISTORY_MAX_POINTS)
		s_battery_history.next = 0U;
	s_battery_history_last_uptime_s =
	    s_battery_history.points[s_battery_history.count - 1U].uptime_s;
	s_battery_history_has_last = true;
}

static bool storage_battery_history_save_file(void)
{
	lfs_t *lfs = lfs_get();
	lfs_file_t file;
	lfs_ssize_t written;

	if (!lfs_is_ready() || lfs == NULL || !s_battery_history_loaded)
		return false;

	(void)lfs_remove(lfs, BATTERY_HISTORY_TEMP_FILE_PATH);
	if (lfs_file_open(lfs, &file, BATTERY_HISTORY_TEMP_FILE_PATH,
			  LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) < 0) {
		DBG_PRINTF("[battery_hist] open tmp failed\n");
		return false;
	}
	written = lfs_file_write(lfs, &file, &s_battery_history,
				 sizeof(s_battery_history));
	if (lfs_file_close(lfs, &file) < 0) {
		DBG_PRINTF("[battery_hist] close tmp failed\n");
		(void)lfs_remove(lfs, BATTERY_HISTORY_TEMP_FILE_PATH);
		return false;
	}
	if (written != (lfs_ssize_t)sizeof(s_battery_history)) {
		DBG_PRINTF("[battery_hist] write short=%ld need=%lu\n",
			   (long)written,
			   (unsigned long)sizeof(s_battery_history));
		(void)lfs_remove(lfs, BATTERY_HISTORY_TEMP_FILE_PATH);
		return false;
	}
	if (lfs_rename(lfs, BATTERY_HISTORY_TEMP_FILE_PATH,
		       BATTERY_HISTORY_FILE_PATH) < 0) {
		DBG_PRINTF("[battery_hist] rename failed\n");
		(void)lfs_remove(lfs, BATTERY_HISTORY_TEMP_FILE_PATH);
		return false;
	}
	return true;
}

bool storage_battery_history_init(void)
{
	lfs_t *lfs = lfs_get();
	lfs_file_t file;
	lfs_ssize_t read_len;

	if (s_battery_history_loaded)
		return true;
	if (!lfs_is_ready() || lfs == NULL)
		return false;

	if (lfs_file_open(lfs, &file, BATTERY_HISTORY_FILE_PATH, LFS_O_RDONLY) < 0) {
		DBG_PRINTF("[battery_hist] missing, create empty\n");
		storage_battery_history_set_empty();
		(void)storage_battery_history_save_file();
		return true;
	}
	read_len = lfs_file_read(lfs, &file, &s_battery_history,
				 sizeof(s_battery_history));
	(void)lfs_file_close(lfs, &file);
	if (read_len == (lfs_ssize_t)sizeof(battery_history_legacy_file_t) &&
	    storage_battery_history_legacy_valid((const battery_history_legacy_file_t *)&s_battery_history)) {
		DBG_PRINTF("[battery_hist] migrate 96 -> 288 points\n");
		storage_battery_history_migrate_legacy((const battery_history_legacy_file_t *)&s_battery_history);
		(void)storage_battery_history_save_file();
		return true;
	}
	if (read_len != (lfs_ssize_t)sizeof(s_battery_history) ||
	    !storage_battery_history_valid(&s_battery_history)) {
		DBG_PRINTF("[battery_hist] invalid read=%ld need=%lu, reset\n",
			   (long)read_len,
			   (unsigned long)sizeof(s_battery_history));
		storage_battery_history_set_empty();
		(void)storage_battery_history_save_file();
		return true;
	}

	s_battery_history_loaded = true;
	if (s_battery_history.count > 0U) {
		uint16_t last_index;

		if (s_battery_history.next == 0U)
			last_index = (uint16_t)(STORAGE_BATTERY_HISTORY_MAX_POINTS - 1U);
		else
			last_index = (uint16_t)(s_battery_history.next - 1U);
		if (s_battery_history.count < STORAGE_BATTERY_HISTORY_MAX_POINTS)
			last_index = (uint16_t)(s_battery_history.count - 1U);
		s_battery_history_last_uptime_s =
		    s_battery_history.points[last_index].uptime_s;
		s_battery_history_has_last = true;
	}
	return true;
}

bool storage_battery_history_clear(void)
{
	if (!lfs_is_ready())
		return false;
	storage_battery_history_set_empty();
	return storage_battery_history_save_file();
}

bool storage_uptime_checkpoint_load(uint32_t *out_seconds)
{
	lfs_t *lfs = lfs_get();
	lfs_file_t file;
	uptime_checkpoint_file_t ckpt;
	lfs_ssize_t read_len;

	if (out_seconds == NULL || !lfs_is_ready() || lfs == NULL)
		return false;
	if (lfs_file_open(lfs, &file, UPTIME_CHECKPOINT_FILE_PATH, LFS_O_RDONLY) < 0)
		return false;
	read_len = lfs_file_read(lfs, &file, &ckpt, sizeof(ckpt));
	(void)lfs_file_close(lfs, &file);
	if (read_len != (lfs_ssize_t)sizeof(ckpt) ||
	    ckpt.magic != UPTIME_CHECKPOINT_MAGIC ||
	    ckpt.version != UPTIME_CHECKPOINT_VERSION) {
		return false;
	}
	*out_seconds = ckpt.seconds;
	return true;
}

bool storage_uptime_checkpoint_save(uint32_t seconds)
{
	lfs_t *lfs = lfs_get();
	lfs_file_t file;
	const uptime_checkpoint_file_t ckpt = {
		.magic = UPTIME_CHECKPOINT_MAGIC,
		.version = UPTIME_CHECKPOINT_VERSION,
		.reserved = 0,
		.seconds = seconds,
	};
	lfs_ssize_t written;

	if (!lfs_is_ready() || lfs == NULL)
		return false;
	(void)lfs_remove(lfs, UPTIME_CHECKPOINT_TEMP_FILE_PATH);
	if (lfs_file_open(lfs, &file, UPTIME_CHECKPOINT_TEMP_FILE_PATH,
			  LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) < 0)
		return false;
	written = lfs_file_write(lfs, &file, &ckpt, sizeof(ckpt));
	if (lfs_file_close(lfs, &file) < 0) {
		(void)lfs_remove(lfs, UPTIME_CHECKPOINT_TEMP_FILE_PATH);
		return false;
	}
	if (written != (lfs_ssize_t)sizeof(ckpt)) {
		(void)lfs_remove(lfs, UPTIME_CHECKPOINT_TEMP_FILE_PATH);
		return false;
	}
	if (lfs_rename(lfs, UPTIME_CHECKPOINT_TEMP_FILE_PATH,
		       UPTIME_CHECKPOINT_FILE_PATH) < 0) {
		(void)lfs_remove(lfs, UPTIME_CHECKPOINT_TEMP_FILE_PATH);
		return false;
	}
	return true;
}

static uint32_t storage_battery_history_current_uptime(void)
{
	eeprom_config_t cfg;
	uint32_t checkpoint_seconds = 0;

	storage_config_set_defaults(&cfg);
	(void)storage_load_config(&cfg);
	if (storage_uptime_checkpoint_load(&checkpoint_seconds) &&
	    checkpoint_seconds > cfg.rtc_uptime_seconds) {
		cfg.rtc_uptime_seconds = checkpoint_seconds;
	}
	cfg.magic = EEPROM_MAGIC;
	cfg.version = EEPROM_CONFIG_VERSION;
	storage_config_sanitize(&cfg);
	return rtc_clock_get_uptime_seconds(&cfg);
}

bool storage_battery_history_record(uint16_t millivolts, uint8_t percent)
{
	uint32_t uptime_s;
	storage_battery_history_point_t *point;

	if (!storage_system_ready())
		return false;
	if (!storage_battery_history_init())
		return false;

	uptime_s = storage_battery_history_current_uptime();
	if (s_battery_history_has_last) {
		if (uptime_s < s_battery_history_last_uptime_s) {
			storage_battery_history_set_empty();
		} else if ((uint32_t)(uptime_s - s_battery_history_last_uptime_s) <
			   STORAGE_BATTERY_HISTORY_INTERVAL_SEC) {
			return false;
		}
	}

	if (percent > 100U)
		percent = 100U;

	point = &s_battery_history.points[s_battery_history.next];
	point->uptime_s = uptime_s;
	point->millivolts = millivolts;
	point->percent = percent;
	point->reserved = 0U;

	s_battery_history.next++;
	if (s_battery_history.next >= STORAGE_BATTERY_HISTORY_MAX_POINTS)
		s_battery_history.next = 0U;
	if (s_battery_history.count < STORAGE_BATTERY_HISTORY_MAX_POINTS)
		s_battery_history.count++;

	if (!storage_battery_history_save_file())
		return false;
	s_battery_history_last_uptime_s = uptime_s;
	s_battery_history_has_last = true;
	return true;
}

uint16_t storage_battery_history_get(storage_battery_history_point_t *out_points,
				     uint16_t max_points)
{
	uint16_t count;
	uint16_t first;
	uint16_t copy_count;
	uint16_t copied = 0;
	uint32_t now_uptime_s;
	uint32_t cutoff_s = 0;

	if (out_points == NULL || max_points == 0U)
		return 0;
	if (!storage_battery_history_init())
		return 0;

	count = s_battery_history.count;
	if (count == 0U)
		return 0;
	if (count >= STORAGE_BATTERY_HISTORY_MAX_POINTS)
		first = s_battery_history.next;
	else
		first = 0U;

	now_uptime_s = storage_battery_history_current_uptime();
	if (now_uptime_s > STORAGE_BATTERY_HISTORY_WINDOW_SEC)
		cutoff_s = now_uptime_s - STORAGE_BATTERY_HISTORY_WINDOW_SEC;

	copy_count = (count > max_points) ? max_points : count;
	if (copy_count < count) {
		const uint16_t skip = (uint16_t)(count - copy_count);

		first = (uint16_t)((first + skip) % STORAGE_BATTERY_HISTORY_MAX_POINTS);
	}

	for (uint16_t i = 0; i < copy_count; i++) {
		const uint16_t idx = (uint16_t)((first + i) %
					       STORAGE_BATTERY_HISTORY_MAX_POINTS);

		if (s_battery_history.points[idx].uptime_s < cutoff_s)
			continue;
		out_points[copied++] = s_battery_history.points[idx];
	}
	return copied;
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
	eeprom_config_record_t record;

	if (!out || !s_eeprom_inited || !s_eeprom_present)
		return false;

	if (storage_select_latest_config_record(&record, NULL)) {
		*out = record.config;
		storage_config_sanitize(out);
		return true;
	}
	if (!at24cxx_read(s_m24c02, EEPROM_OFFSET_CONFIG, (uint8_t *)out,
			  sizeof(eeprom_config_t))) {
		return false;
	}
	return storage_upgrade_loaded_config(out);
}

bool storage_save_config(const eeprom_config_t *cfg)
{
	eeprom_config_t sanitized;
	eeprom_config_record_t latest;
	eeprom_config_record_t record;
	uint16_t latest_offset = EEPROM_OFFSET_CONFIG_SLOT1;
	uint16_t next_offset = EEPROM_OFFSET_CONFIG_SLOT0;
	uint32_t next_sequence = 1U;
	bool have_latest;

	if (!cfg || !s_eeprom_inited || !s_eeprom_present)
		return false;
	sanitized = *cfg;
	sanitized.magic = EEPROM_MAGIC;
	sanitized.version = EEPROM_CONFIG_VERSION;
	storage_config_sanitize(&sanitized);

	have_latest = storage_select_latest_config_record(&latest, &latest_offset);
	if (have_latest) {
		if (memcmp(&latest.config, &sanitized, sizeof(sanitized)) == 0)
			return true;
		next_sequence = latest.sequence + 1U;
		next_offset = storage_next_config_slot(latest_offset);
	}

	memset(&record, 0xFF, sizeof(record));
	record.magic = EEPROM_CONFIG_RECORD_MAGIC;
	record.version = EEPROM_CONFIG_VERSION;
	record.reserved = 0;
	record.sequence = next_sequence;
	record.config = sanitized;
	memset(record.padding, 0, sizeof(record.padding));
	record.crc = storage_config_record_crc(&record);

	if (at24cxx_write(s_m24c02, next_offset, (uint8_t *)&record,
			  sizeof(record))) {
		return true;
	}
	next_offset = storage_next_config_slot(next_offset);
	return at24cxx_write(s_m24c02, next_offset, (uint8_t *)&record,
			     sizeof(record));
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
	if (!lfs_is_ready())
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
	(void)reg_fn("Boot Count", storage_menu_action_show_boot_count, UI_MENU_MODE_ALL);
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

	if (!at24cxx_read(s_m24c02, EEPROM_OFFSET_TEST, backup, sizeof(backup))) {
		DBG_PRINTF("M24C02 EEPROM test: backup read failed\n");
		return false;
	}

	if (!at24cxx_write(s_m24c02, EEPROM_OFFSET_TEST, pattern, sizeof(pattern))) {
		DBG_PRINTF("M24C02 EEPROM test: pattern write failed\n");
		return false;
	}

	if (!at24cxx_read(s_m24c02, EEPROM_OFFSET_TEST, verify, sizeof(verify))) {
		DBG_PRINTF("M24C02 EEPROM test: verify read failed\n");
		(void)at24cxx_write(s_m24c02, EEPROM_OFFSET_TEST, backup, sizeof(backup));
		return false;
	}

	if (memcmp(pattern, verify, sizeof(pattern)) != 0) {
		DBG_PRINTF("M24C02 EEPROM test: verify mismatch\n");
		(void)at24cxx_write(s_m24c02, EEPROM_OFFSET_TEST, backup, sizeof(backup));
		return false;
	}

	if (!at24cxx_write(s_m24c02, EEPROM_OFFSET_TEST, backup, sizeof(backup))) {
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
	if (s_flash_inited && s_flash_present)
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
	return storage_flash_is_present() && lfs_is_ready();
}

void storage_prepare_shutdown(void)
{
	uint32_t uptime_seconds = 0;

	storage_eeprom_init();
	if (storage_eeprom_is_present()) {
		eeprom_config_t cfg;
		uint32_t checkpoint_seconds = 0;

		storage_config_set_defaults(&cfg);
		(void)storage_load_config(&cfg);
		if (storage_uptime_checkpoint_load(&checkpoint_seconds) &&
		    checkpoint_seconds > cfg.rtc_uptime_seconds) {
			cfg.rtc_uptime_seconds = checkpoint_seconds;
		}
		cfg.magic = EEPROM_MAGIC;
		cfg.version = EEPROM_CONFIG_VERSION;
		uptime_seconds = rtc_clock_get_uptime_seconds(&cfg);
		cfg.rtc_uptime_seconds = uptime_seconds;
		(void)storage_save_config(&cfg);
	}
	if (storage_flash_is_present()) {
		if (lfs_is_ready()) {
			if (uptime_seconds == 0U) {
				eeprom_config_t cfg;
				uint32_t checkpoint_seconds = 0;

				storage_config_set_defaults(&cfg);
				(void)storage_load_config(&cfg);
				if (storage_uptime_checkpoint_load(&checkpoint_seconds) &&
				    checkpoint_seconds > cfg.rtc_uptime_seconds) {
					cfg.rtc_uptime_seconds = checkpoint_seconds;
				}
				uptime_seconds = rtc_clock_get_uptime_seconds(&cfg);
			}
			(void)storage_uptime_checkpoint_save(uptime_seconds);
		}
		lfs_unmount_fs();
	}
}

void storage_init_task(void *arg)
{

	(void)arg;

	DBG_PRINTF("[storage] init task start\n");
	storage_eeprom_init();   /* init bus + detect M24C02; if not present, load/save APIs return false */
	/* Mount flash FS ASAP so UI can enter system quickly and read boot_count. */
	for (uint8_t i = 0; i < STORAGE_FLASH_BOOT_RETRY_COUNT && !lfs_is_ready(); i++) {
		int lfs_err = -1;

		DBG_PRINTF("[storage] flash/lfs try %u\n", (unsigned int)(i + 1U));
		storage_flash_init();   /* reset + read_id; if present (0xEF), allow LittleFS */
		if (storage_flash_is_present()) {
			lfs_err = lfs_first_run();
			DBG_PRINTF("[storage] lfs_first_run=%d ready=%d\n",
				   lfs_err, lfs_is_ready());
			if (lfs_err == 0)
				break;
		} else {
			DBG_PRINTF("[storage] flash not present\n");
		}
		vTaskDelay(pdMS_TO_TICKS(STORAGE_FLASH_BOOT_RETRY_MS));
	}
	DBG_PRINTF("[storage] init done flash=%d ready=%d\n",
		   storage_flash_is_present(), lfs_is_ready());
	// storage_eeprom_test();
	for (;;) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}
