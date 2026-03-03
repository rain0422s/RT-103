#include "uart_forward.h"
#include "main.h"
#include "usart.h"
#include "dma.h"
#include "gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <string.h>

#define USART_LEN 64

extern DMA_HandleTypeDef hdma_usart2_rx;
extern DMA_HandleTypeDef hdma_usart2_tx;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;

static uint8_t usart1_rx_DMA_buffer[USART_LEN];
static uint8_t usart2_rx_DMA_buffer[USART_LEN];
static uint8_t usart1_tx_buf[USART_LEN];
static uint8_t usart2_tx_buf[USART_LEN];

typedef struct {
	UART_HandleTypeDef *huart;
	uint8_t *rx_buf;
	uint8_t *tx_buf;
	SemaphoreHandle_t tx_sem;
	DMA_HandleTypeDef *hdma_rx;
} uart_dev_t;

typedef struct {
	uart_dev_t *dev;
	uint16_t size;
} uart_event_t;

static QueueHandle_t uart_rx_queue;
static uart_dev_t slave_uart;
static uart_dev_t master_uart;

#define SLAVE_UART  (&slave_uart)
#define MASTER_UART (&master_uart)

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

static void uart_forward_task(void *argument)
{
	uart_event_t event;
	(void)argument;

	for (;;) {
		if (xQueueReceive(uart_rx_queue, &event, portMAX_DELAY) != pdPASS)
			continue;
		if (event.size == 0 || event.size > USART_LEN)
			continue;
		forward_uart(event.dev, event.dev->rx_buf, event.size);
	}
}

static void uart_start_idle_dma(uart_dev_t *uart_dev)
{
	HAL_UARTEx_ReceiveToIdle_DMA(uart_dev->huart, uart_dev->rx_buf, USART_LEN);
	__HAL_DMA_DISABLE_IT(uart_dev->hdma_rx, DMA_IT_HT);
}

void uart_dma_init(void)
{
	slave_uart.huart = &huart1;
	slave_uart.rx_buf = usart1_rx_DMA_buffer;
	slave_uart.tx_buf = usart1_tx_buf;
	slave_uart.tx_sem = xSemaphoreCreateBinary();
	slave_uart.hdma_rx = &hdma_usart1_rx;

	master_uart.huart = &huart2;
	master_uart.rx_buf = usart2_rx_DMA_buffer;
	master_uart.tx_buf = usart2_tx_buf;
	master_uart.tx_sem = xSemaphoreCreateBinary();
	master_uart.hdma_rx = &hdma_usart2_rx;

	xSemaphoreGive(slave_uart.tx_sem);
	xSemaphoreGive(master_uart.tx_sem);

	uart_rx_queue = xQueueCreate(8, sizeof(uart_event_t));

	uart_start_idle_dma(SLAVE_UART);
	uart_start_idle_dma(MASTER_UART);

	xTaskCreate(uart_forward_task, "uart_fwd", 256, NULL, 5, NULL);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	uart_event_t event;
	uart_dev_t *dev = NULL;

	if (huart == SLAVE_UART->huart)
		dev = SLAVE_UART;
	else if (huart == MASTER_UART->huart)
		dev = MASTER_UART;

	if (!dev)
		return;

	event.dev = dev;
	event.size = Size;
	xQueueSendFromISR(uart_rx_queue, &event, &xHigherPriorityTaskWoken);
	uart_start_idle_dma(dev);
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	uart_dev_t *dev = NULL;

	if (huart == SLAVE_UART->huart)
		dev = SLAVE_UART;
	else if (huart == MASTER_UART->huart)
		dev = MASTER_UART;

	if (dev)
		xSemaphoreGiveFromISR(dev->tx_sem, &xHigherPriorityTaskWoken);
	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
