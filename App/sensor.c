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

#define ATT_LPF_ALPHA                  0.22f
#define ROTATION_RATE_THRESHOLD_DPS    15.0f
#define ROTATION_DYNACC_THRESHOLD_MG   120.0f
#define TILT_ACTION_TRIGGER_DEG        4.0f
#define TILT_ACTION_DELTA_DEG          1.8f
#define TILT_ACTION_REARM_DEG          8.0f
#define TILT_ACTION_RATE_DPS           1.8f
#define TILT_ACTION_REARM_RATE_DPS     0.8f
#define TILT_ACTION_MAX_PITCH_DEG      30.0f
#define TILT_ACTION_MAX_DYNACC_MG      120.0f
#define TILT_ACTION_MAX_RAW_DYNACC_MG  120.0f
#define TILT_ACTION_MAX_SAMPLE_STEP_MG 240.0f
#define TILT_ACTION_CONFIRM_MS         90U
#define TILT_STABLE_BASELINE_MS        300U
#define TILT_STABLE_JITTER_DEG         1.0f
#define SENSOR_ACTIVE_SAMPLE_MS        20U
#define SENSOR_IDLE_SAMPLE_MS          500U
#define SENSOR_IDLE_TIMEOUT_MS         5000U
#define SENSOR_FIFO_WTM_ACTIVE         2U
#define SENSOR_FIFO_WTM_IDLE           4U
#define SENSOR_FIFO_MAX_SAMPLES        32
#define BATTERY_ADC_VREF_MV            3300U
#define BATTERY_ADC_MAX                4095U
#define BATTERY_DIVIDER_TOP_OHM        300000U
#define BATTERY_DIVIDER_BOTTOM_OHM     150000U
#define BATTERY_CUTOFF_MV              2750U
#define BATTERY_UI_EMPTY_MV            3300U
#define BATTERY_FULL_MV                4200U
#define BATTERY_CAPACITY_MAH           2000U
#define BATTERY_CHARGE_CURRENT_MIN_MA  1000U
#define BATTERY_CHARGE_CURRENT_MAX_MA  2000U
#define BATTERY_DISCHARGE_TEMP_MIN_C   (-20)
#define BATTERY_DISCHARGE_TEMP_MAX_C   60
#define BATTERY_CHARGE_TEMP_MIN_C      0
#define BATTERY_CHARGE_TEMP_MAX_C      50
#define BATTERY_POLL_MS                1000U
#define BATTERY_ADC_TRIM_DIVISOR       10U
#define BATTERY_FILTER_PREV_WEIGHT     7U
#define BATTERY_FILTER_NEW_WEIGHT      1U
#define BATTERY_GAIN_DEFAULT           10000U
#define BATTERY_GAIN_MIN               8000U
#define BATTERY_GAIN_MAX               12000U
#define BATTERY_OFFSET_MIN_MV          (-500)
#define BATTERY_OFFSET_MAX_MV          500
typedef enum {
	TILT_STATE_ARMED = 0,
	TILT_STATE_MOVING_RIGHT,
	TILT_STATE_MOVING_LEFT,
	TILT_STATE_HELD_RIGHT,
	TILT_STATE_HELD_LEFT,
} tilt_action_state_t;

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
static uint16_t *s_battery_adc_values;
static uint16_t s_battery_adc_count;
static sensor_battery_t s_battery = {0};
static uint16_t s_battery_filtered_mv;
static bool s_battery_filter_valid;
static uint16_t s_battery_raw_mv;
static uint16_t s_battery_gain_permyriad = BATTERY_GAIN_DEFAULT;
static int16_t s_battery_offset_mv;
static float s_pose_roll_zero_deg;
static float s_pose_pitch_zero_deg;
static int8_t s_pose_roll_sign = 1;
static tilt_action_state_t s_tilt_state = TILT_STATE_ARMED;
static float s_tilt_ref_deg;
static bool s_tilt_ref_valid;
static uint32_t s_tilt_motion_since_ms;
static float s_tilt_stable_candidate_deg;
static uint32_t s_tilt_stable_since_ms;
static bool s_tilt_stable_valid;
static volatile sensor_tilt_event_t s_tilt_event = SENSOR_TILT_EVENT_NONE;
static volatile uint32_t s_active_request_until_ms;
static bool sensor_measure_accel_avg_mg(stmdev_ctx_t *ctx, uint16_t samples,
					float *ax_mg, float *ay_mg, float *az_mg);

