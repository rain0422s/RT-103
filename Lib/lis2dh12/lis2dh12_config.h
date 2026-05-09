/**
 * @file lis2dh12_config.h
 * @brief LIS2DH12 工作模式、数据读取、FIFO、中断、自检、活动/不活动 配置 API
 *        参考 AN5005 / LIS2DH12 数据手册
 */
#ifndef __LIS2DH12_CONFIG_H
#define __LIS2DH12_CONFIG_H

#include "lis2dh12_reg.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 1. 四种工作模式 ========== */
typedef enum {
    LIS2DH12_MODE_POWER_DOWN = 0,
    LIS2DH12_MODE_LOW_POWER,
    LIS2DH12_MODE_NORMAL,
    LIS2DH12_MODE_HIGH_RESOLUTION,
} lis2dh12_config_mode_t;

/**
 * 设置工作模式。从掉电唤醒或切换模式后建议延时 7/ODR 再读数据。
 * @param ctx 设备上下文（如 lis2dh12_get_ctx()）
 * @param mode 掉电/低功耗/正常/高分辨率
 * @param odr 非掉电时的 ODR（掉电时忽略）
 * @return 0 成功
 */
int lis2dh12_config_set_mode(stmdev_ctx_t *ctx, lis2dh12_config_mode_t mode,
                             lis2dh12_odr_t odr);

/**
 * 模式切换后建议等待的毫秒数（7/ODR 近似）。odr 为当前 ODR 枚举。
 */
uint32_t lis2dh12_config_settling_ms(lis2dh12_odr_t odr);

/* ========== 2. 数据读取三方案 ========== */

/** 使能 BDU（块更新），防止读过程中高低字节错位。建议高 ODR 或中断读时开启。 */
int lis2dh12_config_enable_bdu(stmdev_ctx_t *ctx, uint8_t enable);

/**
 * 将 DRDY（数据就绪）映射到 INT1。主机在 INT1 中断里读加速度。
 * @param ctx 设备上下文
 * @param enable 1 使能，0 关闭
 */
int lis2dh12_config_enable_drdy_int1(stmdev_ctx_t *ctx, uint8_t enable);

/**
 * 轮询读取：等待 STATUS_REG ZYXDA 后读一次加速度。适用于无 INT 的轮询方案。
 * @param ctx 设备上下文
 * @param raw 输出 [x,y,z] 原始值（可选，传 NULL 则仅等待）
 * @return 0 成功
 */
int lis2dh12_config_poll_once(stmdev_ctx_t *ctx, int16_t raw[3]);

/* ========== 3. FIFO 全功能 ========== */
typedef enum {
    LIS2DH12_FIFO_BYPASS = 0,
    LIS2DH12_FIFO_FIFO,
    LIS2DH12_FIFO_STREAM,
    LIS2DH12_FIFO_STREAM_TO_FIFO,
} lis2dh12_config_fifo_mode_t;

/** 设置 FIFO 模式：Bypass / FIFO / Stream / Stream-to-FIFO。先使能 FIFO(CTRL_REG5)。 */
int lis2dh12_config_fifo_set_mode(stmdev_ctx_t *ctx, lis2dh12_config_fifo_mode_t mode);

/** 设置水印（0~31）。达到后 FIFO_SRC.WTM=1，可触发 INT。 */
int lis2dh12_config_fifo_set_watermark(stmdev_ctx_t *ctx, uint8_t wm);

/** INT1 响应 FIFO 水印（WTM）或溢出（OVR）。mask: bit0=WTM, bit1=OVR。 */
int lis2dh12_config_fifo_int1(stmdev_ctx_t *ctx, uint8_t wtm_enable, uint8_t ovr_enable);

/** 使能 FIFO 模块（CTRL_REG5 fifo_en=1）。 */
int lis2dh12_config_fifo_enable(stmdev_ctx_t *ctx, uint8_t enable);

