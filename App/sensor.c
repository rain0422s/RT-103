#include "sensor.h"
#include "led_control.h"
#include "utils.h"
#include "storage.h"
#include "read_data_simple.h"
#include "lis2dh12_config.h"
#include "eeprom_layout.h"
#include "FreeRTOS.h"
#include "task.h"
#include <math.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.1415926f
#endif

#define ATT_LPF_ALPHA                  0.12f
#define ROTATION_RATE_THRESHOLD_DPS    15.0f
#define ROTATION_DYNACC_THRESHOLD_MG   120.0f
#define SENSOR_ACTIVE_SAMPLE_MS        50U
#define SENSOR_IDLE_SAMPLE_MS          500U
#define SENSOR_INACTIVITY_TIMEOUT_MS   5000U
#define INT2_WAKE_GUARD_MS             300U

static sensor_attitude_t s_att = {0};
static bool s_att_valid;
static float s_ax_f;
static float s_ay_f;
static float s_az_f;
static uint32_t s_last_update_ms;
static float s_roll_rate_signed_dps;
static float s_pitch_rate_signed_dps;
static volatile uint8_t s_motion_irq_pending;
static uint8_t s_int1_last_level;
static uint8_t s_sensor_low_power_mode;

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

static const char *sensor_rotation_direction_str(void)
{
	if (!s_att.rotating) {
		return "STILL";
	}

	const float abs_roll_rate = fabsf(s_roll_rate_signed_dps);
	const float abs_pitch_rate = fabsf(s_pitch_rate_signed_dps);
	if (abs_roll_rate >= abs_pitch_rate) {
		return (s_roll_rate_signed_dps >= 0.0f) ? "PITCH_UP" : "PITCH_DOWN";
	}
	return (s_pitch_rate_signed_dps >= 0.0f) ? "ROLL_RIGHT" : "ROLL_LEFT";
}

static void sensor_config_int1_int2(stmdev_ctx_t *ctx)
{
	/* INT1: 6D orientation detect, mapped to PA11 (input/polling). */
	(void)lis2dh12_config_int1_6d_4d(
		ctx,
		1, /* six_d */
		1, 1, 1, 1, 1, 1, /* xh/xl/yh/yl/zh/zl */
		18, /* threshold */
		1   /* duration */
	);

	/* INT2: activity/inactivity mapped to PA12 (EXTI interrupt). */
	(void)lis2dh12_config_activity_inactivity(ctx, 12, 25);
	/* Make INT2 more robust for MCU capture:
	 * - explicit active-high polarity
	 * - latched interrupt (cleared by reading INT2_SRC)
	 */
	lis2dh12_ctrl_reg6_t c6;
	if (lis2dh12_pin_int2_config_get(ctx, &c6) == 0) {
		c6.int_polarity = 0; /* active-high */
		c6.i2_act = 1;
		(void)lis2dh12_pin_int2_config_set(ctx, &c6);
	}
	(void)lis2dh12_int2_pin_notification_mode_set(ctx, LIS2DH12_INT2_LATCHED);
}

static void sensor_set_power_mode(stmdev_ctx_t *ctx, uint8_t low_power)
{
	if (low_power) {
		if (!s_sensor_low_power_mode) {
			(void)lis2dh12_config_set_mode(ctx, LIS2DH12_MODE_LOW_POWER, LIS2DH12_ODR_10Hz);
			s_sensor_low_power_mode = 1U;
			DBG_PRINTF("LIS2DH12 -> LOW_POWER @10Hz\n");
		}
	} else {
		if (s_sensor_low_power_mode) {
			(void)lis2dh12_config_set_mode(ctx, LIS2DH12_MODE_HIGH_RESOLUTION, LIS2DH12_ODR_100Hz);
			s_sensor_low_power_mode = 0U;
			DBG_PRINTF("LIS2DH12 -> HIGH_RESOLUTION @100Hz\n");
		}
	}
}

static void sensor_handle_int1_orientation(stmdev_ctx_t *ctx)
{
	lis2dh12_int1_src_t src;
	if (lis2dh12_int1_gen_source_get(ctx, &src) != 0) {
		return;
	}
	if (src.ia) {
		const char *dir = "UNKNOWN";
		if (src.xh) dir = "RIGHT";
		else if (src.xl) dir = "LEFT";
		else if (src.yh) dir = "FORWARD";
		else if (src.yl) dir = "BACKWARD";
		else if (src.zh) dir = "FACE_UP";
		else if (src.zl) dir = "FACE_DOWN";

		(void)dir;
		/* DBG_PRINTF("INT1 6D event: dir=%s | XL=%d XH=%d YL=%d YH=%d ZL=%d ZH=%d\n",
		           dir, src.xl, src.xh, src.yl, src.yh, src.zl, src.zh); */
	}
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
	float roll_rate_signed_dps = 0.0f;
	float pitch_rate_signed_dps = 0.0f;
	if (dt_s > 1e-3f) {
		roll_rate_signed_dps = angle_delta_deg(roll_deg, s_att.roll_deg) / dt_s;
		pitch_rate_signed_dps = angle_delta_deg(pitch_deg, s_att.pitch_deg) / dt_s;
		roll_rate_dps = fabsf(roll_rate_signed_dps);
		pitch_rate_dps = fabsf(pitch_rate_signed_dps);
	}

	const float acc_norm = sqrtf(s_ax_f * s_ax_f + s_ay_f * s_ay_f + s_az_f * s_az_f);
	const float dyn_acc = fabsf(acc_norm - 1000.0f);

	s_att.roll_deg = roll_deg;
	s_att.pitch_deg = pitch_deg;
	s_roll_rate_signed_dps = roll_rate_signed_dps;
	s_pitch_rate_signed_dps = pitch_rate_signed_dps;
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
	/* Optional debug print can be re-enabled when needed. */
	(void)adc_mV;
	return 0;
}