static void sensor_update_attitude(float ax_mg, float ay_mg, float az_mg);

static float sensor_raw_to_mg(int16_t raw)
{
	return s_chip_high_perf ? LIS2DH12_FROM_FS_2g_HR_TO_mg(raw)
				: LIS2DH12_FROM_FS_2g_LP_TO_mg(raw);
}

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

static void sensor_tilt_event_latch(sensor_tilt_event_t event)
{
	if (event != SENSOR_TILT_EVENT_NONE)
		s_tilt_event = event;
}

static void sensor_tilt_action_reset(void)
{
	s_tilt_state = TILT_STATE_ARMED;
	s_tilt_ref_deg = 0.0f;
	s_tilt_ref_valid = false;
	s_tilt_motion_since_ms = 0;
	s_tilt_stable_candidate_deg = 0.0f;
	s_tilt_stable_since_ms = 0;
	s_tilt_stable_valid = false;
	taskENTER_CRITICAL();
	s_tilt_event = SENSOR_TILT_EVENT_NONE;
	taskEXIT_CRITICAL();
}

static void sensor_tilt_action_arm_at(float physical_tilt)
{
	s_tilt_state = TILT_STATE_ARMED;
	s_tilt_ref_deg = physical_tilt;
	s_tilt_ref_valid = true;
	s_tilt_motion_since_ms = 0;
	s_tilt_stable_candidate_deg = physical_tilt;
	s_tilt_stable_since_ms = HAL_GetTick();
	s_tilt_stable_valid = true;
}

static void sensor_tilt_stable_track(float physical_tilt, uint32_t now_ms)
{
	if (!s_tilt_stable_valid ||
	    fabsf(angle_delta_deg(physical_tilt, s_tilt_stable_candidate_deg)) >
	    TILT_STABLE_JITTER_DEG) {
		s_tilt_stable_candidate_deg = physical_tilt;
		s_tilt_stable_since_ms = now_ms;
		s_tilt_stable_valid = true;
		return;
	}

	if ((uint32_t)(now_ms - s_tilt_stable_since_ms) >=
	    TILT_STABLE_BASELINE_MS) {
		sensor_tilt_action_arm_at(physical_tilt);
	}
}

static bool sensor_tilt_action_ready_to_latch(uint32_t now_ms, float delta)
{
	return s_tilt_motion_since_ms != 0U &&
	       delta >= TILT_ACTION_DELTA_DEG &&
	       (uint32_t)(now_ms - s_tilt_motion_since_ms) >=
	       TILT_ACTION_CONFIRM_MS;
}

static void sensor_tilt_action_latch(sensor_tilt_event_t event,
				     float physical_tilt,
				     float physical_screen,
				     float delta,
				     float physical_rate)
{
	const char *name = (event == SENSOR_TILT_EVENT_RIGHT) ? "right" : "left";

	s_tilt_state = (event == SENSOR_TILT_EVENT_RIGHT) ?
		       TILT_STATE_HELD_RIGHT : TILT_STATE_HELD_LEFT;
	s_tilt_ref_deg = physical_tilt;
	s_tilt_motion_since_ms = 0;
	DBG_PRINTF("[tilt] %s tilt_x10=%d screen_x10=%d delta_x10=%d rate_x10=%d\n",
		   name,
		   (int)(physical_tilt * 10.0f),
		   (int)(physical_screen * 10.0f),
		   (int)(delta * 10.0f),
		   (int)(physical_rate * 10.0f));
	sensor_tilt_event_latch(event);
}

