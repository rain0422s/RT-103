#ifndef __SENSOR_H
#define __SENSOR_H

#include "adc.h"
#include "sht3x.h"
#include "crc8.h"

#include "read_data_simple.h"
// #define MPU6050

#ifdef MPU6050
#include "atk_ms6050.h"
void get_mpu6050_value(void);
uint8_t mpu6050_init(void);
#endif

uint8_t get_sensor_value(SHT3xObjectType sht,uint16_t *ADC_Value);
uint8_t sensor_init(SHT3xObjectType sht,uint16_t *ADC_Value,ADC_HandleTypeDef adc,lis2dh12_ctx_t dev_ctx);


#endif