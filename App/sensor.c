#include "sensor.h"
#include "led_control.h"
#include "utils.h"
#include "storage.h"
#include "read_data_simple.h"
#include "eeprom_layout.h"
#include <math.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.1415926f
#endif

#define ATT_LPF_ALPHA                  0.12f
#define ROTATION_RATE_THRESHOLD_DPS    15.0f
#define ROTATION_DYNACC_THRESHOLD_MG   120.0f

static sensor_attitude_t s_att = {0};
static bool s_att_valid;
static float s_ax_f;
static float s_ay_f;
static float s_az_f;
static uint32_t s_last_update_ms;

static float rad_to_deg(float rad)
{
	return rad * (180.0f / M_PI);
}

static float angle_delta_deg(float now_deg, float prev_deg)
{
	float d = now_deg - prev_deg;
	while (d > 180.0f) d -= 360.0f;
	while (d < -180.0f) d += 360.0f;
	return d;
}

static void sensor_update_attitude(float ax_mg, float ay_mg, float az_mg)
{
	const uint32_t now_ms = HAL_GetTick();
	float dt_s = 0.0f;
	if (s_last_update_ms != 0U && now_ms > s_last_update_ms) {
		dt_s = (float)(now_ms - s_last_update_ms) / 1000.0f;
	}
	s_last_update_ms = now_ms;

	if (!s_att_valid) {
		s_ax_f = ax_mg;
		s_ay_f = ay_mg;
		s_az_f = az_mg;
		s_att_valid = true;
	}

	s_ax_f += ATT_LPF_ALPHA * (ax_mg - s_ax_f);
	s_ay_f += ATT_LPF_ALPHA * (ay_mg - s_ay_f);
	s_az_f += ATT_LPF_ALPHA * (az_mg - s_az_f);

	const float denom = sqrtf(s_ay_f * s_ay_f + s_az_f * s_az_f);
	const float roll_deg = rad_to_deg(atan2f(s_ay_f, s_az_f));
	const float pitch_deg = rad_to_deg(atan2f(-s_ax_f, (denom > 1e-6f) ? denom : 1e-6f));

	float roll_rate_dps = 0.0f;
	float pitch_rate_dps = 0.0f;
	if (dt_s > 1e-3f) {
		roll_rate_dps = fabsf(angle_delta_deg(roll_deg, s_att.roll_deg) / dt_s);
		pitch_rate_dps = fabsf(angle_delta_deg(pitch_deg, s_att.pitch_deg) / dt_s);
	}

	const float acc_norm = sqrtf(s_ax_f * s_ax_f + s_ay_f * s_ay_f + s_az_f * s_az_f);
	const float dyn_acc = fabsf(acc_norm - 1000.0f);

	s_att.roll_deg = roll_deg;
	s_att.pitch_deg = pitch_deg;
	s_att.rotating = (roll_rate_dps > ROTATION_RATE_THRESHOLD_DPS) ||
	                 (pitch_rate_dps > ROTATION_RATE_THRESHOLD_DPS) ||
	                 (dyn_acc > ROTATION_DYNACC_THRESHOLD_MG);
}

uint8_t sensor_init(uint16_t *ADC_Value, ADC_HandleTypeDef adc)
{
	HAL_ADCEx_Calibration_Start(&adc);
	HAL_ADC_Start_DMA(&adc, (uint32_t *)ADC_Value, 100);
	return 0;
}

uint8_t get_sensor_value(uint16_t *ADC_Value)
{
	uint32_t sum = 0;
	for (int i = 0; i < 100; i++)
		sum += ADC_Value[i];

	/* Avoid float printf (big flash cost). Print fixed-point voltage (mV). */
	const uint32_t adc_avg = sum / 100u;               /* 0..4095 */
	const uint32_t adc_mV  = (adc_avg * 3300u) / 4096u;
	/* Keep calculations local; DBG_PRINTF compiles out when DEBUG_PRINT=0. */
	DBG_PRINTF("\r\n ADC: %lu.%03lu V\r\n",
	           (unsigned long)(adc_mV / 1000u),
	           (unsigned long)(adc_mV % 1000u));
	delay_ms(300);
	delay_ms(500);
	return 0;
}

void sensor_task(void *arg)
{
	(void)arg;
	/* LIS2DH12 初始化并加载 EEPROM 中保存的零 g 校准（若有） */
	lis2dh12_init();
	storage_eeprom_init();
	lis2dh12_calib_t cal;
	if (storage_load_lis2dh12_calib(&cal))
		lis2dh12_set_calib_offset(cal.offset_x, cal.offset_y, cal.offset_z);

	for (;;) {
		breathing_led_run_once();
		lis2dh12_read_data(lis2dh12_get_ctx());
		float ax_mg, ay_mg, az_mg;
		if (lis2dh12_get_last_accel_mg(&ax_mg, &ay_mg, &az_mg)) {
			sensor_update_attitude(ax_mg, ay_mg, az_mg);
			DBG_PRINTF("attitude roll/pitch=%0.2f/%0.2f deg, rotating=%d\n",
			           s_att.roll_deg, s_att.pitch_deg, s_att.rotating ? 1 : 0);
		}
		delay_ms(50);
	}
}

/**
 * 执行一次 LIS2DH12 零 g 校准，并将 s_calib_offset 保存到 EEPROM。
 * 调用前传感器需静止、Z 轴向上。可由 UI 菜单项或调试命令触发。
 */
void sensor_lis2dh12_calibrate_and_save(void)
{
	stmdev_ctx_t *ctx = lis2dh12_get_ctx();
	if (lis2dh12_calibrate(ctx) != 0)
		return;
	lis2dh12_calib_t cal = {
		.magic   = EEPROM_CALIB_MAGIC,
		.offset_x = 0,
		.offset_y = 0,
		.offset_z = 0,
	};
	lis2dh12_get_calib_offset(&cal.offset_x, &cal.offset_y, &cal.offset_z);
	storage_eeprom_init();
	storage_save_lis2dh12_calib(&cal);
}

bool sensor_get_attitude(sensor_attitude_t *out)
{
	if (!out || !s_att_valid)
		return false;
	*out = s_att;
	return true;
}