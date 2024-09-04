#include "sensor.h"


 /**
   * @brief   读取MPU6050的ID
   * @param
   * @retval 
   */
uint8_t MPU6050ReadID(void){
        unsigned char Re = 0;
        HAL_I2C_Mem_Read(&hi2c2, 0xd0, 0X75, 1, &Re, 1, 0xffff);
        if (Re != 0x68) {
                println("MPU6050 was not found\r\n");
                return 0;
        } else {
                println("MPU6050 ID = %x\r\n",Re);
                return 1;
        }

}

uint8_t sensor_init(SHT3xObjectType sht,uint16_t ADC_Value,ADC_HandleTypeDef adc){
        HAL_ADCEx_Calibration_Start(&adc);
        HAL_ADC_Start_DMA(&adc,(uint32_t*)&ADC_Value, 100);
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
        HAL_Delay(1);		

        printf("\r\n %f \r\n",ADC_Vol);
        printf("\r\n The adc value is %f \r\n",ADC_Vol);
        HAL_Delay(300);

        if (!sht3x_get_sensor_value(&sht))
                printf("%4f, %4f\n", sht.temp, sht.rh);

        HAL_Delay(500);
      
}

/**
 * @brief       例程演示入口函数
 * @param    
 * @retval     
 */
void get_mpu6050_value(void)
{
        uint8_t ret;

        uint8_t niming_report = 0;
        float pit, rol, yaw;
        int16_t acc_x, acc_y, acc_z;
        int16_t gyr_x, gyr_y, gyr_z;
        int16_t temp;



        while(atk_ms6050_dmp_get_data(&pit,&rol,&yaw)!=0){}
        /* 获取ATK-MS6050 DMP处理后的数据 */
        // ret  = atk_ms6050_dmp_get_data(&pit, &rol, &yaw);
                printf("[ line: %d | function: %s ] ret = %d" "\r\n", __LINE__, __FUNCTION__, ret);
        /* 获取ATK-MS6050加速度*/
        ret += atk_ms6050_get_accelerometer(&acc_x, &acc_y, &acc_z);
                printf("[ line: %d | function: %s ] ret = %d" "\r\n", __LINE__, __FUNCTION__, ret);

        ret += atk_ms6050_get_gyroscope(&gyr_x, &gyr_y, &gyr_z);
                printf("[ line: %d | function: %s ] ret = %d" "\r\n", __LINE__, __FUNCTION__, ret);
        /* 获取ATK-MS6050温度*/
        ret += atk_ms6050_get_temperature(&temp);
                printf("[ line: %d | function: %s ] ret = %d" "\r\n", __LINE__, __FUNCTION__, ret);
        if (ret == 0)
        {

                printf("pit: %.2f, rol: %.2f, yaw: %.2f, ", pit, rol, yaw);
                printf("acc_x: %d, acc_y: %d, acc_z: %d, ", acc_x, acc_y, acc_z);
                printf("gyr_x: %d, gyr_y: %d, gyr_z: %d, ", gyr_x, gyr_y, gyr_z);
                printf("temp: %d\r\n", temp);


        }

}

uint8_t mpu6050_init(void){  

        uint8_t ret;
        MPU6050ReadID();

        /* 初始化ATK-MS6050 */
        ret = atk_ms6050_init();
        if (ret != 0)
        {
                printf("ATK-MS6050 init failed!\r\n");
                // while (1)
                // {
                //     LED0_TOGGLE();
                //     delay_ms(200);
                // }
        }
        printf("ATK-MS6050 1111111222222222222!\r\n");
        /* 初始化ATK-MS6050 DMP */
        ret = atk_ms6050_dmp_init();
        printf("[ line: %d | function: %s ] ret = %d" "\r\n", __LINE__, __FUNCTION__, ret);
        if (ret != 0)
        {
                printf("ATK-MS6050 DMP init failed!\r\n");
                // while (1)
                // {
                //     LED0_TOGGLE();
                //     delay_ms(200);
                // }
        }
}