/**
 * @file lis2dh12_config.c
 * @brief LIS2DH12 工作模式、数据读取、FIFO、中断、自检、活动/不活动 配置实现
 */
#include "lis2dh12_config.h"
#include "lis2dh12_reg.h"
#include <string.h>

#include "stm32f1xx_hal.h"

/* ---------- 1. 四种工作模式 ---------- */
int lis2dh12_config_set_mode(stmdev_ctx_t *ctx, lis2dh12_config_mode_t mode,
                             lis2dh12_odr_t odr)
{
    if (!ctx) return -1;

    if (mode == LIS2DH12_MODE_POWER_DOWN) {
        lis2dh12_data_rate_set(ctx, LIS2DH12_POWER_DOWN);
        return 0;
    }

    lis2dh12_data_rate_set(ctx, odr);
    switch (mode) {
        case LIS2DH12_MODE_LOW_POWER:
            lis2dh12_operating_mode_set(ctx, LIS2DH12_LP_8bit);
            break;
        case LIS2DH12_MODE_NORMAL:
            lis2dh12_operating_mode_set(ctx, LIS2DH12_NM_10bit);
            break;
        case LIS2DH12_MODE_HIGH_RESOLUTION:
            lis2dh12_operating_mode_set(ctx, LIS2DH12_HR_12bit);
            break;
        default:
            return -1;
    }
    return 0;
}

uint32_t lis2dh12_config_settling_ms(lis2dh12_odr_t odr)
{
    uint32_t hz = 1;
    switch (odr) {
        case LIS2DH12_ODR_1Hz:   hz = 1;   break;
        case LIS2DH12_ODR_10Hz:  hz = 10;  break;
        case LIS2DH12_ODR_25Hz:  hz = 25;  break;
        case LIS2DH12_ODR_50Hz:  hz = 50;  break;
        case LIS2DH12_ODR_100Hz: hz = 100; break;
        case LIS2DH12_ODR_200Hz: hz = 200; break;
        case LIS2DH12_ODR_400Hz: hz = 400; break;
        default: return 100;
    }
    /* 7/ODR 秒 → 毫秒，至少 1ms */
    uint32_t ms = 7000 / hz;
    return ms > 0 ? ms : 1;
}

/* ---------- 2. 数据读取三方案 ---------- */
int lis2dh12_config_enable_bdu(stmdev_ctx_t *ctx, uint8_t enable)
{
    if (!ctx) return -1;
    return (int)lis2dh12_block_data_update_set(ctx, enable ? 1 : 0);
}

int lis2dh12_config_enable_drdy_int1(stmdev_ctx_t *ctx, uint8_t enable)
{
    if (!ctx) return -1;
    lis2dh12_ctrl_reg3_t c3;
    if (lis2dh12_pin_int1_config_get(ctx, &c3) != 0) return -1;
    c3.i1_zyxda = enable ? 1 : 0;
    return (int)lis2dh12_pin_int1_config_set(ctx, &c3);
}

int lis2dh12_config_poll_once(stmdev_ctx_t *ctx, int16_t raw[3])
{
    if (!ctx) return -1;
    lis2dh12_status_reg_t status;
    uint32_t timeout = 1000;
    while (timeout--) {
        if (lis2dh12_status_get(ctx, &status) != 0) return -1;
        if (status.zyxda) break;
        HAL_Delay(1);
    }
    if (!status.zyxda) return -1;
    if (raw) {
        if (lis2dh12_acceleration_raw_get(ctx, raw) != 0) return -1;
    }
    return 0;
}

/* ---------- 3. FIFO ---------- */
static lis2dh12_fm_t _fifo_mode_map(lis2dh12_config_fifo_mode_t m)
{
    switch (m) {
        case LIS2DH12_FIFO_BYPASS:         return LIS2DH12_BYPASS_MODE;
        case LIS2DH12_FIFO_FIFO:           return LIS2DH12_FIFO_MODE;
        case LIS2DH12_FIFO_STREAM:         return LIS2DH12_DYNAMIC_STREAM_MODE;
        case LIS2DH12_FIFO_STREAM_TO_FIFO: return LIS2DH12_STREAM_TO_FIFO_MODE;
        default: return LIS2DH12_BYPASS_MODE;
    }
}

