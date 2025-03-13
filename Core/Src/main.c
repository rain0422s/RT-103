/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "i2c.h"
#include "rtc.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "i2cdev.h"
#include "string.h"
#include "stdio.h"
#include "storage.h"
#include "display.h"
#include "sensor.h"
#include "utils.h"
// #include "lfs.h"
#include "FreeRTOS.h"
#include "event_groups.h"
#include "read_data_simple.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
uint16_t adc_value[100];
SHT3xObjectType sht;
struct i2c_cli m24c02;
u8g2_t u8g2;
stmdev_ctx_t dev_ctx;
uint8_t whoamI = 0;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#define TURN_ON_LED() do {                           \
HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0,GPIO_PIN_RESET );\
} while(0)

#define TURN_OFF_LED() do {                        \
HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0,GPIO_PIN_SET );\
} while(0)

#define ENABLE_DC(x) do{ x? \
	                     HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_SET): \
	                     HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_RESET); \
                 } while(0)
#define BULE_LED(x) do{ x? \
	                     HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET): \
	                     HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET); \
                 } while(0)
                 
void delay_us1(uint16_t time){
        uint16_t i = 0;
        while(time--) {
                i = 10; // 自定义循环次数
                while(i--);
        }
}
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint16_t pwmVal=0;  
uint8_t dir=1;   


 
 
// static uint32_t send_data1=1;
// static uint32_t send_data2=1;

// 声明事件
#define EVENT7 (0x01 << 6)


        int flag =0;
//任务控制权柄
TaskHandle_t xHandleTsak[4];
// 事件控制权柄
EventGroupHandle_t myxEventGroupHandle_t = NULL;


// void eventTask2(void)
// {
// 	// 设置变量接收事件
// 	EventBits_t r_event;


// 	while(1)
// 	{

// 		r_event = xEventGroupWaitBits(myxEventGroupHandle_t,EVENT7,
// 									  pdTRUE,pdFALSE,portMAX_DELAY);

// 		if((r_event&EVENT7) != 0)
// 		{
//                         // for(i=0;i<50;i++){
                      
//                                 if(HAL_GPIO_ReadPin(GPIOC ,GPIO_PIN_6) == 0){
                                        
//                                         flag =     1;
//                                         printf("I do it1 %d\n",flag); 
//                                 }else{
//                                         flag =     0;
//                                         printf("I am alive %d\n",flag); 
                                      
//                                 }
                                        
   
// //                                         BULE_LED(0);
// // ENABLE_DC(0);
                       

                            
               
        
                        
                       
                        

// 		}
                 

//                 portDISABLE_INTERRUPTS();

                              
               
		
// 	}
// }




// void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
// {

// 	BaseType_t pxHigherPriorityTaskWoken; 
// 	uint32_t ulReturn;
// 	uint16_t event;
	

// 	ulReturn = taskENTER_CRITICAL_FROM_ISR();
	

// 	GPIO_PinState pinState = HAL_GPIO_ReadPin( GPIOC,GPIO_Pin );
// 	if(pinState == GPIO_PIN_RESET )
// 	{
// 		// 判断中断位置
// 		if(GPIO_Pin == GPIO_PIN_6 )
// 		{
// 			event = EVENT7;
// 		}


// 		xEventGroupSetBitsFromISR(myxEventGroupHandle_t,event,
// 		&pxHigherPriorityTaskWoken);

// 		portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
// 	}
	

// 	taskEXIT_CRITICAL_FROM_ISR( ulReturn ); 	
// }

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

void ui_task(void* arg)
{
    while(1)
    {
        loop1(u8g2);
        delay_ms(500);
    }
}

void gesture_task(void* arg)
{
                static portTickType myPreviousWakeTime;
          myPreviousWakeTime = xTaskGetTickCount();
    while(1)
    {
        if(flag&&HAL_GPIO_ReadPin(GPIOC ,GPIO_PIN_6) == 0)
        {
                // BULE_LED(1);
                printf("I will die \n");


                            xTaskDelayUntil(&myPreviousWakeTime, pdMS_TO_TICKS(5000));
                if(HAL_GPIO_ReadPin(GPIOC ,GPIO_PIN_6) == 0){
                        printf("I am die\n");
                        flag =0;
                        BULE_LED(0);
                        ENABLE_DC(0);
                }

        }
        // get_mpu6050_value();
        delay_ms(1000);
    }
}

void sensor_task(void* arg)
{ 
        lis2dh12_init(&dev_ctx);
        enable_fifo(&dev_ctx);
    while(1)
    {


        // get_sensor_value(sht,adc_value);
        lis2dh12_read_data(&dev_ctx);
        //HAL_GPIO_ReadPin(GPIOB ,GPIO_PIN_0) INT1
        
        read_fifo(&dev_ctx);
        if(!HAL_GPIO_ReadPin(GPIOC ,GPIO_PIN_5)){//check INT2
                printf("I sleep\n");
                BULE_LED(0);
        }else{
                printf("I am ailve\n");
                BULE_LED(1);
        }
        if(!HAL_GPIO_ReadPin(GPIOB ,GPIO_PIN_0)){//check INT1
                printf("I get it\n");
                clear_init1(&dev_ctx);
        }else{
                printf("I no get \n");
        }
        delay_ms(2000);


	  while (pwmVal< 500)
	  {
		  pwmVal++;
		  __HAL_TIM_SetCompare(&htim3, TIM_CHANNEL_4, pwmVal);    
		//   TIM3->CCR1 = pwmVal;  

                  delay_ms(1);
	  }

	  while (pwmVal)
	  {
		  pwmVal--;
		  __HAL_TIM_SetCompare(&htim3, TIM_CHANNEL_4, pwmVal); 
		//   TIM3->CCR1 = pwmVal;    

                  delay_ms(1);
	  }
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_RTC_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_I2C2_Init();
  MX_SPI1_Init();
  MX_SPI2_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_PWM_Start(&htim3,TIM_CHANNEL_4);


	


        // sensor_init(sht,adc_value,hadc1);


        // spi_flash_test();


        // i2c_eeprom_test(m24c02);


        // ui_test(u8g2);

        // lfs_test();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

        // xTaskCreate(ui_task, "ui_task", 128, NULL, 5, NULL);




        xTaskCreate(sensor_task, "sensor_task", 256, NULL, 3, NULL);

        // xTaskCreate(gesture_task, "gesture_task", 128, NULL, 1, NULL);
	// xTaskCreate(
	// 					(TaskFunction_t )eventTask2,(const char *)"task3",
	// 					(uint16_t)128,(void*) NULL,1,&xHandleTsak[2]);
								

	
	// // 创建事件
	// myxEventGroupHandle_t = xEventGroupCreate();
	// if(!myxEventGroupHandle_t)
	// 	printf("event fail\n");
	// else
	// 	printf("event suc\n");

        // 启动任务调度
       vTaskStartScheduler();

  while (1){
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_ADC;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