static void sensor_update_tilt_action(float roll_deg, float pitch_deg,
				      float pitch_rate_dps,
				      float dyn_acc_mg,
				      float raw_dyn_acc_mg,
				      float sample_step_mg)
{
	const uint32_t now_ms = HAL_GetTick();
	const float physical_tilt =
	    angle_delta_deg(pitch_deg, s_pose_pitch_zero_deg) *
	    (float)s_pose_roll_sign;
	const float physical_screen =
	    angle_delta_deg(roll_deg, s_pose_roll_zero_deg);
	const float physical_rate = pitch_rate_dps * (float)s_pose_roll_sign;
	const float abs_tilt = fabsf(physical_tilt);
	const float abs_rate = fabsf(pitch_rate_dps);

	if (fabsf(physical_screen) > TILT_ACTION_MAX_PITCH_DEG) {
		sensor_tilt_action_reset();
		return;
	}
	if (dyn_acc_mg > TILT_ACTION_MAX_DYNACC_MG) {
		sensor_tilt_action_reset();
		return;
	}
	if (raw_dyn_acc_mg > TILT_ACTION_MAX_RAW_DYNACC_MG) {
		sensor_tilt_action_reset();
		return;
	}
	if (sample_step_mg > TILT_ACTION_MAX_SAMPLE_STEP_MG) {
		sensor_tilt_action_reset();
		return;
	}

	if (!s_tilt_ref_valid) {
		s_tilt_ref_deg = physical_tilt;
		s_tilt_ref_valid = true;
	}

	if (abs_tilt <= TILT_ACTION_REARM_DEG &&
	    abs_rate <= TILT_ACTION_REARM_RATE_DPS) {
		sensor_tilt_action_arm_at(physical_tilt);
		return;
	}

	if (abs_rate <= TILT_ACTION_REARM_RATE_DPS) {
		if (s_tilt_state == TILT_STATE_MOVING_RIGHT &&
		    physical_tilt > TILT_ACTION_TRIGGER_DEG) {
			const float delta =
			    angle_delta_deg(physical_tilt, s_tilt_ref_deg);
			if (sensor_tilt_action_ready_to_latch(now_ms, delta))
				sensor_tilt_action_latch(SENSOR_TILT_EVENT_RIGHT,
							 physical_tilt, physical_screen,
							 delta, physical_rate);
			return;
		}
		if (s_tilt_state == TILT_STATE_MOVING_LEFT &&
		    physical_tilt < -TILT_ACTION_TRIGGER_DEG) {
			const float delta =
			    angle_delta_deg(s_tilt_ref_deg, physical_tilt);
			if (sensor_tilt_action_ready_to_latch(now_ms, delta))
				sensor_tilt_action_latch(SENSOR_TILT_EVENT_LEFT,
							 physical_tilt, physical_screen,
							 delta, physical_rate);
			return;
		}
		sensor_tilt_stable_track(physical_tilt, now_ms);
		return;
	}
	s_tilt_stable_valid = false;

	if (physical_rate > TILT_ACTION_RATE_DPS &&
	    physical_tilt > TILT_ACTION_TRIGGER_DEG) {
		const float delta =
		    angle_delta_deg(physical_tilt, s_tilt_ref_deg);
		if (s_tilt_state == TILT_STATE_HELD_RIGHT)
			return;
		if (s_tilt_state != TILT_STATE_MOVING_RIGHT) {
			s_tilt_state = TILT_STATE_MOVING_RIGHT;
			s_tilt_motion_since_ms = now_ms;
			return;
		}
		if (!sensor_tilt_action_ready_to_latch(now_ms, delta))
			return;
		sensor_tilt_action_latch(SENSOR_TILT_EVENT_RIGHT,
					 physical_tilt, physical_screen,
					 delta, physical_rate);
	} else if (physical_rate < -TILT_ACTION_RATE_DPS &&
		   physical_tilt < -TILT_ACTION_TRIGGER_DEG) {
		const float delta =
		    angle_delta_deg(s_tilt_ref_deg, physical_tilt);
		if (s_tilt_state == TILT_STATE_HELD_LEFT)
			return;
		if (s_tilt_state != TILT_STATE_MOVING_LEFT) {
			s_tilt_state = TILT_STATE_MOVING_LEFT;
			s_tilt_motion_since_ms = now_ms;
			return;
		}
		if (!sensor_tilt_action_ready_to_latch(now_ms, delta))
			return;
		sensor_tilt_action_latch(SENSOR_TILT_EVENT_LEFT,
					 physical_tilt, physical_screen,
					 delta, physical_rate);
	}
}

