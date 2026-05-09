/*
 ******************************************************************************
 * @file    read_data_simple.c
 * @author  MEMS Software Solution Team
 * @date    05-October-2017
 * @brief   LIS2DH12 驱动：平台读写、初始化、轮询读、校准、FIFO/中断/活动识别封装。
 *
 * 功能概要：
 *   - 平台层：platform_write / platform_read（I2C/SPI，支持多字节自动递增）
 *   - 初始化：lis2dh12_init()，工作模式/ODR/量程在 init 中设定
 *   - 轮询读：lis2dh12_read_data()，依据 STATUS_REG.ZYXDA 读加速度并换算 mg、倾角
 *   - 校准：lis2dh12_calibrate()、set_calib_offset/get_calib_offset（零 g 偏移）
 *   - FIFO/中断/活动：enable_fifo_bypass、enable_fifo、read_fifo、enable_inertial_wakeup、
 *     enable_activity_recognition、clear_init1 等，部分内部复用 lis2dh12_config API
 *
 * 更多模式/FIFO/中断/自检/活动-不活动配置见：lis2dh12_config.h
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; COPYRIGHT(c) 2017 STMicroelectronics</center></h2>
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright notice,
 *      this list of conditions and the following disclaimer in the documentation
 *      and/or other materials provided with the distribution.
 *   3. Neither the name of STMicroelectronics nor the names of its contributors
 *      may be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "read_data_simple.h"
#include "lis2dh12_config.h"

/* ==================== 板级配置 ==================== */
/* LIS2DH12 使用 SPI2，CS 引脚如下。若板子不同请修改 CS 宏。 */
#ifdef MKI109V2
#define CS_SPI2_GPIO_Port   GPIOB
#define CS_SPI2_Pin         GPIO_PIN_12
#define CS_SPI1_GPIO_Port   GPIOA
#define CS_SPI1_Pin         GPIO_PIN_4
#endif

#ifdef NUCLEO_STM32F411RE
/* N/A on NUCLEO_STM32F411RE + IKS01A1 */
/* N/A on NUCLEO_STM32F411RE + IKS01A2 */
#define CS_SPI2_GPIO_Port   0
#define CS_SPI2_Pin         0
#define CS_SPI1_GPIO_Port   0
#define CS_SPI1_Pin         0
#endif

#define TX_BUF_DIM          1000
#define DEGREE_CAL          (180.0f / 3.1416f)
#define FILTER_CNT          4

/** 单次采样：加速度 mg + 三轴倾角（非标准 Pitch/Roll，为各轴相对另两轴夹角） */
typedef struct {
        short x, y, z;
        short new_angle_x, new_angle_y, new_angle_z;
        short old_angle_x, old_angle_y, old_angle_z;
} axis_info_t;

typedef struct filter_avg {
        axis_info_t info[FILTER_CNT];
        unsigned char count;
} filter_avg_t;

/* ==================== 模块内全局/静态变量 ==================== */
static axis3bit16_t data_raw_acceleration;
static axis1bit16_t data_raw_temperature;
static float acceleration_mg[3];
static float temperature_degC;
static uint8_t whoamI;
static uint8_t tx_buffer[TX_BUF_DIM];
static uint8_t s_accel_valid;

/** 零 g 校准偏移 (LSB)。应用方式：calibrated_raw = raw - offset。2g HR 下 1g ≈ 1024 LSB */
static int16_t s_calib_offset[3] = { 0, 0, 0 };
#define LIS2DH12_CALIB_SAMPLES  64
#define LIS2DH12_2G_HR_1G_LSB   1024
#ifndef M_PI
#define M_PI 3.14159f
#endif

/* ==================== 平台层：SPI/I2C 读写 ====================
 * 由 lis2dh12_reg 通过 ctx->write_reg/read_reg 调用。
 * handle 区分 hi2c1 / hspi1 / hspi2，实现多字节时寄存器地址需带自动递增位（I2C 0x80，SPI 写 0x40、读 0xC0）。
 */

