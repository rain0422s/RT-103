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
#define RED_LED(x) do{ x? \
	                     HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_RESET): \
	                     HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET); \
                 } while(0)


#define BLUE_LED(x) do{ x? \
	                     HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_RESET): \
	                     HAL_GPIO_WritePin(GPIOC, GPIO_PIN_10, GPIO_PIN_SET); \
                 } while(0)


#define PowerOn         HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
#define PowerDown       HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
#define POWERKEY_GPIO_PORT      GPIOB
#define POWERKEY_GPIO_PIN       GPIO_PIN_11
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint16_t pwmVal=0;    
static void Creator(void); /* 用于创建和初始化FreeRTOS中的所有任务、事件和信号量 */
static TaskHandle_t V_handle_task_Creator = NULL;
TaskHandle_t V_handle_task_DeviceStart = NULL;
TaskHandle_t V_handle_task_IdleLED = NULL;
TaskHandle_t xHandleTsak = NULL;
TaskHandle_t xHandleTsak1 = NULL;
TimerHandle_t xLedTimer;
#define EVENT7 (0x01 << 6)
bool time_flag=false;
EventGroupHandle_t myxEventGroupHandle_t = NULL;




#define USART_LEN 64

/* DMA接收缓冲 */
uint8_t usart1_rx_DMA_buffer[USART_LEN];
uint8_t usart2_rx_DMA_buffer[USART_LEN];


/* 发送缓存，任务里拷贝数据后发送 */
uint8_t usart1_tx_buf[USART_LEN];
uint8_t usart2_tx_buf[USART_LEN];


/* UART、DMA句柄，由CubeMX或手动定义 */
extern DMA_HandleTypeDef hdma_usart2_rx;
extern DMA_HandleTypeDef hdma_usart2_tx;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;

/* FreeRTOS 资源 */
typedef struct {
    UART_HandleTypeDef *huart;
    uint8_t *rx_buf;
    uint8_t *tx_buf;
    SemaphoreHandle_t tx_sem;        // 发送信号量，保护DMA发送互斥
    DMA_HandleTypeDef *hdma_rx;
} uart_dev_t;

typedef struct {
    uart_dev_t *dev;
    uint16_t size;
} uart_event_t;

QueueHandle_t uart_rx_queue;

/* 任务句柄 */
TaskHandle_t uart_forward_task_handle;


uart_dev_t slave_uart = {
    .huart = &huart1,
    .rx_buf = usart1_rx_DMA_buffer,
    .tx_buf = usart1_tx_buf,
    .tx_sem = NULL,   // 后面初始化
    .hdma_rx = &hdma_usart1_rx,
};

uart_dev_t master_uart = {
    .huart = &huart2,
    .rx_buf = usart2_rx_DMA_buffer,
    .tx_buf = usart2_tx_buf,
    .tx_sem = NULL,   // 后面初始化
    .hdma_rx = &hdma_usart2_rx,
};
#define SLAVE_UART (&slave_uart)
#define MASTER_UART (&master_uart)


/* ----------------- 任务实现 ----------------- */

static void forward_uart(uart_dev_t *dst, uint8_t *data, uint16_t size)
{
    taskENTER_CRITICAL();
    memcpy(dst->tx_buf, data, size);
    taskEXIT_CRITICAL();

    if (xSemaphoreTake(dst->tx_sem, portMAX_DELAY) == pdTRUE) {
        if (HAL_UART_Transmit_DMA(dst->huart, dst->tx_buf, size) != HAL_OK) {
            xSemaphoreGive(dst->tx_sem);
        }
    }
}

void uart_forward_task(void *argument)
{
    uart_event_t event;

    for (;;)
    {
        if (xQueueReceive(uart_rx_queue, &event, portMAX_DELAY) == pdPASS)
        {
            uart_dev_t *src = event.dev;

            if (event.size == 0 || event.size > USART_LEN) 
                continue;

            // 谁发的 → 回发给谁
            forward_uart(src, src->rx_buf, event.size);
        }
    }
}


void uart_start_idle_dma(uart_dev_t *uart_dev)
{
    HAL_UARTEx_ReceiveToIdle_DMA(uart_dev->huart, uart_dev->rx_buf, USART_LEN);
    __HAL_DMA_DISABLE_IT(uart_dev->hdma_rx, DMA_IT_HT);
}

