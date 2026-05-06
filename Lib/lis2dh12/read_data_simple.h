#ifndef __IMU_DEV_H__
#define __IMU_DEV_H__

#include "lis2dh12_reg.h"
#include <string.h>
#include <stdio.h>
#define MKI109V2

// #define NUCLEO_STM32F411RE

#ifdef MKI109V2
#include "stm32f1xx_hal.h"
// #include "usbd_cdc_if.h"
#include "spi.h"
#include "i2c.h"
#endif

#ifdef NUCLEO_STM32F411RE
#include "stm32f4xx_hal.h"
#include "i2c.h"
#include "usart.h"
#include "gpio.h"
#endif

void lis2dh12_init(void);
/** Return the device context used in init (for read_data/calibrate). */
stmdev_ctx_t *lis2dh12_get_ctx(void);
void lis2dh12_read_data(stmdev_ctx_t *dev_ctx);
/** Get latest acceleration in mg from last successful read_data call. */
uint8_t lis2dh12_get_last_accel_mg(float *x_mg, float *y_mg, float *z_mg);

/** Zero-g calibration offset (LSB). Apply: calibrated_raw = raw - offset. */
void lis2dh12_set_calib_offset(int16_t x, int16_t y, int16_t z);
void lis2dh12_get_calib_offset(int16_t *x, int16_t *y, int16_t *z);
/** Run calibration: sensor must be still, Z-axis up. Collects samples and sets offset (store to EEPROM via storage_save_lis2dh12_calib). */
uint8_t lis2dh12_calibrate(stmdev_ctx_t *dev_ctx);

void clear_init1(stmdev_ctx_t *dev_ctx);
void enable_fifo_bypass(stmdev_ctx_t *dev_ctx);
void enable_fifo(stmdev_ctx_t *dev_ctx);
void read_fifo(stmdev_ctx_t *dev_ctx);
#endif