/** 写寄存器：Reg 为起始地址，Bufp/len 为数据。多字节时地址已带自动递增位。 */
static int32_t platform_write(void *handle, uint8_t Reg, const uint8_t *Bufp,
                              uint16_t len)
{
        if (handle == &hi2c1) {
                /* Auto-increment (datasheet: MSB of reg addr = 1 for multi-byte) */
                Reg |= 0x80;
                /* HAL expects 7-bit I2C addr; ADD_H 0x33 = (0x19<<1)|1 → use 0x19<<1 */
                HAL_I2C_Mem_Write(handle, (LIS2DH12_I2C_ADD_H & 0xFE), Reg,
                                I2C_MEMADD_SIZE_8BIT, Bufp, len, 1000);
        }
        #ifdef MKI109V2  
        else if (handle == &hspi2){
                /* enable auto incremented in multiple read/write commands */
                Reg |= 0x40;    
                HAL_GPIO_WritePin(CS_SPI2_GPIO_Port, CS_SPI2_Pin, GPIO_PIN_RESET);
                HAL_SPI_Transmit(handle, &Reg, 1, 1000);
                HAL_SPI_Transmit(handle, Bufp, len, 1000);
                HAL_GPIO_WritePin(CS_SPI2_GPIO_Port, CS_SPI2_Pin, GPIO_PIN_SET);
        }
        else if (handle == &hspi1){
                /* enable auto incremented in multiple read/write commands */
                Reg |= 0x40;     
                HAL_GPIO_WritePin(CS_SPI1_GPIO_Port, CS_SPI1_Pin, GPIO_PIN_RESET);
                HAL_SPI_Transmit(handle, &Reg, 1, 1000);
                HAL_SPI_Transmit(handle, Bufp, len, 1000);
                HAL_GPIO_WritePin(CS_SPI1_GPIO_Port, CS_SPI1_Pin, GPIO_PIN_SET);
        }
        #endif

        return 0;
}

/** 读寄存器：Reg 为起始地址，读回 len 字节到 Bufp。多字节时 Reg 已带 0x80|0x40。 */
static int32_t platform_read(void *handle, uint8_t Reg, uint8_t *Bufp,
                             uint16_t len)
{
        if (handle == &hi2c1) {
                Reg |= 0x80;
                HAL_I2C_Mem_Read(handle, (LIS2DH12_I2C_ADD_H & 0xFE), Reg,
                                I2C_MEMADD_SIZE_8BIT, Bufp, len, 1000);
        }
        #ifdef MKI109V2   
        else if (handle == &hspi2){               
                /* enable auto incremented in multiple read/write commands */
                Reg |= 0xC0;
                HAL_GPIO_WritePin(CS_SPI2_GPIO_Port, CS_SPI2_Pin, GPIO_PIN_RESET);
                HAL_SPI_Transmit(handle, &Reg, 1, 1000);
                HAL_SPI_Receive(handle, Bufp, len, 1000);
                HAL_GPIO_WritePin(CS_SPI2_GPIO_Port, CS_SPI2_Pin, GPIO_PIN_SET);
        }else if (handle == &hspi1){
                /* enable auto incremented in multiple read/write commands */
                Reg |= 0xC0;    
                HAL_GPIO_WritePin(CS_SPI1_GPIO_Port, CS_SPI1_Pin, GPIO_PIN_RESET);
                HAL_SPI_Transmit(handle, &Reg, 1, 1000);
                HAL_SPI_Receive(handle, Bufp, len, 1000);
                HAL_GPIO_WritePin(CS_SPI1_GPIO_Port, CS_SPI1_Pin, GPIO_PIN_SET);
        }
        #endif
        return 0;
}