/* ----------------- 初始化 ----------------- */
void uart_dma_init(void)
{
        // 先填指针和缓冲区
        SLAVE_UART->tx_sem = xSemaphoreCreateBinary();
        MASTER_UART->tx_sem = xSemaphoreCreateBinary();


        /* 初始给信号量，表示DMA可用 */
        xSemaphoreGive(SLAVE_UART->tx_sem);
        xSemaphoreGive(MASTER_UART->tx_sem);

        uart_rx_queue = xQueueCreate(8, sizeof(uart_event_t));

        /* 启动DMA空闲接收 */
        uart_start_idle_dma(SLAVE_UART);
        uart_start_idle_dma(MASTER_UART);

        xTaskCreate(uart_forward_task, "uart_fwd", 256, NULL, 5, &uart_forward_task_handle);
}

/* ----------------- 中断回调 ----------------- */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        uart_event_t event;

        uart_dev_t *dev = NULL;

        if (huart == SLAVE_UART->huart) dev = SLAVE_UART;
        else if (huart == MASTER_UART->huart) dev = MASTER_UART;

        event.dev = dev;
        event.size = Size;

        /* 发送事件给任务 */
        xQueueSendFromISR(uart_rx_queue, &event, &xHigherPriorityTaskWoken);

        /* 重新启动DMA接收 */
        uart_start_idle_dma(dev);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}


/* ----------------- 发送完成回调 ----------------- */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        uart_dev_t *dev = NULL;

        if (huart == SLAVE_UART->huart) dev = SLAVE_UART;
        else if (huart == MASTER_UART->huart) dev = MASTER_UART;

        xSemaphoreGiveFromISR(dev->tx_sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}




void eventTask2(void){
        // static portTickType myPreviousWakeTime;
        ButtonState buttonState = IDLE_STATE;
        // 设置变量接收事件
        EventBits_t r_event;
	for (;;){
		r_event = xEventGroupWaitBits(myxEventGroupHandle_t,EVENT7,
						pdTRUE,pdFALSE,portMAX_DELAY);
		if((r_event&EVENT7) != 0){
                        printf("I do it eventTask2\n"); 
                        time_flag=false;

                        if (xTimerReset(xLedTimer, 0) != pdPASS) {
                                printf("Timer reset failed\n");
                                return;
                        }

                        while(!HAL_GPIO_ReadPin(POWERKEY_GPIO_PORT ,POWERKEY_GPIO_PIN) && !time_flag){
                                delay_ms(1);
                        }

                        if (xTimerStop(xLedTimer, 0) != pdPASS) {
                                printf("Timer stop failed\n");
                                return;
                        }
                        printf("I will die\n");
                        if(time_flag && button_scan(true,&buttonState) == LONG_PRESS_STATE){
                                printf("I am die2\n");   
                                PowerDown;
                        }

                        printf("I am alive\n");
                }
        }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	BaseType_t pxHigherPriorityTaskWoken; 
	uint32_t ulReturn;
	uint16_t event;
	ulReturn = taskENTER_CRITICAL_FROM_ISR();
	GPIO_PinState pinState = HAL_GPIO_ReadPin( POWERKEY_GPIO_PORT , GPIO_Pin );
	if(pinState == GPIO_PIN_RESET ){
		// 判断中断位置 
		if(GPIO_Pin == POWERKEY_GPIO_PIN ){
			event = EVENT7;
		}
		xEventGroupSetBitsFromISR(myxEventGroupHandle_t,event,
                        &pxHigherPriorityTaskWoken);
                // 如果有更高优先级的任务被唤醒，则进行任务切换
		portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
	}
	taskEXIT_CRITICAL_FROM_ISR( ulReturn ); 	
}

void vTimerCallback(TimerHandle_t xTimer) {
        time_flag=true;
}
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */


/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#ifdef U8G2_ENABLED
u8g2_t u8g2;
void ui_task(void* arg)
{
    while(1)
    {
        loop1(u8g2);
        delay_ms(500);
    }
}
#endif
void gesture_task(void* arg){
        lfs_first_run(); 
        i2c_eeprom_test();
        ButtonState buttonState = IDLE_STATE;
        while(1){
                switch(button_scan(false,&buttonState)){
                        case SHORT_PRESS_STATE:
                                BLUE_LED(1);
                                RED_LED(0);
                                break;
                        case LONG_PRESS_STATE:
                                BLUE_LED(0);
                                RED_LED(0);
                                break;
                        case DOUBLE_PRESS_STATE:
                                BLUE_LED(0);
                                RED_LED(1);
                                break;
                }
                delay_ms(10);
        }
}
#define lis2dh12_INT1_GPIO_Port   GPIOA
#define lis2dh12_INT1_Pin         GPIO_PIN_11
#define lis2dh12_INT2_GPIO_Port   GPIOA
#define lis2dh12_INT2_Pin         GPIO_PIN_12

