#include "rtc_clock.h"
#include "stm32f1xx_hal.h"

#define RTC_CLOCK_BKP_MARKER       0xA55Au
#define RTC_CLOCK_LSE_TIMEOUT_MS   5000u
#define RTC_CLOCK_RTC_TIMEOUT      100000u
#define RTC_CLOCK_SECONDS_PER_DAY  86400u
#define RTC_CLOCK_MAX_CALIB        30
#define RTC_CLOCK_MIN_CALIB        (-30)

static bool s_rtc_ready;

static bool rtc_clock_wait_flag(volatile uint32_t *reg, uint32_t mask, bool set, uint32_t timeout)
{
        while (timeout-- > 0u) {
                const bool is_set = ((*reg & mask) != 0u);

                if (is_set == set)
                        return true;
        }
        return false;
}

static void rtc_clock_enable_backup_access(void)
{
        RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;
        PWR->CR |= PWR_CR_DBP;
}

static bool rtc_clock_wait_lse_ready(void)
{
        const uint32_t start = HAL_GetTick();

        while ((RCC->BDCR & RCC_BDCR_LSERDY) == 0u) {
                if ((uint32_t)(HAL_GetTick() - start) >= RTC_CLOCK_LSE_TIMEOUT_MS)
                        return false;
        }
        return true;
}

static void rtc_clock_reset_backup_domain(void)
{
        RCC->BDCR |= RCC_BDCR_BDRST;
        RCC->BDCR &= ~RCC_BDCR_BDRST;
}

static bool rtc_clock_wait_rtoff(void)
{
        return rtc_clock_wait_flag(&RTC->CRL, RTC_CRL_RTOFF, true, RTC_CLOCK_RTC_TIMEOUT);
}

static bool rtc_clock_enter_config(void)
{
        if (!rtc_clock_wait_rtoff())
                return false;
        RTC->CRL |= RTC_CRL_CNF;
        return true;
}

static bool rtc_clock_exit_config(void)
{
        RTC->CRL &= ~RTC_CRL_CNF;
        return rtc_clock_wait_rtoff();
}

static bool rtc_clock_sync(void)
{
        RTC->CRL &= ~RTC_CRL_RSF;
        return rtc_clock_wait_flag(&RTC->CRL, RTC_CRL_RSF, true, RTC_CLOCK_RTC_TIMEOUT);
}

static bool rtc_clock_write_counter(uint32_t seconds)
{
        if (!rtc_clock_enter_config())
                return false;
        RTC->CNTH = (uint16_t)(seconds >> 16);
        RTC->CNTL = (uint16_t)(seconds & 0xFFFFu);
        return rtc_clock_exit_config();
}

static uint32_t rtc_clock_read_counter(void)
{
        uint16_t hi1;
        uint16_t lo;
        uint16_t hi2;

        do {
                hi1 = (uint16_t)RTC->CNTH;
                lo = (uint16_t)RTC->CNTL;
                hi2 = (uint16_t)RTC->CNTH;
        } while (hi1 != hi2);

        return ((uint32_t)hi1 << 16) | lo;
}

static uint32_t rtc_clock_apply_calib(uint32_t raw_seconds, const eeprom_config_t *cfg)
{
        int64_t corrected = raw_seconds;

        if (cfg != NULL && cfg->rtc_calib_sec_per_day != 0) {
                const int32_t elapsed = (int32_t)(raw_seconds - cfg->rtc_calib_anchor_raw);
                corrected += ((int64_t)elapsed * (int64_t)cfg->rtc_calib_sec_per_day) /
                             (int64_t)RTC_CLOCK_SECONDS_PER_DAY;
                if (corrected < 0)
                        corrected = 0;
        }
        return (uint32_t)corrected;
}

bool rtc_clock_init(void)
{
        rtc_clock_enable_backup_access();

        if ((RCC->BDCR & RCC_BDCR_RTCEN) != 0u &&
            (RCC->BDCR & RCC_BDCR_RTCSEL) != RCC_BDCR_RTCSEL_LSE) {
                rtc_clock_reset_backup_domain();
        }

        if ((RCC->BDCR & RCC_BDCR_RTCEN) == 0u) {
                RCC->BDCR |= RCC_BDCR_LSEON;
                if (!rtc_clock_wait_lse_ready()) {
                        s_rtc_ready = false;
                        return false;
                }

                RCC->BDCR &= ~RCC_BDCR_RTCSEL;
                RCC->BDCR |= RCC_BDCR_RTCSEL_LSE;
                RCC->BDCR |= RCC_BDCR_RTCEN;

                if (!rtc_clock_sync()) {
                        s_rtc_ready = false;
                        return false;
                }
                if (!rtc_clock_enter_config()) {
                        s_rtc_ready = false;
                        return false;
                }
                RTC->PRLH = 0;
                RTC->PRLL = 32767u;
                if (!rtc_clock_exit_config()) {
                        s_rtc_ready = false;
                        return false;
                }
        } else {
                RCC->BDCR |= RCC_BDCR_LSEON;
                if (!rtc_clock_wait_lse_ready()) {
                        s_rtc_ready = false;
                        return false;
                }
                if (!rtc_clock_sync()) {
                        s_rtc_ready = false;
                        return false;
                }
        }

        s_rtc_ready = true;
        return true;
}

bool rtc_clock_is_ready(void)
{
        return s_rtc_ready;
}

bool rtc_clock_time_is_set(void)
{
        return s_rtc_ready && BKP->DR1 == RTC_CLOCK_BKP_MARKER;
}

uint32_t rtc_clock_get_raw_seconds(void)
{
        if (!s_rtc_ready)
                return 0;
        return rtc_clock_read_counter();
}

bool rtc_clock_set_time(uint8_t hour, uint8_t minute, uint8_t second)
{
        uint32_t raw_seconds;

        if (!s_rtc_ready || hour > 23u || minute > 59u || second > 59u)
                return false;
        raw_seconds = ((uint32_t)hour * 3600u) + ((uint32_t)minute * 60u) + second;
        if (!rtc_clock_write_counter(raw_seconds))
                return false;
        BKP->DR1 = RTC_CLOCK_BKP_MARKER;
        return true;
}

rtc_clock_time_t rtc_clock_get_time(const eeprom_config_t *cfg)
{
        rtc_clock_time_t t = {0};
        uint32_t seconds;

        if (!rtc_clock_time_is_set())
                return t;

        seconds = rtc_clock_apply_calib(rtc_clock_read_counter(), cfg) % RTC_CLOCK_SECONDS_PER_DAY;
        t.hour = (uint8_t)(seconds / 3600u);
        seconds %= 3600u;
        t.minute = (uint8_t)(seconds / 60u);
        t.second = (uint8_t)(seconds % 60u);
        t.valid = true;
        return t;
}

int8_t rtc_clock_calib_get(const eeprom_config_t *cfg)
{
        if (cfg == NULL)
                return 0;
        return cfg->rtc_calib_sec_per_day;
}

void rtc_clock_calib_set(eeprom_config_t *cfg, int8_t sec_per_day)
{
        if (cfg == NULL)
                return;
        if (sec_per_day > RTC_CLOCK_MAX_CALIB)
                sec_per_day = RTC_CLOCK_MAX_CALIB;
        else if (sec_per_day < RTC_CLOCK_MIN_CALIB)
                sec_per_day = RTC_CLOCK_MIN_CALIB;
        cfg->rtc_calib_sec_per_day = sec_per_day;
        cfg->rtc_calib_anchor_raw = rtc_clock_get_raw_seconds();
}
