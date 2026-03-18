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
#include "lfs_util.h"
#include "lfs_port.h"
#include "FreeRTOS.h"
#include "event_groups.h"
#include "read_data_simple.h"
#include "key.h"
#include "queue.h"
#include "semphr.h"
#include "led_control.h"
#include "gesture.h"
#include "uart_forward.h"
#include "power_key.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
uint16_t adc_value[100];
SHT3xObjectType sht;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static void Creator(void); /* 用于创建和初始化FreeRTOS中的所有任务、事件和信号量 */
static TaskHandle_t V_handle_task_Creator = NULL;
TaskHandle_t V_handle_task_DeviceStart = NULL;
TaskHandle_t V_handle_task_IdleLED = NULL;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
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
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_SPI2_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_TIM3_Init();
  MX_TIM2_Init();
  MX_SPI3_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_PWM_Start(&htim2,TIM_CHANNEL_1);
  PowerOn;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
        xTaskCreate((TaskFunction_t)Creator,
                (const char *)"Creator",
                (uint16_t)128,
                (void *)NULL,
                (UBaseType_t)10,
                (TaskHandle_t *)&V_handle_task_Creator);

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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
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
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/**
 * @brief Create all RTOS tasks, event group and timer; blink LED on success then delete self.
 */
static void Creator(void)
{
        taskENTER_CRITICAL();

        uart_dma_init();

        BaseType_t ok = pdPASS;
        if (xTaskCreate((TaskFunction_t)sensor_task, "sensor_task", 256, NULL, 3,
                        (TaskHandle_t *)&V_handle_task_DeviceStart) != pdPASS) {
                printf("[Creator] sensor_task create failed\n");
                ok = pdFALSE;
        }
        if (xTaskCreate((TaskFunction_t)gesture_task, "gesture_task", 384, NULL, 10,
                        (TaskHandle_t *)&V_handle_task_IdleLED) != pdPASS) {
                printf("[Creator] gesture_task create failed\n");
                ok = pdFALSE;
        }

        if (power_key_create() != 1) {
                ok = pdFALSE;
        }

        if (xTaskCreate((TaskFunction_t)storage_init_task, "storage_init", 512, NULL, 6, NULL) != pdPASS) {
                printf("[Creator] storage_init_task create failed\n");
                ok = pdFALSE;
        }
        led_ctl_queue = xQueueCreate(LED_CTL_QUEUE_LEN, sizeof(uint8_t));
        if (!led_ctl_queue)
                ok = pdFALSE;
        else if (xTaskCreate((TaskFunction_t)led_control_task, "led_ctl", 96, NULL, 2, NULL) != pdPASS)
                ok = pdFALSE;

#ifdef U8G2_ENABLED
        storage_eeprom_init();
        eeprom_config_t eeprom_cfg;
        if (!storage_load_config(&eeprom_cfg))
                eeprom_cfg.ui_select = 0;
        if (xTaskCreate((TaskFunction_t)ui_task, "ui_task", 128,
                        (void *)(intptr_t)eeprom_cfg.ui_select, 5, NULL) != pdPASS) {
                printf("[Creator] ui_task create failed\n");
                ok = pdFALSE;
        }
#endif

        taskEXIT_CRITICAL();
        if (ok == pdPASS)
                led_control_send(LED_CMD_READY);

        vTaskDelete(V_handle_task_Creator);
}
/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

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