void sensor_task(void *arg)
{
	(void)arg;
	stmdev_ctx_t *ctx = lis2dh12_get_ctx();
	/* LIS2DH12 初始化并加载 EEPROM 中保存的零 g 校准（若有） */
	lis2dh12_init();
	sensor_config_int1_int2(ctx);
	storage_eeprom_init();
	lis2dh12_calib_t cal;
	if (storage_load_lis2dh12_calib(&cal))
		lis2dh12_set_calib_offset(cal.offset_x, cal.offset_y, cal.offset_z);

	uint32_t last_poll_ms = HAL_GetTick();
	uint32_t last_activity_ms = last_poll_ms;
	uint32_t int2_ignore_until_ms = 0U;
	uint8_t continuous_mode = 0U;
	s_sensor_low_power_mode = 0U;
	s_int1_last_level = (uint8_t)HAL_GPIO_ReadPin(lis2dh12_INT1_GPIO_Port, lis2dh12_INT1_Pin);
	for (;;) {
		const uint32_t now_ms = HAL_GetTick();
		const uint32_t sample_interval_ms = continuous_mode ? SENSOR_ACTIVE_SAMPLE_MS : SENSOR_IDLE_SAMPLE_MS;
		const bool sample_due = (now_ms - last_poll_ms) >= sample_interval_ms;
		bool do_sample = false;
		bool int2_event = false;

		const uint8_t int1_level = (uint8_t)HAL_GPIO_ReadPin(lis2dh12_INT1_GPIO_Port, lis2dh12_INT1_Pin);
		if (int1_level == (uint8_t)GPIO_PIN_SET && s_int1_last_level != (uint8_t)GPIO_PIN_SET) {
			sensor_handle_int1_orientation(ctx);
		}
		s_int1_last_level = int1_level;

		if (s_motion_irq_pending) {
			s_motion_irq_pending = 0U;
			if (now_ms < int2_ignore_until_ms) {
				/* Ignore short glitch right after mode switch; still clear latched source. */
				lis2dh12_int2_src_t int2_src;
				(void)lis2dh12_int2_gen_source_get(ctx, &int2_src);
			} else {
				int2_event = true;
				last_activity_ms = now_ms;
				if (!continuous_mode) {
					DBG_PRINTF("INT2 activity event -> continuous attitude ON\n");
				}
				continuous_mode = 1U;
				sensor_set_power_mode(ctx, 0U);
				do_sample = true;
			}
		} else if (sample_due) {
			do_sample = true;
		}

		if (do_sample) {
			last_poll_ms = now_ms;
			if (int2_event) {
				/* Read INT2 source to clear latched INT2 and avoid missed retrigger. */
				lis2dh12_int2_src_t int2_src;
				(void)lis2dh12_int2_gen_source_get(ctx, &int2_src);
			}
			lis2dh12_read_data(ctx);
			float ax_mg, ay_mg, az_mg;
			if (lis2dh12_get_last_accel_mg(&ax_mg, &ay_mg, &az_mg)) {
				sensor_update_attitude(ax_mg, ay_mg, az_mg);
				if (s_att.rotating) {
					last_activity_ms = now_ms;
					/* Fallback wake-up path: if INT2 misses, motion estimate can still re-enable
					 * continuous/high-performance mode from periodic samples. */
					if (!continuous_mode) {
						continuous_mode = 1U;
						sensor_set_power_mode(ctx, 0U);
						DBG_PRINTF("motion detected in sample -> continuous attitude ON\n");
					}
				}
				const int32_t roll_mdeg = (int32_t)(s_att.roll_deg * 1000.0f);
				const int32_t pitch_mdeg = (int32_t)(s_att.pitch_deg * 1000.0f);
				const int32_t roll_abs_mdeg = (roll_mdeg < 0) ? -roll_mdeg : roll_mdeg;
				const int32_t pitch_abs_mdeg = (pitch_mdeg < 0) ? -pitch_mdeg : pitch_mdeg;
				DBG_PRINTF("attitude roll/pitch=%s%ld.%03ld/%s%ld.%03ld deg, rotating=%d, dir=%s\n",
				           (roll_mdeg < 0) ? "-" : "",
				           (long)(roll_abs_mdeg / 1000),
				           (long)(roll_abs_mdeg % 1000),
				           (pitch_mdeg < 0) ? "-" : "",
				           (long)(pitch_abs_mdeg / 1000),
				           (long)(pitch_abs_mdeg % 1000),
				           s_att.rotating ? 1 : 0,
				           sensor_rotation_direction_str());
			}
		}
		if (continuous_mode && (now_ms - last_activity_ms >= SENSOR_INACTIVITY_TIMEOUT_MS)) {
			continuous_mode = 0U;
			sensor_set_power_mode(ctx, 1U);
			int2_ignore_until_ms = now_ms + INT2_WAKE_GUARD_MS;
			/* Clear possible latched stale INT2 status during LP transition. */
			lis2dh12_int2_src_t int2_src;
			(void)lis2dh12_int2_gen_source_get(ctx, &int2_src);
			DBG_PRINTF("inactivity >5s -> increase sample interval to %lums\n",
			           (unsigned long)SENSOR_IDLE_SAMPLE_MS);
		}
		vTaskDelay(pdMS_TO_TICKS(10));
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

void sensor_notify_motion_irq(void)
{
	s_motion_irq_pending = 1U;
}