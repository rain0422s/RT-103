#include "rtc_clock.h"
#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"

#define RTC_CLOCK_BKP_MARKER       0xA55Au
#define RTC_CLOCK_LSE_FALLBACK_MS  15000u
#define RTC_CLOCK_LSI_FAIL_MS      1000u
#define RTC_CLOCK_RTC_TIMEOUT      100000u
#define RTC_CLOCK_SECONDS_PER_DAY  86400u
#define RTC_CLOCK_DEFAULT_SECONDS  0u
#define RTC_CLOCK_MAX_CALIB        30
#define RTC_CLOCK_MIN_CALIB        (-30)
#define RTC_CLOCK_LSE_HZ           32768u
#define RTC_CLOCK_LSI_HZ           LSI_VALUE

typedef enum {
        RTC_CLOCK_STATE_OFF = 0,
        RTC_CLOCK_STATE_LSE_STARTING,
        RTC_CLOCK_STATE_LSI_STARTING,
        RTC_CLOCK_STATE_READY,
        RTC_CLOCK_STATE_FAILED,
} rtc_clock_state_t;

static bool s_rtc_ready;
static rtc_clock_state_t s_rtc_state;
static rtc_clock_source_t s_rtc_source;
static uint32_t s_start_tick_ms;
static bool s_start_tick_valid;
static uint32_t s_uptime_anchor_tick_ms;
static bool s_uptime_anchor_valid;

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

static void rtc_clock_note_lse_start(void)
{
        if (s_start_tick_valid)
                return;
        s_start_tick_ms = HAL_GetTick();
        s_start_tick_valid = true;
}

static void rtc_clock_restart_start_timer(void)
{
        s_start_tick_ms = HAL_GetTick();
        s_start_tick_valid = true;
}

