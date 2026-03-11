#ifndef __SENSOR_H
#define __SENSOR_H

#include "adc.h"
#include "sht3x.h"
#include "crc8.h"
#include "utils.h"
// #include "atk_ms6050.h"

#define lis2dh12_INT1_GPIO_Port   GPIOA
#define lis2dh12_INT1_Pin         GPIO_PIN_11
#define lis2dh12_INT2_GPIO_Port   GPIOA
#define lis2dh12_INT2_Pin         GPIO_PIN_12

uint8_t get_sensor_value(SHT3xObjectType sht,uint16_t *ADC_Value);
uint8_t sensor_init(SHT3xObjectType sht,uint16_t *ADC_Value,ADC_HandleTypeDef adc);
void sensor_task(void *arg);
/** 执行 LIS2DH12 零 g 校准并保存到 EEPROM（传感器静止、Z 轴向上时调用） */
void sensor_lis2dh12_calibrate_and_save(void);
// void get_mpu6050_value(void);
// uint8_t mpu6050_init(void);
#endif