/** 调试输出：MKI109V2 用 printf，NUCLEO 用 UART。 */
void tx_com(uint8_t *tx_buffer, uint16_t len)
{
#ifdef NUCLEO_STM32F411RE  
        HAL_UART_Transmit( &huart2, tx_buffer, len, 1000 );
#endif
#ifdef MKI109V2  
        //CDC_Transmit_FS( tx_buffer, len );
        printf("[lis2dh12]:%s\r\n", tx_buffer);
#endif
}

/* ==================== 阈值/时间换算（用于 INT/ACT 寄存器） ====================
 * 数据手册：Normal 模式 2g 时 1 LSB = 16 mg；4g=32 mg；8g=62 mg；16g=186 mg。
 * 返回值用于 INTx_THS、ACT_THS 等 7 位阈值寄存器。
 */
uint8_t set_mg(stmdev_ctx_t *dev_ctx, uint16_t mg)
{
        uint8_t one_lsb;
        uint16_t act_ths;

        if (!dev_ctx) return 0;
        switch (dev_ctx->fs) {
                case LIS2DH12_2g:
                        one_lsb = 16;
                        act_ths = (mg + (one_lsb >> 1)) >> 4;
                        break;
                case LIS2DH12_4g:
                        one_lsb = 32;
                        act_ths = (mg + (one_lsb >> 1)) >> 5;
                        break;
                case LIS2DH12_8g:
                        one_lsb = 62;
                        act_ths = (mg + (one_lsb / 2)) / one_lsb;
                        break;
                case LIS2DH12_16g:
                        one_lsb = 186;
                        act_ths = (mg + (one_lsb / 2)) / one_lsb;
                        break;
                default:
                        return 0;
        }
        return (act_ths > 0x7F) ? 0x7F : (uint8_t)act_ths;
}

/** 将“时间（秒）”转为 ACT_DUR 寄存器值。单位：1/ODR。公式：(time*ODR-1+4)>>3，限制在 0~255。 */
uint8_t set_time(stmdev_ctx_t *dev_ctx, uint16_t time)
{
        uint16_t odr = 1;
        uint16_t act_dur;

        if (!dev_ctx) return 0;
        switch (dev_ctx->odr) {
                case LIS2DH12_ODR_1Hz:   odr = 1;    break;
                case LIS2DH12_ODR_10Hz:  odr = 10;   break;
                case LIS2DH12_ODR_25Hz:  odr = 25;   break;
                case LIS2DH12_ODR_50Hz:  odr = 50;   break;
                case LIS2DH12_ODR_100Hz: odr = 100;  break;
                case LIS2DH12_ODR_200Hz: odr = 200; break;
                case LIS2DH12_ODR_400Hz: odr = 400;  break;
                case LIS2DH12_ODR_1kHz620_LP:
                        odr = 1620;
                        break;
                case LIS2DH12_ODR_5kHz376_LP_1kHz344_NM_HP:
                        odr = (dev_ctx->mode == LIS2DH12_LP_8bit) ? 5376u : 1344u;
                        break;
                default:
                        return 0;
        }
        act_dur = ((time * odr - 1) + 4) >> 3;
        return (act_dur > 0xFF) ? 0xFF : (uint8_t)act_dur;
}
/* ==================== FIFO 封装（内部复用 lis2dh12_config） ==================== */

/** 设置为 Bypass 模式：FIFO 不缓冲，数据直通。可选调试打印空标志。 */
void enable_fifo_bypass(stmdev_ctx_t *dev_ctx)
{
        if (!dev_ctx) return;
        lis2dh12_config_fifo_set_mode(dev_ctx, LIS2DH12_FIFO_BYPASS);
#ifdef MKI109V2
        uint8_t val;
        lis2dh12_fifo_empty_flag_get(dev_ctx, &val);
        printf("fifo_empty_flag:%d\n", val);
#endif
}

