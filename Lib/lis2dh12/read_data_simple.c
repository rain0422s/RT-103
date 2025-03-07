/*
 ******************************************************************************
 * @file    read_data_simple.c
 * @author  MEMS Software Solution Team
 * @date    05-October-2017
 * @brief   This file show the simplest way to get data from sensor.
 *
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; COPYRIGHT(c) 2017 STMicroelectronics</center></h2>
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright notice,
 *      this list of conditions and the following disclaimer in the documentation
 *      and/or other materials provided with the distribution.
 *   3. Neither the name of STMicroelectronics nor the names of its contributors
 *      may be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* Includes ------------------------------------------------------------------*/
#include "read_data_simple.h"

/* Private macro -------------------------------------------------------------*/
#ifdef MKI109V2
#define CS_SPI2_GPIO_Port   GPIOB
#define CS_SPI2_Pin         GPIO_PIN_12
#define CS_SPI1_GPIO_Port   GPIOA
#define CS_SPI1_Pin         GPIO_PIN_4
#endif

#ifdef NUCLEO_STM32F411RE
/* N/A on NUCLEO_STM32F411RE + IKS01A1 */
/* N/A on NUCLEO_STM32F411RE + IKS01A2 */
#define CS_SPI2_GPIO_Port   0
#define CS_SPI2_Pin         0
#define CS_SPI1_GPIO_Port   0
#define CS_SPI1_Pin         0
#endif

#define TX_BUF_DIM          1000
#define DEGREE_CAL 180.0/3.1416
#define FILTER_CNT 4

typedef struct {
        short x;
        short y;
        short z;
        short new_angle_x;
        short new_angle_y;
        short new_angle_z;
        short old_angle_x;
        short old_angle_y;
        short old_angle_z;
}axis_info_t;
 
 
typedef struct filter_avg{
  axis_info_t info[FILTER_CNT];
  unsigned char count;
}filter_avg_t;
/* Private variables ---------------------------------------------------------*/
static axis3bit16_t data_raw_acceleration;
static axis1bit16_t data_raw_temperature;
static float acceleration_mg[3];
static float temperature_degC;
static uint8_t whoamI;
static uint8_t tx_buffer[TX_BUF_DIM];
#define M_PI 3.14159
/* Extern variables ----------------------------------------------------------*/

/* Private functions ---------------------------------------------------------*/

/*
 *   Replace the functions "platform_write" and "platform_read" with your
 *   platform specific read and write function.
 *   This example use an STM32 evaluation board and CubeMX tool.
 *   In this case the "*handle" variable is usefull in order to select the
 *   correct interface but the usage uf "*handle" is not mandatory.
 */

static int32_t platform_write(void *handle, uint8_t Reg, uint8_t *Bufp,
                              uint16_t len){
        if (handle == &hi2c1){
                /* enable auto incremented in multiple read/write commands */
                Reg |= 0x80; 
                HAL_I2C_Mem_Write(handle, LIS2DH12_I2C_ADD_H, Reg,
                                I2C_MEMADD_SIZE_8BIT, Bufp, len, 1000);
        }
        #ifdef MKI109V2  
        else if (handle == &hspi2){
                /* enable auto incremented in multiple read/write commands */
                Reg |= 0x40;    
                HAL_GPIO_WritePin(CS_SPI2_GPIO_Port, CS_SPI2_Pin, GPIO_PIN_RESET);
                HAL_SPI_Transmit(handle, &Reg, 1, 1000);
                HAL_SPI_Transmit(handle, Bufp, len, 1000);
                HAL_GPIO_WritePin(CS_SPI2_GPIO_Port, CS_SPI2_Pin, GPIO_PIN_SET);
        }
        else if (handle == &hspi1){
                /* enable auto incremented in multiple read/write commands */
                Reg |= 0x40;     
                HAL_GPIO_WritePin(CS_SPI1_GPIO_Port, CS_SPI1_Pin, GPIO_PIN_RESET);
                HAL_SPI_Transmit(handle, &Reg, 1, 1000);
                HAL_SPI_Transmit(handle, Bufp, len, 1000);
                HAL_GPIO_WritePin(CS_SPI1_GPIO_Port, CS_SPI1_Pin, GPIO_PIN_SET);
        }
        #endif

        return 0;
}

static int32_t platform_read(void *handle, uint8_t Reg, uint8_t *Bufp,
                             uint16_t len){
                                
        if (handle == &hi2c1){
                /* enable auto incremented in multiple read/write commands */
                Reg |= 0x80;
                HAL_I2C_Mem_Read(handle, LIS2DH12_I2C_ADD_H, Reg,
                                I2C_MEMADD_SIZE_8BIT, Bufp, len, 1000);
        }
        #ifdef MKI109V2   
        else if (handle == &hspi2){               
                /* enable auto incremented in multiple read/write commands */
                Reg |= 0xC0;
                HAL_GPIO_WritePin(CS_SPI2_GPIO_Port, CS_SPI2_Pin, GPIO_PIN_RESET);
                HAL_SPI_Transmit(handle, &Reg, 1, 1000);
                HAL_SPI_Receive(handle, Bufp, len, 1000);
                HAL_GPIO_WritePin(CS_SPI2_GPIO_Port, CS_SPI2_Pin, GPIO_PIN_SET);
        }else if (handle == &hspi1){
                /* enable auto incremented in multiple read/write commands */
                Reg |= 0xC0;    
                HAL_GPIO_WritePin(CS_SPI1_GPIO_Port, CS_SPI1_Pin, GPIO_PIN_RESET);
                HAL_SPI_Transmit(handle, &Reg, 1, 1000);
                HAL_SPI_Receive(handle, Bufp, len, 1000);
                HAL_GPIO_WritePin(CS_SPI1_GPIO_Port, CS_SPI1_Pin, GPIO_PIN_SET);
        }
        #endif
// printf("I do platform_read cmd:%x,str:%s\n",Reg,Bufp);
        return 0;
}

