#ifndef __SENSOR_H
#define __SENSOR_H

#include "adc.h"
#include <stdbool.h>

#define lis2dh12_INT1_GPIO_Port   GPIOA
#define lis2dh12_INT1_Pin         GPIO_PIN_11
#define lis2dh12_INT2_GPIO_Port   GPIOA
#define lis2dh12_INT2_Pin         GPIO_PIN_12

uint8_t get_sensor_value(uint16_t *ADC_Value);
uint8_t sensor_init(uint16_t *ADC_Value, ADC_HandleTypeDef adc);
void sensor_task(void *arg);
/** 执行 LIS2DH12 零 g 校准并保存到 EEPROM（传感器静止、Z 轴向上时调用） */
void sensor_lis2dh12_calibrate_and_save(void);

typedef struct {
	float roll_deg;
	float pitch_deg;
	bool rotating;
} sensor_attitude_t;

typedef enum {
	SENSOR_6D_DIR_UNKNOWN = 0,
	SENSOR_6D_DIR_RIGHT,
	SENSOR_6D_DIR_LEFT,
	SENSOR_6D_DIR_FORWARD,
	SENSOR_6D_DIR_BACKWARD,
	SENSOR_6D_DIR_FACE_UP,
	SENSOR_6D_DIR_FACE_DOWN,
} sensor_6d_dir_t;

/** Get latest filtered roll/pitch and rotation state. */
bool sensor_get_attitude(sensor_attitude_t *out);
/** Get latest 6D orientation direction (updated on INT1 event). */
sensor_6d_dir_t sensor_get_6d_dir(void);
/** Notify sensor task that motion interrupt has occurred (called from ISR). */
void sensor_notify_motion_irq(void);
#endif