/** 使能 FIFO 并设为 FIFO 模式（写满水印后停写，等主机读空）。水印/溢出中断见 lis2dh12_config_fifo_int1。 */
void enable_fifo(stmdev_ctx_t *dev_ctx)
{
        if (!dev_ctx) return;
        lis2dh12_config_fifo_enable(dev_ctx, 1);
        lis2dh12_config_fifo_set_mode(dev_ctx, LIS2DH12_FIFO_FIFO);
}

/** 批量读 FIFO 并逐样本转 mg 通过 tx_com 输出。使用 config 批量读，读速需 ≥ ODR 以免溢出。 */
void read_fifo(stmdev_ctx_t *dev_ctx)
{
        if (!dev_ctx) return;
        int16_t buf[30 * 3];
        int n = lis2dh12_config_fifo_batch_read(dev_ctx, buf, 30);
#ifdef MKI109V2
        printf("fifo data level read: %d\n", n);
#endif
        for (int i = 0; i < n; i++) {
                acceleration_mg[0] = LIS2DH12_FROM_FS_2g_HR_TO_mg(buf[i * 3 + 0]);
                acceleration_mg[1] = LIS2DH12_FROM_FS_2g_HR_TO_mg(buf[i * 3 + 1]);
                acceleration_mg[2] = LIS2DH12_FROM_FS_2g_HR_TO_mg(buf[i * 3 + 2]);
                sprintf((char *)tx_buffer, "Acceleration [mg]:%4.2f\t%4.2f\t%4.2f\r\n",
                        acceleration_mg[0], acceleration_mg[1], acceleration_mg[2]);
                tx_com(tx_buffer, strlen((char const *)tx_buffer));
        }
}

/* ==================== 中断与唤醒（INT1 惯性唤醒 + 活动/不活动 INT2） ====================
 * 惯性唤醒：ODR 100Hz，高通滤波，INT1 阈值 250 mg，读 REFERENCE 锁参考，INT1_CFG 全轴使能。
 * 发生唤醒后需读 INT1_SRC 清除锁存，见 clear_init1。
 */

void enable_inertial_wakeup(stmdev_ctx_t *dev_ctx)
{
        if (!dev_ctx) return;
        uint8_t ctrl_reg;
        /* CTRL_REG1: ODR=100Hz, XYZ 使能 */
        ctrl_reg = 0x57;
        lis2dh12_write_reg(dev_ctx, LIS2DH12_CTRL_REG1, (uint8_t *)&ctrl_reg, 1);
        /* CTRL_REG2: 高通滤波使能（用于唤醒检测） */
        ctrl_reg = 0x09;
        lis2dh12_write_reg(dev_ctx, LIS2DH12_CTRL_REG2, (uint8_t *)&ctrl_reg, 1);
        /* CTRL_REG3: INT1 映射到 IA1（惯性唤醒） */
        ctrl_reg = 0x40;
        lis2dh12_write_reg(dev_ctx, LIS2DH12_CTRL_REG3, (uint8_t *)&ctrl_reg, 1);
        /* CTRL_REG4: ±2g */
        ctrl_reg = 0x00;
        lis2dh12_write_reg(dev_ctx, LIS2DH12_CTRL_REG4, (uint8_t *)&ctrl_reg, 1);
        /* CTRL_REG5: INT1 锁存 */
        ctrl_reg = 0x08;
        lis2dh12_write_reg(dev_ctx, LIS2DH12_CTRL_REG5, (uint8_t *)&ctrl_reg, 1);
        /* INT1 阈值 250 mg，持续时间 0 */
        lis2dh12_int1_gen_threshold_set(dev_ctx, set_mg(dev_ctx, 250));
        lis2dh12_int1_gen_duration_set(dev_ctx, 0x00);
        /* 读 REFERENCE 将高通参考设为当前加速度 */
        lis2dh12_filter_reference_get(dev_ctx, &ctrl_reg);
        /* INT1_CFG: 各轴高/低均参与（0x3F），用于唤醒 */
        ctrl_reg = 0x3F;
        lis2dh12_int1_gen_conf_set(dev_ctx, (lis2dh12_int1_cfg_t *)&ctrl_reg);
#ifdef MKI109V2
        lis2dh12_read_reg(dev_ctx, LIS2DH12_CTRL_REG1, &ctrl_reg, 1);
        printf("CTRL_REG1:0x%x\n", ctrl_reg);
#endif
}

