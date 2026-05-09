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
#define SENSOR_IDLE_TIMEOUT_MS         5000U
#define SENSOR_FIFO_WTM_ACTIVE         12U
#define SENSOR_FIFO_WTM_IDLE           4U
#define SENSOR_FIFO_MAX_SAMPLES        32
static sensor_attitude_t s_att = {0};
static bool s_att_valid;
static float s_ax_f;
static float s_ay_f;
static float s_az_f;
static uint32_t s_last_update_ms;
static float s_roll_rate_signed_dps;
static float s_pitch_rate_signed_dps;
static volatile uint8_t s_motion_irq_pending;
static uint8_t s_fifo_irq_pending;
static volatile sensor_6d_dir_t s_6d_dir = SENSOR_6D_DIR_UNKNOWN;
static bool s_chip_high_perf;
static uint32_t s_last_motion_ms;
static bool sensor_measure_accel_avg_mg(stmdev_ctx_t *ctx, uint16_t samples,
					float *ax_mg, float *ay_mg, float *az_mg);

static void sensor_update_attitude(float ax_mg, float ay_mg, float az_mg);

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

static void sensor_set_fifo_watermark(stmdev_ctx_t *ctx, bool high_perf)
{
	const uint8_t wtm = high_perf ? SENSOR_FIFO_WTM_ACTIVE : SENSOR_FIFO_WTM_IDLE;
	(void)lis2dh12_config_fifo_set_watermark(ctx, wtm);
}

static void sensor_config_int2_6d(stmdev_ctx_t *ctx)
{
	lis2dh12_int2_cfg_t cfg = { 0 };
	cfg._6d = 1;
	cfg.xhie = 1; cfg.xlie = 1;
	cfg.yhie = 1; cfg.ylie = 1;
	cfg.zhie = 1; cfg.zlie = 1;
	(void)lis2dh12_int2_gen_conf_set(ctx, &cfg);
	(void)lis2dh12_int2_gen_threshold_set(ctx, 18);
	(void)lis2dh12_int2_gen_duration_set(ctx, 1);

	lis2dh12_ctrl_reg6_t c6;
	if (lis2dh12_pin_int2_config_get(ctx, &c6) == 0) {
		c6.i2_click = 0;
		c6.i2_act = 0;
		c6.i2_ia2 = 1;
		c6.int_polarity = 0;
		(void)lis2dh12_pin_int2_config_set(ctx, &c6);
	}
	(void)lis2dh12_int2_pin_notification_mode_set(ctx, LIS2DH12_INT2_LATCHED);
}

static void sensor_config_int1_int2(stmdev_ctx_t *ctx)
{
	/* INT1: FIFO stream + watermark/overrun as GPIO input poll source. */
	(void)lis2dh12_config_fifo_enable(ctx, 1);
	(void)lis2dh12_config_fifo_set_mode(ctx, LIS2DH12_FIFO_STREAM);
	(void)lis2dh12_config_fifo_int1(ctx, 1, 1);
	sensor_set_fifo_watermark(ctx, true);
	/* INT2: 6D orientation interrupt (EXTI). */
	sensor_config_int2_6d(ctx);
}

static void sensor_apply_chip_mode(stmdev_ctx_t *ctx, bool high_perf)
{
	if (high_perf == s_chip_high_perf)
		return;
	if (high_perf)
		(void)lis2dh12_config_set_mode(ctx, LIS2DH12_MODE_HIGH_RESOLUTION,
					       LIS2DH12_ODR_100Hz);
	else
		(void)lis2dh12_config_set_mode(ctx, LIS2DH12_MODE_LOW_POWER,
					       LIS2DH12_ODR_10Hz);
	s_chip_high_perf = high_perf;
	sensor_set_fifo_watermark(ctx, high_perf);
	vTaskDelay(pdMS_TO_TICKS(lis2dh12_config_settling_ms(
	    high_perf ? LIS2DH12_ODR_100Hz : LIS2DH12_ODR_10Hz)));
}

static void sensor_fifo_drain_and_update(stmdev_ctx_t *ctx)
{
	int16_t fifo_raw[SENSOR_FIFO_MAX_SAMPLES * 3];
	const int count = lis2dh12_config_fifo_batch_read(ctx, fifo_raw, SENSOR_FIFO_MAX_SAMPLES);
	if (count <= 0) {
		return;
	}

	int16_t off_x = 0, off_y = 0, off_z = 0;
	lis2dh12_get_calib_offset(&off_x, &off_y, &off_z);

	for (int i = 0; i < count; i++) {
		const int16_t rx = (int16_t)(fifo_raw[i * 3 + 0] - off_x);
		const int16_t ry = (int16_t)(fifo_raw[i * 3 + 1] - off_y);
		const int16_t rz = (int16_t)(fifo_raw[i * 3 + 2] - off_z);
		const float ax_mg = s_chip_high_perf ? LIS2DH12_FROM_FS_2g_HR_TO_mg(rx)
						     : LIS2DH12_FROM_FS_2g_LP_TO_mg(rx);
		const float ay_mg = s_chip_high_perf ? LIS2DH12_FROM_FS_2g_HR_TO_mg(ry)
						     : LIS2DH12_FROM_FS_2g_LP_TO_mg(ry);
		const float az_mg = s_chip_high_perf ? LIS2DH12_FROM_FS_2g_HR_TO_mg(rz)
						     : LIS2DH12_FROM_FS_2g_LP_TO_mg(rz);
		sensor_update_attitude(ax_mg, ay_mg, az_mg);
	}
}