/**
 * 批量读 FIFO：根据 FIFO_SRC 当前样本数连续读多组 XYZ。
 * 注意：读速应 >= ODR 产生速率，否则会溢出。Stream 模式下可循环调用。
 * @param ctx 设备上下文
 * @param samples 输出缓冲区 [x,y,z, x,y,z, ...]，至少 max_samples*3 个 int16_t
 * @param max_samples 最多读取样本数
 * @return 实际读取的样本数
 */
int lis2dh12_config_fifo_batch_read(stmdev_ctx_t *ctx, int16_t *samples,
                                    int max_samples);

/* ========== 4. 中断功能：自由落体、唤醒、6D/4D、单击/双击 ========== */

/** INT1 自由落体：三轴均低于阈值持续一段时间。ths 约 7 位 LSB，dur 单位 1/ODR。 */
int lis2dh12_config_int1_free_fall(stmdev_ctx_t *ctx, uint8_t ths_lsb, uint8_t dur);

/**
 * INT1 6D/4D 朝向。6d=1 为 6 方向，0 为 4 方向；xh/xl/yh/yl/zh/zl 为各轴高/低使能。
 * 典型 6D：xh=xl=yh=yl=zh=zl=1，设阈值。
 */
int lis2dh12_config_int1_6d_4d(stmdev_ctx_t *ctx, uint8_t six_d, uint8_t xh, uint8_t xl,
                               uint8_t yh, uint8_t yl, uint8_t zh, uint8_t zl,
                               uint8_t ths_lsb, uint8_t dur);

/**
 * 单击/双击：使能轴与单双击。xs/xd=单/双 X 轴，同理 y,z。
 * ths 单击阈值；time_limit 双击冲击时间限；latency 双击间隔；window 第二次敲击窗。
 */
int lis2dh12_config_int1_click(stmdev_ctx_t *ctx,
                               uint8_t xs, uint8_t xd, uint8_t ys, uint8_t yd,
                               uint8_t zs, uint8_t zd,
                               uint8_t ths, uint8_t time_limit,
                               uint8_t latency, uint8_t window);

/** 单击/双击映射到 INT2（CTRL_REG6 i2_click）；关闭 INT1 上的 click 路由。 */
int lis2dh12_config_int2_click(stmdev_ctx_t *ctx,
                               uint8_t xs, uint8_t xd, uint8_t ys, uint8_t yd,
                               uint8_t zs, uint8_t zd,
                               uint8_t ths, uint8_t time_limit,
                               uint8_t latency, uint8_t window);

/** 清除 INT1 源（读 INT1_SRC）并可选清除 Click 源（读 CLICK_SRC）。 */
void lis2dh12_config_int1_clear_source(stmdev_ctx_t *ctx);

/* ========== 5. 自检 ========== */

/** 自检结果：差值在预期范围内为 PASS。 */
typedef struct {
    int16_t delta_x, delta_y, delta_z;  /* 自检开启前后平均差值 LSB */
    uint8_t pass;                       /* 1=PASS, 0=FAIL（或未运行） */
} lis2dh12_self_test_result_t;

/**
 * 运行自检：关自检→读平均→开自检(正)→读平均→关自检→比较差值。
 * @param ctx 设备上下文
 * @param samples 每阶段采样数（建议 32）
 * @param result 输出差值与 pass
 * @return 0 成功
 */
int lis2dh12_config_self_test_run(stmdev_ctx_t *ctx, uint16_t samples,
                                 lis2dh12_self_test_result_t *result);

/* ========== 6. 活动/不活动自动节能 ========== */

/**
 * 配置活动/不活动：ACT_THS 阈值（7 位），ACT_DUR 不活动持续时间（8 位，单位 1/ODR）。
 * 活动/不活动中断映射到 INT2（CTRL_REG6 i2_act=1）。
 */
int lis2dh12_config_activity_inactivity(stmdev_ctx_t *ctx, uint8_t act_ths,
                                        uint8_t act_dur);

#ifdef __cplusplus
}
#endif

#endif /* __LIS2DH12_CONFIG_H */