/** 读 INT1_SRC 并打印（读操作即清除 INT1 锁存）。若还需清除 Click 锁存可再调 lis2dh12_config_int1_clear_source。 */
void clear_init1(stmdev_ctx_t *dev_ctx)
{
        if (!dev_ctx) return;
        uint8_t src_byte;
        lis2dh12_int1_gen_source_get(dev_ctx, (lis2dh12_int1_src_t *)&src_byte);
#ifdef MKI109V2
        printf("INT1_SRC:0x%02x\n", src_byte);
#endif
}

/** 使能活动/不活动：先配置惯性唤醒（INT1），再设 ACT 阈值 200 mg、不活动约 2 s，并映射到 INT2。 */
void enable_activity_recognition(stmdev_ctx_t *dev_ctx)
{
        if (!dev_ctx) return;
        enable_inertial_wakeup(dev_ctx);
        lis2dh12_config_activity_inactivity(dev_ctx,
                set_mg(dev_ctx, 200),  /* 活动阈值约 200 mg */
                set_time(dev_ctx, 2)); /* 不活动判定约 2 s */
        lis2dh12_operating_mode_set(dev_ctx, dev_ctx->mode);
}

/**
 * 高分辨率模式切换示例：HR → 掉电 → 读 REFERENCE（复位滤波）→ 再回 HR、ODR 100Hz。
 * 等价思路可用 lis2dh12_config_set_mode(ctx, LIS2DH12_MODE_HIGH_RESOLUTION, LIS2DH12_ODR_100Hz)
 * 后若从掉电恢复，先读 REFERENCE 再设模式。
 */
void enable_high_resolution_mode(stmdev_ctx_t *dev_ctx)
{
        if (!dev_ctx) return;
        uint8_t ctrl_reg;
        lis2dh12_ctrl_reg4_t ctrl_reg4;
        ctrl_reg4.hr = 1;
        lis2dh12_write_reg(dev_ctx, LIS2DH12_CTRL_REG4, (uint8_t *)&ctrl_reg4, 1);
        ctrl_reg = 0x57;
        lis2dh12_write_reg(dev_ctx, LIS2DH12_CTRL_REG1, (uint8_t *)&ctrl_reg, 1);
        /* 进入掉电再切回时，需读 REFERENCE 复位内部滤波 */
        ctrl_reg = 0x07;
        lis2dh12_write_reg(dev_ctx, LIS2DH12_CTRL_REG1, (uint8_t *)&ctrl_reg, 1);
        lis2dh12_filter_reference_get(dev_ctx, &ctrl_reg);
        ctrl_reg = 0x57;
        lis2dh12_write_reg(dev_ctx, LIS2DH12_CTRL_REG1, (uint8_t *)&ctrl_reg, 1);
#ifdef MKI109V2
        lis2dh12_read_reg(dev_ctx, LIS2DH12_CTRL_REG1, &ctrl_reg, 1);
        printf("CTRL_REG1:0x%x\n", ctrl_reg);
#endif
}

/* ==================== 设备上下文与初始化 ==================== */

/* Single device context: static storage (BSS), allocated at load time, not heap — no fragmentation. */
static stmdev_ctx_t s_dev_ctx;

stmdev_ctx_t *lis2dh12_get_ctx(void)
{
        return &s_dev_ctx;
}

