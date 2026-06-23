#ifndef __SENSOR_H
#define __SENSOR_H

#include "adc.h"
#include "eeprom_layout.h"
#include <stdbool.h>
#include <stdint.h>

#define lis2dh12_INT1_GPIO_Port   GPIOA
#define lis2dh12_INT1_Pin         GPIO_PIN_11
#define lis2dh12_INT2_GPIO_Port   GPIOA
#define lis2dh12_INT2_Pin         GPIO_PIN_12

#define SENSOR_BATTERY_ADC_SAMPLES 100U

typedef struct {
	uint16_t millivolts;
	uint16_t raw_millivolts;
	uint8_t percent;
	bool valid;
} sensor_battery_t;

uint8_t get_sensor_value(uint16_t *ADC_Value);
uint8_t sensor_init(uint16_t *ADC_Value, ADC_HandleTypeDef adc);
uint8_t sensor_battery_start(uint16_t *adc_values, uint16_t sample_count);
void sensor_battery_poll(void);
bool sensor_get_battery(sensor_battery_t *out);
uint8_t sensor_battery_percent_from_mv(uint16_t millivolts);
void sensor_battery_apply_config(const eeprom_config_t *cfg);
bool sensor_battery_calibrate_gain(uint16_t true_millivolts, uint16_t *out_gain_permyriad);
void sensor_task(void *arg);
/** 执行 LIS2DH12 零 g 校准并保存到 EEPROM（传感器静止、Z 轴向上时调用） */
bool sensor_lis2dh12_calibrate_and_save(void);

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

typedef enum {
	SENSOR_TILT_EVENT_NONE = 0,
	SENSOR_TILT_EVENT_RIGHT,
	SENSOR_TILT_EVENT_LEFT,
} sensor_tilt_event_t;

/** Get latest filtered roll/pitch and rotation state. */
bool sensor_get_attitude(sensor_attitude_t *out);
void sensor_pose_apply_config(const eeprom_config_t *cfg);
bool sensor_pose_capture_config(eeprom_config_t *cfg);
/** Get latest 6D orientation direction (updated on INT1 event). */
sensor_6d_dir_t sensor_get_6d_dir(void);
bool sensor_tilt_event_get(sensor_tilt_event_t *out);
/** Keep LIS2DH12 in high-rate sampling for interactive UI gestures. */
void sensor_request_active(uint32_t hold_ms);
/** Notify sensor task: LIS2DH12 INT2 click IRQ (called from ISR). */
void sensor_notify_motion_irq(void);
#endif