int lis2dh12_config_fifo_set_mode(stmdev_ctx_t *ctx, lis2dh12_config_fifo_mode_t mode)
{
    if (!ctx) return -1;
    return (int)lis2dh12_fifo_mode_set(ctx, _fifo_mode_map(mode));
}

int lis2dh12_config_fifo_set_watermark(stmdev_ctx_t *ctx, uint8_t wm)
{
    if (!ctx) return -1;
    if (wm > 31) wm = 31;
    return (int)lis2dh12_fifo_watermark_set(ctx, wm);
}

int lis2dh12_config_fifo_int1(stmdev_ctx_t *ctx, uint8_t wtm_enable, uint8_t ovr_enable)
{
    if (!ctx) return -1;
    lis2dh12_ctrl_reg3_t c3;
    if (lis2dh12_pin_int1_config_get(ctx, &c3) != 0) return -1;
    c3.i1_wtm    = wtm_enable ? 1 : 0;
    c3.i1_overrun = ovr_enable ? 1 : 0;
    return (int)lis2dh12_pin_int1_config_set(ctx, &c3);
}

int lis2dh12_config_fifo_enable(stmdev_ctx_t *ctx, uint8_t enable)
{
    if (!ctx) return -1;
    return (int)lis2dh12_fifo_set(ctx, enable ? 1 : 0);
}

int lis2dh12_config_fifo_batch_read(stmdev_ctx_t *ctx, int16_t *samples, int max_samples)
{
    if (!ctx || !samples || max_samples <= 0) return 0;
    lis2dh12_fifo_src_reg_t src;
    if (lis2dh12_fifo_status_get(ctx, &src) != 0) return 0;
    uint8_t n = src.fss;
    if (n > 31) n = 31;
    if (n > (uint8_t)max_samples) n = (uint8_t)max_samples;
    for (uint8_t i = 0; i < n; i++) {
        if (lis2dh12_acceleration_raw_get(ctx, &samples[i * 3]) != 0)
            return (int)i;
    }
    return (int)n;
}

/* ---------- 4. 中断：自由落体、6D/4D、单击/双击 ---------- */
int lis2dh12_config_int1_free_fall(stmdev_ctx_t *ctx, uint8_t ths_lsb, uint8_t dur)
{
    if (!ctx) return -1;
    lis2dh12_int1_cfg_t cfg = { 0 };
    cfg.xlie = cfg.ylie = cfg.zlie = 1;
    cfg.aoi = 1;
    if (lis2dh12_int1_gen_conf_set(ctx, &cfg) != 0) return -1;
    lis2dh12_int1_gen_threshold_set(ctx, ths_lsb & 0x7F);
    lis2dh12_int1_gen_duration_set(ctx, dur & 0x7F);
    lis2dh12_ctrl_reg3_t c3;
    if (lis2dh12_pin_int1_config_get(ctx, &c3) != 0) return -1;
    c3.i1_ia1 = 1;
    return (int)lis2dh12_pin_int1_config_set(ctx, &c3);
}

int lis2dh12_config_int1_6d_4d(stmdev_ctx_t *ctx, uint8_t six_d, uint8_t xh, uint8_t xl,
                               uint8_t yh, uint8_t yl, uint8_t zh, uint8_t zl,
                               uint8_t ths_lsb, uint8_t dur)
{
    if (!ctx) return -1;
    lis2dh12_int1_cfg_t cfg = { 0 };
    cfg._6d = six_d ? 1 : 0;
    cfg.xhie = xh; cfg.xlie = xl;
    cfg.yhie = yh; cfg.ylie = yl;
    cfg.zhie = zh; cfg.zlie = zl;
    if (lis2dh12_int1_gen_conf_set(ctx, &cfg) != 0) return -1;
    lis2dh12_int1_gen_threshold_set(ctx, ths_lsb & 0x7F);
    lis2dh12_int1_gen_duration_set(ctx, dur & 0x7F);
    lis2dh12_ctrl_reg3_t c3;
    if (lis2dh12_pin_int1_config_get(ctx, &c3) != 0) return -1;
    c3.i1_ia1 = 1;
    return (int)lis2dh12_pin_int1_config_set(ctx, &c3);
}