/** 初始化：绑定 platform 读写、SPI2、2g/400Hz/HR，并校验 WHO_AM_I（接受 0x33 或 0x41）。 */
void lis2dh12_init(void)
{
        s_dev_ctx.write_reg = platform_write;
        s_dev_ctx.read_reg  = platform_read;
        s_dev_ctx.handle    = &hspi2;  /* LIS2DH12 on SPI2; CS = CS_SPI2_GPIO_Port/Pin (GPIOC, PIN_7) */
        s_dev_ctx.fs        = LIS2DH12_2g;
        s_dev_ctx.odr       = LIS2DH12_ODR_400Hz;
        s_dev_ctx.mode      = LIS2DH12_HR_12bit;

        whoamI = 0;
        lis2dh12_device_id_get(&s_dev_ctx, &whoamI);
        /* WHO_AM_I reg 0x0F: C110926 LIS2DH12TR 规格书为 0x33；部分型号/批次返回 0x41。SPI/I2C 同寄存器同值，接受 0x33 与 0x41。 */
        if (whoamI != LIS2DH12_ID && whoamI != 0x33U) {
                printf("lis2dh12 error (whoamI=0x%02x)\n", (unsigned)whoamI);
        } else {
                printf("lis2dh12 SUCCESS\n");
        }

        lis2dh12_data_rate_set(&s_dev_ctx, s_dev_ctx.odr);
        lis2dh12_full_scale_set(&s_dev_ctx, s_dev_ctx.fs);
        lis2dh12_operating_mode_set(&s_dev_ctx, s_dev_ctx.mode);
}

/** 设置零 g 校准偏移 (LSB)，读数据时会做 raw - offset。 */
void lis2dh12_set_calib_offset(int16_t x, int16_t y, int16_t z)
{
        s_calib_offset[0] = x;
        s_calib_offset[1] = y;
        s_calib_offset[2] = z;
}

void lis2dh12_get_calib_offset(int16_t *x, int16_t *y, int16_t *z)
{
        if (x) *x = s_calib_offset[0];
        if (y) *y = s_calib_offset[1];
        if (z) *z = s_calib_offset[2];
}

/** 校准：传感器静止、Z 轴向上，采多组取平均，计算 offset 使静止时约为 (0,0,1g)。结果可存 EEPROM。 */
uint8_t lis2dh12_calibrate(stmdev_ctx_t *dev_ctx)
{
        int32_t sum[3] = { 0, 0, 0 };
        uint16_t n;
        const int32_t one_g_raw = ((int32_t)LIS2DH12_2G_HR_1G_LSB << 4);
        if (!dev_ctx) return 1;
        for (n = 0; n < LIS2DH12_CALIB_SAMPLES; n++) {
                int16_t raw[3];
                lis2dh12_acceleration_raw_get(dev_ctx, raw);
                sum[0] += raw[0];
                sum[1] += raw[1];
                sum[2] += raw[2];
                HAL_Delay(5);
        }
        s_calib_offset[0] = (int16_t)(sum[0] / (int32_t)LIS2DH12_CALIB_SAMPLES);
        s_calib_offset[1] = (int16_t)(sum[1] / (int32_t)LIS2DH12_CALIB_SAMPLES);
        {
                const int32_t avg_z = (sum[2] / (int32_t)LIS2DH12_CALIB_SAMPLES);
                const int32_t target_z = (avg_z >= 0) ? one_g_raw : -one_g_raw;
                s_calib_offset[2] = (int16_t)(avg_z - target_z);
        }
        return 0;
}

/* ==================== 轮询读（无中断） ====================
 * 依据 STATUS_REG.ZYXDA 读一次加速度，减校准偏移后转 mg，并更新三轴倾角（各轴相对另两轴夹角，非标准 Pitch/Roll）。
 * 若有温度就绪则读温度。如需 DRDY 中断或 BDU 防错位，见 lis2dh12_config_enable_drdy_int1 / lis2dh12_config_enable_bdu。
 */