static void sensor_attitude_filter_reset(void)
{
	s_att = (sensor_attitude_t){0};
	s_att_valid = false;
	s_ax_f = 0.0f;
	s_ay_f = 0.0f;
	s_az_f = 0.0f;
	s_last_update_ms = 0;
	s_roll_rate_signed_dps = 0.0f;
	s_pitch_rate_signed_dps = 0.0f;
	sensor_tilt_action_reset();
}

void sensor_request_active(uint32_t hold_ms)
{
	const uint32_t now_ms = HAL_GetTick();
	const uint32_t until_ms = now_ms + hold_ms;

	taskENTER_CRITICAL();
	if ((int32_t)(until_ms - s_active_request_until_ms) > 0)
		s_active_request_until_ms = until_ms;
	taskEXIT_CRITICAL();
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
		const float ax_mg = sensor_raw_to_mg(rx);
		const float ay_mg = sensor_raw_to_mg(ry);
		const float az_mg = sensor_raw_to_mg(rz);
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
	float sample_step_mg = 0.0f;
	const float raw_acc_norm = sqrtf(ax_mg * ax_mg + ay_mg * ay_mg + az_mg * az_mg);
	const float raw_dyn_acc = fabsf(raw_acc_norm - 1000.0f);
	if (s_last_update_ms != 0U && now_ms > s_last_update_ms) {
		dt_s = (float)(now_ms - s_last_update_ms) / 1000.0f;
	}
	s_last_update_ms = now_ms;

	if (!s_att_valid) {
		s_ax_f = ax_mg;
		s_ay_f = ay_mg;
		s_az_f = az_mg;
		s_att_valid = true;
	} else {
		const float dx = ax_mg - s_ax_f;
		const float dy = ay_mg - s_ay_f;
		const float dz = az_mg - s_az_f;

		sample_step_mg = sqrtf(dx * dx + dy * dy + dz * dz);
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
	sensor_update_tilt_action(roll_deg, pitch_deg, pitch_rate_signed_dps,
				  dyn_acc, raw_dyn_acc, sample_step_mg);

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

typedef struct {
	uint16_t millivolts;
	uint8_t percent;
} battery_ocv_point_t;

static const battery_ocv_point_t s_battery_ocv_table[] = {
	{3300U, 0U},
	{3500U, 10U},
	{3600U, 20U},
	{3680U, 30U},
	{3730U, 40U},
	{3790U, 50U},
	{3870U, 60U},
	{3950U, 70U},
	{4020U, 80U},
	{4110U, 90U},
	{4200U, 100U},
};

uint8_t sensor_battery_percent_from_mv(uint16_t millivolts)
{
	const uint8_t count = (uint8_t)(sizeof(s_battery_ocv_table) /
				       sizeof(s_battery_ocv_table[0]));

	if (millivolts <= BATTERY_UI_EMPTY_MV)
		return 0;
	if (millivolts >= BATTERY_FULL_MV)
		return 100;
	for (uint8_t i = 1; i < count; i++) {
		const battery_ocv_point_t *lo = &s_battery_ocv_table[i - 1U];
		const battery_ocv_point_t *hi = &s_battery_ocv_table[i];

		if (millivolts <= hi->millivolts) {
			const uint32_t mv_span = (uint32_t)(hi->millivolts - lo->millivolts);
			const uint32_t pct_span = (uint32_t)(hi->percent - lo->percent);
			const uint32_t mv_rel = (uint32_t)(millivolts - lo->millivolts);

			if (mv_span == 0U)
				return lo->percent;
			return (uint8_t)(lo->percent +
					 ((mv_rel * pct_span + mv_span / 2U) / mv_span));
		}
	}
	return 100;
}

void sensor_battery_apply_config(const eeprom_config_t *cfg)
{
	uint16_t gain = BATTERY_GAIN_DEFAULT;
	int16_t offset = 0;

	if (cfg != NULL) {
		gain = cfg->battery_gain_permyriad;
		offset = cfg->battery_offset_mv;
	}
	if (gain < BATTERY_GAIN_MIN || gain > BATTERY_GAIN_MAX)
		gain = BATTERY_GAIN_DEFAULT;
	if (offset < BATTERY_OFFSET_MIN_MV || offset > BATTERY_OFFSET_MAX_MV)
		offset = 0;
	s_battery_gain_permyriad = gain;
	s_battery_offset_mv = offset;
}

bool sensor_battery_calibrate_gain(uint16_t true_millivolts,
				   uint16_t *out_gain_permyriad)
{
	int32_t target;
	uint32_t gain;

	if (out_gain_permyriad == NULL || s_battery_raw_mv == 0U ||
	    true_millivolts < BATTERY_CUTOFF_MV ||
	    true_millivolts > BATTERY_FULL_MV + 100U) {
		return false;
	}
	target = (int32_t)true_millivolts - (int32_t)s_battery_offset_mv;
	if (target <= 0)
		return false;
	gain = ((uint32_t)target * 10000U + (s_battery_raw_mv / 2U)) /
	       s_battery_raw_mv;
	if (gain < BATTERY_GAIN_MIN || gain > BATTERY_GAIN_MAX)
		return false;
	*out_gain_permyriad = (uint16_t)gain;
	return true;
}

uint8_t sensor_battery_start(uint16_t *adc_values, uint16_t sample_count)
{
	if (adc_values == NULL || sample_count == 0U)
		return 1;

	s_battery_adc_values = adc_values;
	s_battery_adc_count = sample_count;
	if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
		return 2;
	if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_values, sample_count) != HAL_OK)
		return 3;
	return 0;
}