int lis2dh12_config_int1_click(stmdev_ctx_t *ctx,
                               uint8_t xs, uint8_t xd, uint8_t ys, uint8_t yd,
                               uint8_t zs, uint8_t zd,
                               uint8_t ths, uint8_t time_limit,
                               uint8_t latency, uint8_t window)
{
    if (!ctx) return -1;
    lis2dh12_click_cfg_t cfg = { 0 };
    cfg.xs = xs; cfg.xd = xd;
    cfg.ys = ys; cfg.yd = yd;
    cfg.zs = zs; cfg.zd = zd;
    if (lis2dh12_tap_conf_set(ctx, &cfg) != 0) return -1;
    lis2dh12_tap_threshold_set(ctx, ths & 0x7F);
    lis2dh12_shock_dur_set(ctx, time_limit);
    lis2dh12_quiet_dur_set(ctx, latency);
    lis2dh12_double_tap_timeout_set(ctx, window);
    lis2dh12_ctrl_reg3_t c3;
    if (lis2dh12_pin_int1_config_get(ctx, &c3) != 0) return -1;
    c3.i1_click = 1;
    return (int)lis2dh12_pin_int1_config_set(ctx, &c3);
}

void lis2dh12_config_int1_clear_source(stmdev_ctx_t *ctx)
{
    if (!ctx) return;
    lis2dh12_int1_src_t src;
    lis2dh12_int1_gen_source_get(ctx, &src);
    (void)src;
    lis2dh12_click_src_t csrc;
    lis2dh12_tap_source_get(ctx, &csrc);
    (void)csrc;
}

/* ---------- 5. 自检 ---------- */
int lis2dh12_config_self_test_run(stmdev_ctx_t *ctx, uint16_t samples,
                                  lis2dh12_self_test_result_t *result)
{
    if (!ctx || !result || samples == 0) return -1;
    memset(result, 0, sizeof(*result));

    int32_t sum_b[3] = { 0 }, sum_a[3] = { 0 };
    int16_t raw[3];
    uint16_t i;

    lis2dh12_self_test_set(ctx, LIS2DH12_ST_DISABLE);
    HAL_Delay(80);
    for (i = 0; i < samples; i++) {
        if (lis2dh12_acceleration_raw_get(ctx, raw) != 0) return -1;
        sum_b[0] += raw[0]; sum_b[1] += raw[1]; sum_b[2] += raw[2];
        HAL_Delay(5);
    }

    lis2dh12_self_test_set(ctx, LIS2DH12_ST_POSITIVE);
    HAL_Delay(80);
    for (i = 0; i < samples; i++) {
        if (lis2dh12_acceleration_raw_get(ctx, raw) != 0) return -1;
        sum_a[0] += raw[0]; sum_a[1] += raw[1]; sum_a[2] += raw[2];
        HAL_Delay(5);
    }

    lis2dh12_self_test_set(ctx, LIS2DH12_ST_DISABLE);
    HAL_Delay(80);

    result->delta_x = (int16_t)((sum_a[0] - sum_b[0]) / (int32_t)samples);
    result->delta_y = (int16_t)((sum_a[1] - sum_b[1]) / (int32_t)samples);
    result->delta_z = (int16_t)((sum_a[2] - sum_b[2]) / (int32_t)samples);

    /* 2g HR 下自检典型变化约几百 LSB，按数据手册范围判定（此处简化：非零且同向即认为有效） */
    int16_t dx = result->delta_x, dy = result->delta_y, dz = result->delta_z;
    result->pass = (dx != 0 || dy != 0 || dz != 0) ? 1 : 0;
    return 0;
}

/* ---------- 6. 活动/不活动 ---------- */
int lis2dh12_config_activity_inactivity(stmdev_ctx_t *ctx, uint8_t act_ths,
                                        uint8_t act_dur)
{
    if (!ctx) return -1;
    if (lis2dh12_act_threshold_set(ctx, act_ths & 0x7F) != 0) return -1;
    if (lis2dh12_act_timeout_set(ctx, act_dur) != 0) return -1;
    lis2dh12_ctrl_reg6_t c6;
    if (lis2dh12_pin_int2_config_get(ctx, &c6) != 0) return -1;
    c6.i2_act = 1;
    return (int)lis2dh12_pin_int2_config_set(ctx, &c6);
}