void lis2dh12_read_data(stmdev_ctx_t *dev_ctx)
{
        axis_info_t sample;
        lis2dh12_reg_t reg;

        if (!dev_ctx) return;

        lis2dh12_status_get(dev_ctx, &reg.status_reg);
        if (reg.status_reg.zyxda) {
                memset(data_raw_acceleration.u8bit, 0x00, 3 * sizeof(int16_t));
                lis2dh12_acceleration_raw_get(dev_ctx, data_raw_acceleration.i16bit);
                data_raw_acceleration.i16bit[0] -= s_calib_offset[0];
                data_raw_acceleration.i16bit[1] -= s_calib_offset[1];
                data_raw_acceleration.i16bit[2] -= s_calib_offset[2];
                acceleration_mg[0] = LIS2DH12_FROM_FS_2g_HR_TO_mg(data_raw_acceleration.i16bit[0]);
                acceleration_mg[1] = LIS2DH12_FROM_FS_2g_HR_TO_mg(data_raw_acceleration.i16bit[1]);
                acceleration_mg[2] = LIS2DH12_FROM_FS_2g_HR_TO_mg(data_raw_acceleration.i16bit[2]);
                s_accel_valid = 1U;
                sprintf((char *)tx_buffer, "Acceleration [mg]:%4.2f\t%4.2f\t%4.2f\r\n",
                        acceleration_mg[0], acceleration_mg[1], acceleration_mg[2]);
                /* tx_com(tx_buffer, strlen((char const *)tx_buffer)); */
        }

        lis2dh12_temp_data_ready_get(dev_ctx, &reg.byte);
        if (reg.byte) {
                memset(data_raw_temperature.u8bit, 0x00, sizeof(int16_t));
                lis2dh12_temperature_raw_get(dev_ctx, &data_raw_temperature.i16bit);
                temperature_degC = LIS2DH12_FROM_LSB_TO_degC_HR(data_raw_temperature.i16bit);
                sprintf((char *)tx_buffer, "Temperature [degC]:%6.2f\r\n", temperature_degC);
                /* tx_com(tx_buffer, strlen((char const *)tx_buffer)); */
        }

        sample.x = (short)acceleration_mg[0];
        sample.y = (short)acceleration_mg[1];
        sample.z = (short)acceleration_mg[2];
        sample.new_angle_x = (short)(atan((float)sample.x / (float)sqrt(pow(sample.y, 2) + pow(sample.z, 2))) * DEGREE_CAL);
        sample.new_angle_y = (short)(atan((float)sample.y / (float)sqrt(pow(sample.x, 2) + pow(sample.z, 2))) * DEGREE_CAL);
        sample.new_angle_z = (short)(atan((float)sample.z / (float)sqrt(pow(sample.x, 2) + pow(sample.y, 2))) * DEGREE_CAL);
        if (sample.new_angle_z < 0) {
                sample.new_angle_x = (short)(180 - sample.new_angle_x);
                sample.new_angle_y = (short)(180 - sample.new_angle_y);
        }

        /* 实时日志关闭：减少串口输出干扰。 */
        /* sprintf((char *)tx_buffer,
         *         "LIS2DH12 acc[mg]:%4.2f %4.2f %4.2f | temp[degC]:%6.2f | angle[deg]:%3d %3d %3d\r\n",
         *         acceleration_mg[0], acceleration_mg[1], acceleration_mg[2],
         *         temperature_degC,
         *         sample.new_angle_x, sample.new_angle_y, sample.new_angle_z);
         * tx_com(tx_buffer, strlen((char const *)tx_buffer)); */

        (void)sample;
}

uint8_t lis2dh12_get_last_accel_mg(float *x_mg, float *y_mg, float *z_mg)
{
        if (!s_accel_valid || !x_mg || !y_mg || !z_mg) {
                return 0U;
        }
        *x_mg = acceleration_mg[0];
        *y_mg = acceleration_mg[1];
        *z_mg = acceleration_mg[2];
        return 1U;
}