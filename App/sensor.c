#include "sensor.h"
#include "led_control.h"
#include "utils.h"
#include "storage.h"
#include "read_data_simple.h"
#include "eeprom_layout.h"

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
		delay_ms(2000);
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