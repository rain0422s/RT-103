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

void lis2dh12_init(stmdev_ctx_t *dev_ctx);
void lis2dh12_read_data(stmdev_ctx_t *dev_ctx);
void clear_init1(stmdev_ctx_t *dev_ctx);
void enable_fifo_bypass(stmdev_ctx_t *dev_ctx);
void enable_fifo(stmdev_ctx_t *dev_ctx);
void read_fifo(stmdev_ctx_t *dev_ctx);
#endif