static bool sensor_adc_trimmed_average(const uint16_t *values, uint16_t count,
				       uint16_t *out_avg)
{
	uint16_t samples[SENSOR_BATTERY_ADC_SAMPLES];
	uint16_t sample_count = 0;
	uint16_t trim;
	uint16_t first;
	uint16_t last;
	uint32_t sum = 0;

	if (values == NULL || out_avg == NULL || count == 0U)
		return false;
	if (count > SENSOR_BATTERY_ADC_SAMPLES)
		count = SENSOR_BATTERY_ADC_SAMPLES;

	for (uint16_t i = 0; i < count; i++) {
		const uint16_t v = (uint16_t)(values[i] & BATTERY_ADC_MAX);

		if (v == 0U || v > BATTERY_ADC_MAX)
			continue;
		samples[sample_count++] = v;
	}
	if (sample_count == 0U)
		return false;

	for (uint16_t i = 1; i < sample_count; i++) {
		const uint16_t key = samples[i];
		uint16_t j = i;

		while (j > 0U && samples[j - 1U] > key) {
			samples[j] = samples[j - 1U];
			j--;
		}
		samples[j] = key;
	}

	trim = (uint16_t)(sample_count / BATTERY_ADC_TRIM_DIVISOR);
	if (sample_count < 8U)
		trim = 0U;
	first = trim;
	last = (uint16_t)(sample_count - trim);
	if (last <= first)
		return false;

	for (uint16_t i = first; i < last; i++)
		sum += samples[i];
	*out_avg = (uint16_t)(sum / (uint32_t)(last - first));
	return true;
}

