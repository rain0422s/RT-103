#ifndef __RTC_CLOCK_H
#define __RTC_CLOCK_H

#include "eeprom_layout.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
        uint8_t hour;
        uint8_t minute;
        uint8_t second;
        bool valid;
} rtc_clock_time_t;

bool rtc_clock_init(void);
bool rtc_clock_is_ready(void);
bool rtc_clock_time_is_set(void);
uint32_t rtc_clock_get_raw_seconds(void);
bool rtc_clock_set_time(uint8_t hour, uint8_t minute, uint8_t second);
rtc_clock_time_t rtc_clock_get_time(const eeprom_config_t *cfg);
int8_t rtc_clock_calib_get(const eeprom_config_t *cfg);
void rtc_clock_calib_set(eeprom_config_t *cfg, int8_t sec_per_day);

#endif /* __RTC_CLOCK_H */