static void sensor_handle_int2_orientation(stmdev_ctx_t *ctx)
{
	lis2dh12_int2_src_t src;
	if (lis2dh12_int2_gen_source_get(ctx, &src) != 0) {
		return;
	}
	if (src.ia) {
		sensor_6d_dir_t dir = SENSOR_6D_DIR_UNKNOWN;
		if (src.xh) dir = SENSOR_6D_DIR_RIGHT;
		else if (src.xl) dir = SENSOR_6D_DIR_LEFT;
		else if (src.yh) dir = SENSOR_6D_DIR_FORWARD;
		else if (src.yl) dir = SENSOR_6D_DIR_BACKWARD;
		else if (src.zh) dir = SENSOR_6D_DIR_FACE_UP;
		else if (src.zl) dir = SENSOR_6D_DIR_FACE_DOWN;

		s_6d_dir = dir;
		/* DBG_PRINTF("INT1 6D event: dir=%s | XL=%d XH=%d YL=%d YH=%d ZL=%d ZH=%d\n",
		           dir, src.xl, src.xh, src.yl, src.yh, src.zl, src.zh); */
	}
}

sensor_6d_dir_t sensor_get_6d_dir(void)
{
	return s_6d_dir;
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

static bool sensor_measure_accel_avg_mg(stmdev_ctx_t *ctx, uint16_t samples,
					float *ax_mg, float *ay_mg, float *az_mg)
{
	float sx = 0.0f, sy = 0.0f, sz = 0.0f;
	uint16_t ok = 0;

	if (!ctx || !ax_mg || !ay_mg || !az_mg || samples == 0U)
		return false;

	for (uint16_t i = 0; i < samples; i++) {
		float x = 0.0f, y = 0.0f, z = 0.0f;
		lis2dh12_read_data(ctx);
		if (lis2dh12_get_last_accel_mg(&x, &y, &z)) {
			sx += x;
			sy += y;
			sz += z;
			ok++;
		}
		vTaskDelay(pdMS_TO_TICKS(5));
	}

	if (ok == 0U)
		return false;

	*ax_mg = sx / (float)ok;
	*ay_mg = sy / (float)ok;
	*az_mg = sz / (float)ok;
	return true;
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
	while (!storage_system_ready()) {
		vTaskDelay(pdMS_TO_TICKS(50));
	}
	stmdev_ctx_t *ctx = lis2dh12_get_ctx();
	/* LIS2DH12 初始化并加载 EEPROM 中保存的零 g 校准（若有） */
	lis2dh12_init();
	s_chip_high_perf = false;
	sensor_apply_chip_mode(ctx, true);
	sensor_config_int1_int2(ctx);
	lis2dh12_calib_t cal;
	if (storage_load_lis2dh12_calib(&cal))
		lis2dh12_set_calib_offset(cal.offset_x, cal.offset_y, cal.offset_z);

	uint32_t last_poll_ms = HAL_GetTick();
	s_last_motion_ms = HAL_GetTick();
	for (;;) {
		const uint32_t now_ms = HAL_GetTick();

		const uint32_t sample_interval_ms =
		    s_chip_high_perf ? SENSOR_ACTIVE_SAMPLE_MS : SENSOR_IDLE_SAMPLE_MS;
		const bool sample_due = (now_ms - last_poll_ms) >= sample_interval_ms;
		bool do_sample = false;

		const uint8_t int1_level = (uint8_t)HAL_GPIO_ReadPin(lis2dh12_INT1_GPIO_Port, lis2dh12_INT1_Pin);
		if (int1_level == (uint8_t)GPIO_PIN_SET)
			s_fifo_irq_pending = 1U;

		if (s_motion_irq_pending) {
			s_motion_irq_pending = 0U;
			sensor_handle_int2_orientation(ctx);
			s_last_motion_ms = now_ms;
			sensor_apply_chip_mode(ctx, true);
			do_sample = true;
		} else if (s_fifo_irq_pending) {
			s_fifo_irq_pending = 0U;
			do_sample = true;
		} else if (sample_due) {
			do_sample = true;
		}

		if (do_sample) {
			last_poll_ms = now_ms;
			sensor_fifo_drain_and_update(ctx);
			/* if (s_att_valid) {
				const int roll_x10 = (int)(s_att.roll_deg * 10.0f);
				const int pitch_x10 = (int)(s_att.pitch_deg * 10.0f);
				const int roll_dec = (roll_x10 < 0) ? -(roll_x10 % 10) : (roll_x10 % 10);
				const int pitch_dec = (pitch_x10 < 0) ? -(pitch_x10 % 10) : (pitch_x10 % 10);
				DBG_PRINTF("[att] roll=%d.%d pitch=%d.%d rot=%d\n",
					   roll_x10 / 10, roll_dec,
					   pitch_x10 / 10, pitch_dec,
					   s_att.rotating ? 1 : 0);
			} */
			if (s_att.rotating)
				s_last_motion_ms = now_ms;
		}

		if (s_chip_high_perf &&
		    (uint32_t)(now_ms - s_last_motion_ms) >= SENSOR_IDLE_TIMEOUT_MS)
			sensor_apply_chip_mode(ctx, false);

		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

/**
 * 执行一次 LIS2DH12 零 g 校准，并将 s_calib_offset 保存到 EEPROM。
 * 调用前传感器需静止、Z 轴向上。可由 UI 菜单项或调试命令触发。
 */
bool sensor_lis2dh12_calibrate_and_save(void)
{
	int16_t before_x = 0, before_y = 0, before_z = 0;
	float pre_ax = 0.0f, pre_ay = 0.0f, pre_az = 0.0f;
	float post_ax = 0.0f, post_ay = 0.0f, post_az = 0.0f;
	float pre_err_mg = -1.0f, post_err_mg = -1.0f;
	stmdev_ctx_t *ctx = lis2dh12_get_ctx();

	lis2dh12_get_calib_offset(&before_x, &before_y, &before_z);
	DBG_PRINTF("[calib] before: x=%d y=%d z=%d\n", before_x, before_y, before_z);
	if (sensor_measure_accel_avg_mg(ctx, 24, &pre_ax, &pre_ay, &pre_az)) {
		const float pre_target_z = (pre_az >= 0.0f) ? 1000.0f : -1000.0f;
		const float pre_err = sqrtf(pre_ax * pre_ax + pre_ay * pre_ay +
					    (pre_az - pre_target_z) * (pre_az - pre_target_z));
		pre_err_mg = pre_err;
		DBG_PRINTF("[calib] pre_avg[mg]: x=%d y=%d z=%d | target_z=%d | err=%dmg\n",
			   (int)pre_ax, (int)pre_ay, (int)pre_az,
			   (int)pre_target_z, (int)pre_err);
	}
	if (lis2dh12_calibrate(ctx) != 0) {
		DBG_PRINTF("[calib] failed: lis2dh12_calibrate() error\n");
		return false;
	}
	lis2dh12_calib_t cal = {
		.magic   = EEPROM_CALIB_MAGIC,
		.offset_x = 0,
		.offset_y = 0,
		.offset_z = 0,
	};
	lis2dh12_get_calib_offset(&cal.offset_x, &cal.offset_y, &cal.offset_z);
	DBG_PRINTF("[calib] after : x=%d y=%d z=%d | delta: dx=%d dy=%d dz=%d\n",
		   cal.offset_x, cal.offset_y, cal.offset_z,
		   (int16_t)(cal.offset_x - before_x),
		   (int16_t)(cal.offset_y - before_y),
		   (int16_t)(cal.offset_z - before_z));
	if (!storage_save_lis2dh12_calib(&cal)) {
		DBG_PRINTF("[calib] failed: save EEPROM error\n");
		return false;
	}
	if (sensor_measure_accel_avg_mg(ctx, 24, &post_ax, &post_ay, &post_az)) {
		const float post_target_z = (post_az >= 0.0f) ? 1000.0f : -1000.0f;
		const float post_err = sqrtf(post_ax * post_ax + post_ay * post_ay +
					     (post_az - post_target_z) * (post_az - post_target_z));
		post_err_mg = post_err;
		DBG_PRINTF("[calib] post_avg[mg]: x=%d y=%d z=%d | target_z=%d | err=%dmg\n",
			   (int)post_ax, (int)post_ay, (int)post_az,
			   (int)post_target_z, (int)post_err);
	}
	if (pre_err_mg >= 0.0f && post_err_mg >= 0.0f) {
		if (pre_err_mg > 0.01f) {
			const float improve_pct = ((pre_err_mg - post_err_mg) / pre_err_mg) * 100.0f;
			DBG_PRINTF("[calib] improvement: %d%% (%dmg -> %dmg)\n",
				   (int)improve_pct, (int)pre_err_mg, (int)post_err_mg);
		} else {
			DBG_PRINTF("[calib] improvement: baseline near zero (%dmg -> %dmg)\n",
				   (int)pre_err_mg, (int)post_err_mg);
		}
	}
	DBG_PRINTF("[calib] success: saved to EEPROM\n");
	return true;
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