/*
 *  Function to print messages
 */
void tx_com( uint8_t *tx_buffer, uint16_t len )
{
#ifdef NUCLEO_STM32F411RE  
        HAL_UART_Transmit( &huart2, tx_buffer, len, 1000 );
#endif
#ifdef MKI109V2  
        //CDC_Transmit_FS( tx_buffer, len );
        printf("[lis2dh12]:%s\r\n", tx_buffer);
#endif
}

/* Main Example --------------------------------------------------------------*/
void lis2dh12_init(stmdev_ctx_t *dev_ctx){
        /*
        *  Initialize mems driver interface
        */
        // stmdev_ctx_t dev_ctx;
        uint8_t val;

        dev_ctx->write_reg = platform_write;
        dev_ctx->read_reg = platform_read;
        dev_ctx->handle = &hspi2;  
        /*
        *  Check device ID
        */
        whoamI = 0;
        lis2dh12_device_id_get(dev_ctx, &whoamI);
        if ( whoamI != LIS2DH12_ID )
                while(1); /*manage here device not found */


        lis2dh12_act_threshold_set(dev_ctx,0x0D);
        val = 0x13;
        lis2dh12_act_timeout_set(dev_ctx,0x13);

        val = 0b00001010;;
        lis2dh12_pin_int2_config_set(dev_ctx,&val);
        
        /*
        *  Enable Block Data Update
        */
        lis2dh12_block_data_update_set(dev_ctx, PROPERTY_ENABLE);
        /*
        * Set Output Data Rate
        */
        lis2dh12_data_rate_set(dev_ctx, LIS2DH12_ODR_100Hz);
        /*
        * Set full scale
        */  
        lis2dh12_full_scale_set(dev_ctx, LIS2DH12_2g);
        /*
        * Enable temperature sensor
        */   
        lis2dh12_temperature_meas_set(dev_ctx, LIS2DH12_TEMP_ENABLE);
        /*
        * Set device in continuos mode
        */   
        lis2dh12_operating_mode_set(dev_ctx, LIS2DH12_HR_12bit);
        


}

/*
* Read samples in polling mode (no int)
*/
void lis2dh12_read_data(stmdev_ctx_t *dev_ctx){
        /*
        * Read output only if new value is available
        */
        axis_info_t sample;

        lis2dh12_reg_t reg;
	uint8_t i = 0;

                lis2dh12_status_get(dev_ctx, &reg.status_reg);
        
                if(reg.status_reg.zyxda){
                        /* Read magnetic field data */
                        memset(data_raw_acceleration.u8bit, 0x00, 3*sizeof(int16_t));
                        lis2dh12_acceleration_raw_get(dev_ctx, data_raw_acceleration.u8bit);
                        acceleration_mg[0] = LIS2DH12_FROM_FS_2g_HR_TO_mg( data_raw_acceleration.i16bit[0] );
                        acceleration_mg[1] = LIS2DH12_FROM_FS_2g_HR_TO_mg( data_raw_acceleration.i16bit[1] );
                        acceleration_mg[2] = LIS2DH12_FROM_FS_2g_HR_TO_mg( data_raw_acceleration.i16bit[2] );
        
                        sprintf((char*)tx_buffer, "Acceleration [mg]:%4.2f\t%4.2f\t%4.2f\r\n",
                                acceleration_mg[0], acceleration_mg[1], acceleration_mg[2]);
                        // tx_com( tx_buffer, strlen( (char const*)tx_buffer ) );
                        
                }
        
                lis2dh12_temp_data_ready_get(dev_ctx, &reg.byte);      
                if(reg.byte){
                        /* Read temperature data */
                        memset(data_raw_temperature.u8bit, 0x00, sizeof(int16_t));
                        lis2dh12_temperature_raw_get(dev_ctx, data_raw_temperature.u8bit);
                        temperature_degC = LIS2DH12_FROM_LSB_TO_degC_HR( data_raw_temperature.i16bit );
        
                        sprintf((char*)tx_buffer, "Temperature [degC]:%6.2f\r\n", temperature_degC );
                        //tx_com( tx_buffer, strlen( (char const*)tx_buffer ) );
                }
        
                // float roll  = atan2(sample.y, sample.z) * (180.0 / M_PI);
                // float pitch = atan2(-sample.x, sqrt(sample.y * sample.y + sample.z * sample.z)) * (180.0 / M_PI);
                

                sample.x = acceleration_mg[0]; 
		sample.y = acceleration_mg[1];
		sample.z = acceleration_mg[2];

                //计算三轴旋转角度，但它不是传统的 Pitch/Roll 角，而是每个轴相对于其他两个轴的倾斜角。
                sample.new_angle_x = atan((float)sample.x/(float)sqrt(pow(sample.y, 2)+pow(sample.z, 2))) * DEGREE_CAL;
                sample.new_angle_y = atan((float)sample.y/(float)sqrt(pow(sample.x, 2)+pow(sample.z, 2))) * DEGREE_CAL;
                sample.new_angle_z = atan((float)sample.z/(float)sqrt(pow(sample.x, 2)+pow(sample.y, 2))) * DEGREE_CAL;
                if (sample.new_angle_z < 0)
                {
                        sample.new_angle_x = 180-sample.new_angle_x;
                        sample.new_angle_y = 180-sample.new_angle_y;
                }
                printf("sample->new_angle_x:%d, sample->new_angle_y:%d, sample->new_angle_z:%d \r\n",sample.new_angle_x, sample.new_angle_y, sample.new_angle_z);

}