static bool rtc_clock_start_timed_out(uint32_t timeout_ms)
{
        return s_start_tick_valid &&
               (uint32_t)(HAL_GetTick() - s_start_tick_ms) >= timeout_ms;
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

static void rtc_clock_set_prescaler(uint32_t clock_hz)
{
        const uint32_t prescaler = (clock_hz > 0u) ? (clock_hz - 1u) : 0u;

        RTC->PRLH = (uint16_t)(prescaler >> 16);
        RTC->PRLL = (uint16_t)(prescaler & 0xFFFFu);
}

static bool rtc_clock_finish_init(rtc_clock_source_t source, uint32_t rtcsel, uint32_t clock_hz)
{
        const bool need_config = ((RCC->BDCR & RCC_BDCR_RTCEN) == 0u);

        if (need_config) {
                RCC->BDCR &= ~RCC_BDCR_RTCSEL;
                RCC->BDCR |= rtcsel;
                RCC->BDCR |= RCC_BDCR_RTCEN;
        }

        if (!rtc_clock_sync()) {
                s_rtc_ready = false;
                s_rtc_state = RTC_CLOCK_STATE_FAILED;
                return false;
        }

        if (need_config) {
                if (!rtc_clock_enter_config()) {
                        s_rtc_ready = false;
                        s_rtc_state = RTC_CLOCK_STATE_FAILED;
                        return false;
                }
                rtc_clock_set_prescaler(clock_hz);
                if (!rtc_clock_exit_config()) {
                        s_rtc_ready = false;
                        s_rtc_state = RTC_CLOCK_STATE_FAILED;
                        return false;
                }
        }

        s_rtc_ready = true;
        s_rtc_source = source;
        s_rtc_state = RTC_CLOCK_STATE_READY;
        return true;
}

static bool rtc_clock_existing_source_ready(uint32_t source_bits)
{
        if (source_bits == RCC_BDCR_RTCSEL_LSE) {
                RCC->BDCR |= RCC_BDCR_LSEON;
                return (RCC->BDCR & RCC_BDCR_LSERDY) != 0u;
        }
        if (source_bits == RCC_BDCR_RTCSEL_LSI) {
                RCC->CSR |= RCC_CSR_LSION;
                return (RCC->CSR & RCC_CSR_LSIRDY) != 0u;
        }
        return false;
}

static bool rtc_clock_finish_existing_source(uint32_t source_bits)
{
        if (source_bits == RCC_BDCR_RTCSEL_LSE)
                return rtc_clock_finish_init(RTC_CLOCK_SOURCE_LSE, RCC_BDCR_RTCSEL_LSE,
                                             RTC_CLOCK_LSE_HZ);
        if (source_bits == RCC_BDCR_RTCSEL_LSI)
                return rtc_clock_finish_init(RTC_CLOCK_SOURCE_LSI, RCC_BDCR_RTCSEL_LSI,
                                             RTC_CLOCK_LSI_HZ);
        return false;
}

static bool rtc_clock_start_lsi_fallback(void)
{
        rtc_clock_reset_backup_domain();
        rtc_clock_restart_start_timer();
        RCC->CSR |= RCC_CSR_LSION;
        s_rtc_ready = false;
        s_rtc_source = RTC_CLOCK_SOURCE_NONE;
        s_rtc_state = RTC_CLOCK_STATE_LSI_STARTING;
        return true;
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

static uint32_t rtc_clock_get_uptime_base(const eeprom_config_t *cfg)
{
        if (cfg == NULL || cfg->magic != EEPROM_MAGIC || cfg->version != EEPROM_CONFIG_VERSION)
                return RTC_CLOCK_DEFAULT_SECONDS;
        return cfg->rtc_uptime_seconds;
}

static uint32_t rtc_clock_uptime_tick_ms(void)
{
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
                return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        return HAL_GetTick();
}

static uint32_t rtc_clock_get_uptime_elapsed_seconds(void)
{
        if (!s_uptime_anchor_valid)
                return rtc_clock_uptime_tick_ms() / 1000u;
        return (uint32_t)(rtc_clock_uptime_tick_ms() - s_uptime_anchor_tick_ms) / 1000u;
}

uint32_t rtc_clock_get_uptime_seconds(const eeprom_config_t *cfg)
{
        return rtc_clock_get_uptime_base(cfg) + rtc_clock_get_uptime_elapsed_seconds();
}

void rtc_clock_uptime_sync(const eeprom_config_t *cfg)
{
        (void)cfg;
        s_uptime_anchor_tick_ms = rtc_clock_uptime_tick_ms();
        s_uptime_anchor_valid = true;
}

void rtc_clock_uptime_set(eeprom_config_t *cfg, uint32_t seconds)
{
        if (cfg != NULL)
                cfg->rtc_uptime_seconds = seconds;
        rtc_clock_uptime_sync(cfg);
}

bool rtc_clock_init(void)
{
        (void)rtc_clock_poll();
        return s_rtc_ready;
}

bool rtc_clock_poll(void)
{
        const rtc_clock_state_t prev_state = s_rtc_state;
        const bool was_ready = s_rtc_ready;
        const uint32_t source_bits = RCC->BDCR & RCC_BDCR_RTCSEL;

        if (s_rtc_ready)
                return false;

        rtc_clock_enable_backup_access();

        if ((RCC->BDCR & RCC_BDCR_RTCEN) != 0u) {
                if (source_bits != RCC_BDCR_RTCSEL_LSE && source_bits != RCC_BDCR_RTCSEL_LSI) {
                        rtc_clock_reset_backup_domain();
                        rtc_clock_restart_start_timer();
                } else if (rtc_clock_existing_source_ready(source_bits)) {
                        (void)rtc_clock_finish_existing_source(source_bits);
                        return (s_rtc_state != prev_state) || (s_rtc_ready != was_ready);
                } else if (source_bits == RCC_BDCR_RTCSEL_LSE) {
                        rtc_clock_note_lse_start();
                        if (rtc_clock_start_timed_out(RTC_CLOCK_LSE_FALLBACK_MS))
                                (void)rtc_clock_start_lsi_fallback();
                        else
                                s_rtc_state = RTC_CLOCK_STATE_LSE_STARTING;
                        return (s_rtc_state != prev_state) || (s_rtc_ready != was_ready);
                } else {
                        if (s_rtc_state != RTC_CLOCK_STATE_LSI_STARTING)
                                rtc_clock_restart_start_timer();
                        RCC->CSR |= RCC_CSR_LSION;
                        s_rtc_state = RTC_CLOCK_STATE_LSI_STARTING;
                        return (s_rtc_state != prev_state) || (s_rtc_ready != was_ready);
                }
        }

        RCC->BDCR |= RCC_BDCR_LSEON;
        rtc_clock_note_lse_start();

        if ((RCC->BDCR & RCC_BDCR_LSERDY) != 0u) {
                (void)rtc_clock_finish_init(RTC_CLOCK_SOURCE_LSE, RCC_BDCR_RTCSEL_LSE,
                                            RTC_CLOCK_LSE_HZ);
                return (s_rtc_state != prev_state) || (s_rtc_ready != was_ready);
        }

        if (s_rtc_state != RTC_CLOCK_STATE_LSI_STARTING &&
            rtc_clock_start_timed_out(RTC_CLOCK_LSE_FALLBACK_MS)) {
                (void)rtc_clock_start_lsi_fallback();
                return (s_rtc_state != prev_state) || (s_rtc_ready != was_ready);
        }

        if (s_rtc_state == RTC_CLOCK_STATE_LSI_STARTING) {
                if ((RCC->CSR & RCC_CSR_LSIRDY) != 0u)
                        (void)rtc_clock_finish_init(RTC_CLOCK_SOURCE_LSI, RCC_BDCR_RTCSEL_LSI,
                                                    RTC_CLOCK_LSI_HZ);
                else if (rtc_clock_start_timed_out(RTC_CLOCK_LSI_FAIL_MS))
                        s_rtc_state = RTC_CLOCK_STATE_FAILED;
                return (s_rtc_state != prev_state) || (s_rtc_ready != was_ready);
        }

        s_rtc_ready = false;
        s_rtc_source = RTC_CLOCK_SOURCE_NONE;
        s_rtc_state = RTC_CLOCK_STATE_LSE_STARTING;
        return (s_rtc_state != prev_state) || (s_rtc_ready != was_ready);
}

bool rtc_clock_is_ready(void)
{
        return s_rtc_ready;
}

bool rtc_clock_is_starting(void)
{
        return s_rtc_state == RTC_CLOCK_STATE_LSE_STARTING ||
               s_rtc_state == RTC_CLOCK_STATE_LSI_STARTING;
}

rtc_clock_source_t rtc_clock_get_source(void)
{
        return s_rtc_source;
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

uint32_t rtc_clock_get_persist_seconds(const eeprom_config_t *cfg)
{
        if (rtc_clock_time_is_set())
                return rtc_clock_apply_calib(rtc_clock_read_counter(), cfg);
        return rtc_clock_get_uptime_seconds(cfg);
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

rtc_clock_time_t rtc_clock_get_uptime_time(const eeprom_config_t *cfg)
{
        rtc_clock_time_t t = {0};
        uint32_t seconds = rtc_clock_get_uptime_seconds(cfg) % RTC_CLOCK_SECONDS_PER_DAY;

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