void sensor_battery_poll(void)
{
	uint16_t adc_avg;
	uint32_t adc_mV;
	uint32_t battery_mV;
	uint16_t raw_battery_mV;
	uint16_t filtered_mV;

	if (s_battery_adc_values == NULL || s_battery_adc_count == 0U)
		return;
	if (!sensor_adc_trimmed_average(s_battery_adc_values, s_battery_adc_count,
					&adc_avg)) {
		return;
	}
	adc_mV = (adc_avg * BATTERY_ADC_VREF_MV + (BATTERY_ADC_MAX / 2u)) /
		 BATTERY_ADC_MAX;
	battery_mV = (adc_mV * (BATTERY_DIVIDER_TOP_OHM + BATTERY_DIVIDER_BOTTOM_OHM)) /
		     BATTERY_DIVIDER_BOTTOM_OHM;
	if (battery_mV > UINT16_MAX)
		battery_mV = UINT16_MAX;
	raw_battery_mV = (uint16_t)battery_mV;
	s_battery_raw_mv = raw_battery_mV;
	{
		int32_t calibrated = ((int32_t)raw_battery_mV *
				      (int32_t)s_battery_gain_permyriad +
				      5000) / 10000;

		calibrated += s_battery_offset_mv;
		if (calibrated < 0)
			calibrated = 0;
		if (calibrated > UINT16_MAX)
			calibrated = UINT16_MAX;
		raw_battery_mV = (uint16_t)calibrated;
	}

	if (!s_battery_filter_valid) {
		filtered_mV = raw_battery_mV;
		s_battery_filter_valid = true;
	} else {
		filtered_mV = (uint16_t)((s_battery_filtered_mv * BATTERY_FILTER_PREV_WEIGHT +
					  raw_battery_mV * BATTERY_FILTER_NEW_WEIGHT +
					  ((BATTERY_FILTER_PREV_WEIGHT +
					    BATTERY_FILTER_NEW_WEIGHT) / 2U)) /
					 (BATTERY_FILTER_PREV_WEIGHT +
					  BATTERY_FILTER_NEW_WEIGHT));
	}
	s_battery_filtered_mv = filtered_mV;

	s_battery.millivolts = filtered_mV;
	s_battery.raw_millivolts = s_battery_raw_mv;
	s_battery.percent = sensor_battery_percent_from_mv(filtered_mV);
	s_battery.valid = true;
}

bool sensor_get_battery(sensor_battery_t *out)
{
	if (out == NULL || !s_battery.valid)
		return false;
	*out = s_battery;
	return true;
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
	sensor_battery_poll();
	return 0;
}