void sensor_task(void* arg)
{ 
        // enable_fifo(&dev_ctx);
 
        

        while(1){
                // get_sensor_value(sht,adc_value);
                // lis2dh12_read_data(&dev_ctx);
                lis2dh12_init();
                // printf("I am alive\n");
                // HAL_GPIO_ReadPin(GPIOB ,GPIO_PIN_0) ;//INT1
                // read_fifo(&dev_ctx);
                //check INT2
                // if(!HAL_GPIO_ReadPin(lis2dh12_INT2_GPIO_Port ,lis2dh12_INT2_Pin)){
                //         printf("I sleep\n");
                //         // BLUE_LED(0);
                // }else{
                //         printf("I am ailve\n");
                //         // BLUE_LED(1);
                // }
                //check INT1
                // if(!HAL_GPIO_ReadPin(lis2dh12_INT2_GPIO_Port ,lis2dh12_INT1_Pin)){
                //         printf("I get it\n");
                //         clear_init1(&dev_ctx);
                // }else{
                //         printf("I no get \n");
                // }
                delay_ms(2000);
                //   while (pwmVal< 500)
                //   {
                // 	  pwmVal++;
                // 	  __HAL_TIM_SetCompare(&htim3, TIM_CHANNEL_4, pwmVal);    
                // 	//   TIM3->CCR1 = pwmVal;  
                //           delay_ms(1);
                //   }
                //   while (pwmVal)
                //   {
                // 	  pwmVal--;
                // 	  __HAL_TIM_SetCompare(&htim3, TIM_CHANNEL_4, pwmVal); 
                // 	//   TIM3->CCR1 = pwmVal;    
                //           delay_ms(1);
                //   }
        }
}
void dly_ms(uint32_t ms)
{
    // 每1毫秒大约需要循环72000次（72MHz / 1000）
    // 一个for循环约消耗1个周期（估算），加倍保险系数为10
    const uint32_t count_per_ms = 7200; // 实测可调
    for (uint32_t i = 0; i < (ms * count_per_ms); i++) {
        __NOP(); // 空操作，避免被优化掉
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
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_SPI2_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */
//   HAL_TIM_PWM_Start(&htim3,TIM_CHANNEL_4);
        PowerOn;
        // sensor_init(sht,adc_value,hadc1);
        // ui_test(u8g2);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
        xTaskCreate((TaskFunction_t)Creator,
                (const char *)"Creator",               
                (uint16_t)128,                           
                (void *)NULL,                          
                (UBaseType_t)10,                         
                (TaskHandle_t *)&V_handle_task_Creator);
        // xTaskCreate(ui_task, "ui_task", 128, NULL, 5, NULL);



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
static void Creator(void){

        taskENTER_CRITICAL(); // 进入临界区

        /**
         * @description: 任务创建区
         */
        uart_dma_init();   
        xTaskCreate((TaskFunction_t)sensor_task,             /* 任务入口函数 */
                                        (const char *)"sensor_task",             /* 任务名字 */
                                        (uint16_t)256,                               /* 任务栈大小 */
                                        (void *)NULL,                                /* 任务入口函数参数 */
                                        (UBaseType_t)3,                             /* 任务的优先级 */
                                        (TaskHandle_t *)&V_handle_task_DeviceStart); /* 任务控制块指针 */
                
        xTaskCreate((TaskFunction_t)gesture_task,           
                                        (const char *)"gesture_task",          
                                        (uint16_t)1024,                        
                                        (void *)NULL,                   
                                        (UBaseType_t)10,                        
                                        (TaskHandle_t *)&V_handle_task_IdleLED);

	xTaskCreate((TaskFunction_t )eventTask2,
                                        (const char *)"eventTask2",
                                        (uint16_t)64,
                                        (void*) NULL,
                                        2,
                                        &xHandleTsak);



	// 创建事件
	myxEventGroupHandle_t = xEventGroupCreate();
	if(!myxEventGroupHandle_t)
		printf("event fail\n");
	else
		printf("event suc\n");

        xLedTimer = xTimerCreate(
                        "MyTimer",          // 定时器名称
                        pdMS_TO_TICKS(2000), // 定时器周期（1000毫秒）
                        pdFALSE,          
                        (void *)0,          // 定时器ID
                        vTimerCallback      // 回调函数
                );

  /***********************************任务创建区***********************************/
  vTaskDelete(V_handle_task_Creator); // 删除Creator任务
  taskEXIT_CRITICAL();                // 退出临界区
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
