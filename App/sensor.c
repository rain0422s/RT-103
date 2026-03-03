#include "sensor.h"
#include "led_control.h"
#include "utils.h"

#if 0 /* MPU6050 / ATK-MS6050: enable when needed (I2C2, atk_ms6050) */
static uint8_t MPU6050ReadID(void)
{
	unsigned char Re = 0;
	HAL_I2C_Mem_Read(&hi2c2, 0xd0, 0x75, 1, &Re, 1, 0xffff);
	if (Re != 0x68) {
		println("MPU6050 was not found\r\n");
		return 0;
	}
	println("MPU6050 ID = %x\r\n", Re);
	return 1;
}

static void get_mpu6050_value(void)
{
	uint8_t ret = 0;
	float pit, rol, yaw;
	int16_t acc_x, acc_y, acc_z;
	int16_t gyr_x, gyr_y, gyr_z;
	int16_t temp;

	while (atk_ms6050_dmp_get_data(&pit, &rol, &yaw) != 0) { }
	ret += atk_ms6050_get_accelerometer(&acc_x, &acc_y, &acc_z);
	ret += atk_ms6050_get_gyroscope(&gyr_x, &gyr_y, &gyr_z);
	ret += atk_ms6050_get_temperature(&temp);
	if (ret == 0) {
		printf("pit: %.2f, rol: %.2f, yaw: %.2f, ", pit, rol, yaw);
		printf("acc_x: %d, acc_y: %d, acc_z: %d, ", acc_x, acc_y, acc_z);
		printf("gyr_x: %d, gyr_y: %d, gyr_z: %d, ", gyr_x, gyr_y, gyr_z);
		printf("temp: %d\r\n", temp);
	}
}

static uint8_t mpu6050_init(void)
{
	uint8_t ret;
	MPU6050ReadID();
	ret = atk_ms6050_init();
	if (ret != 0)
		printf("ATK-MS6050 init failed!\r\n");
	ret = atk_ms6050_dmp_init();
	if (ret != 0)
		printf("ATK-MS6050 DMP init failed!\r\n");
	return ret;
}
#endif /* MPU6050 */

uint8_t sensor_init(SHT3xObjectType sht, uint16_t *ADC_Value, ADC_HandleTypeDef adc){
        sht3x_init(&sht ,0x44,hi2c1);
        HAL_ADCEx_Calibration_Start(&adc);
        HAL_ADC_Start_DMA(&adc, (uint32_t *)ADC_Value, 100);
#if 0
        mpu6050_init();
#endif
}

uint8_t get_sensor_value(SHT3xObjectType sht,uint16_t *ADC_Value){       

        float ADC_Vol;
        int adc;

        for(int i = 0,adc =0; i < 100;)
        {
                adc += ADC_Value[i++];
        }
        adc /= 100;

        ADC_Vol =(float) adc/4096*3.3;
        delay_ms(1);		

        printf("\r\n %f \r\n",ADC_Vol);

        printf("\r\n The adc value is %f \r\n",ADC_Vol);
        delay_ms(300);

        if (!sht3x_get_sensor_value(&sht))
                printf("%4f, %4f\n", sht.temp, sht.rh);

        delay_ms(500);
}

void sensor_task(void *arg)
{
	(void)arg;
	// enable_fifo(&dev_ctx);
	// lis2dh12_init();

	for (;;) {
		breathing_led_run_once();
		delay_ms(2000);
	}
}