void sensor_task(void *arg)
{
	(void)arg;
	while (!storage_system_ready()) {
		sensor_battery_poll();
		vTaskDelay(pdMS_TO_TICKS(50));
	}
	stmdev_ctx_t *ctx = lis2dh12_get_ctx();
	eeprom_config_t cfg;
	/* LIS2DH12 初始化并加载 EEPROM 中保存的零 g 校准（若有） */
	lis2dh12_init();
	s_chip_high_perf = false;
	sensor_apply_chip_mode(ctx, true);
	sensor_config_int1_int2(ctx);
	lis2dh12_calib_t cal;
	if (storage_load_lis2dh12_calib(&cal))
		lis2dh12_set_calib_offset(cal.offset_x, cal.offset_y, cal.offset_z);
	storage_config_set_defaults(&cfg);
	if (storage_load_config(&cfg)) {
		sensor_battery_apply_config(&cfg);
		sensor_pose_apply_config(&cfg);
	}
	DBG_PRINTF("[sensor] init done stack=%lu\n",
		   (unsigned long)uxTaskGetStackHighWaterMark(NULL));

	uint32_t last_poll_ms = HAL_GetTick();
	uint32_t last_battery_poll_ms = 0;
	s_last_motion_ms = HAL_GetTick();
	for (;;) {
		const uint32_t now_ms = HAL_GetTick();
		const bool active_requested =
		    (int32_t)(s_active_request_until_ms - now_ms) > 0;

		const uint32_t sample_interval_ms =
		    s_chip_high_perf ? SENSOR_ACTIVE_SAMPLE_MS : SENSOR_IDLE_SAMPLE_MS;
		const bool sample_due = (now_ms - last_poll_ms) >= sample_interval_ms;
		bool do_sample = false;

		const uint8_t int1_level = (uint8_t)HAL_GPIO_ReadPin(lis2dh12_INT1_GPIO_Port, lis2dh12_INT1_Pin);
		if (int1_level == (uint8_t)GPIO_PIN_SET)
			s_fifo_irq_pending = 1U;

		if (active_requested && !s_chip_high_perf) {
			sensor_apply_chip_mode(ctx, true);
			s_last_motion_ms = now_ms;
			do_sample = true;
		}

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

		if (s_chip_high_perf && !active_requested &&
		    (uint32_t)(now_ms - s_last_motion_ms) >= SENSOR_IDLE_TIMEOUT_MS)
			sensor_apply_chip_mode(ctx, false);

		if ((uint32_t)(now_ms - last_battery_poll_ms) >= BATTERY_POLL_MS) {
			sensor_battery_t battery;

			last_battery_poll_ms = now_ms;
			sensor_battery_poll();
			if (sensor_get_battery(&battery)) {
				(void)storage_battery_history_record(battery.millivolts,
								     battery.percent);
			}
		}

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
	sensor_attitude_filter_reset();
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

void sensor_pose_apply_config(const eeprom_config_t *cfg)
{
	if (cfg == NULL || (cfg->pose_flags & 0x01U) == 0U) {
		s_pose_roll_zero_deg = 0.0f;
		s_pose_pitch_zero_deg = 0.0f;
		s_pose_roll_sign = 1;
		sensor_tilt_action_reset();
		return;
	}
	s_pose_roll_zero_deg = (float)cfg->pose_roll_zero_x10 / 10.0f;
	s_pose_pitch_zero_deg = (float)cfg->pose_pitch_zero_x10 / 10.0f;
	s_pose_roll_sign = (cfg->pose_roll_sign == -1) ? -1 : 1;
	sensor_tilt_action_reset();
}

bool sensor_pose_capture_config(eeprom_config_t *cfg)
{
	if (cfg == NULL || !s_att_valid)
		return false;
	cfg->pose_flags |= 0x01U;
	cfg->pose_roll_zero_x10 = (int16_t)((s_att.roll_deg >= 0.0f) ?
					    (s_att.roll_deg * 10.0f + 0.5f) :
					    (s_att.roll_deg * 10.0f - 0.5f));
	cfg->pose_pitch_zero_x10 = (int16_t)((s_att.pitch_deg >= 0.0f) ?
					     (s_att.pitch_deg * 10.0f + 0.5f) :
					     (s_att.pitch_deg * 10.0f - 0.5f));
	if (cfg->pose_roll_sign != -1 && cfg->pose_roll_sign != 1)
		cfg->pose_roll_sign = 1;
	sensor_pose_apply_config(cfg);
	DBG_PRINTF("[pose] saved: roll_x10=%d pitch_x10=%d sign=%d\n",
		   (int)cfg->pose_roll_zero_x10,
		   (int)cfg->pose_pitch_zero_x10,
		   (int)cfg->pose_roll_sign);
	return true;
}

bool sensor_tilt_event_get(sensor_tilt_event_t *out)
{
	sensor_tilt_event_t event;

	if (out == NULL)
		return false;
	taskENTER_CRITICAL();
	event = s_tilt_event;
	s_tilt_event = SENSOR_TILT_EVENT_NONE;
	taskEXIT_CRITICAL();
	*out = event;
	return event != SENSOR_TILT_EVENT_NONE;
}

void sensor_notify_motion_irq(void)
{
	s_motion_irq_pending = 1U;
}
