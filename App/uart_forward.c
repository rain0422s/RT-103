#include "uart_forward.h"
#include "main.h"
#include "usart.h"
#include "dma.h"
#include "gpio.h"
#include "rtc_clock.h"
#include "storage.h"
#include "sensor.h"
#include "display.h"
#include "ota_layout.h"
#include "ota_update.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define USART_LEN 320
#define UART_LINE_LEN 192
#define UART_FORWARD_TASK_STACK_WORDS 512

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
	uint8_t data[USART_LEN];
} uart_event_t;

static QueueHandle_t uart_rx_queue;
static uart_dev_t slave_uart;
static uart_dev_t master_uart;
static char s_pc_line[UART_LINE_LEN];
static uint8_t s_pc_line_len;

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

static void uart_reply(const char *fmt, ...)
{
	char buf[128];
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (len < 0)
		return;
	if (len >= (int)sizeof(buf))
		len = (int)sizeof(buf) - 1;
	(void)HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)len, 200);
}

static void uart_ota_reply(const char *text, void *ctx)
{
	(void)ctx;
	uart_reply("%s\r\n", text);
}

static uint32_t uart_load_le32(const uint8_t *data)
{
	return (uint32_t)data[0] |
	       ((uint32_t)data[1] << 8U) |
	       ((uint32_t)data[2] << 16U) |
	       ((uint32_t)data[3] << 24U);
}

static const char *uart_skip_spaces(const char *s)
{
	while (s != NULL && (*s == ' ' || *s == '\t'))
		s++;
	return s;
}

static bool uart_parse_uint2(const char *s, uint8_t *out)
{
	if (s == NULL || out == NULL || s[0] < '0' || s[0] > '9' ||
	    s[1] < '0' || s[1] > '9') {
		return false;
	}
	*out = (uint8_t)((s[0] - '0') * 10 + (s[1] - '0'));
	return true;
}

static bool uart_parse_time(const char *s, uint8_t *hour, uint8_t *minute,
			    uint8_t *second)
{
	uint8_t h;
	uint8_t m;
	uint8_t sec;

	if (!uart_parse_uint2(s, &h) || s[2] != ':' ||
	    !uart_parse_uint2(&s[3], &m) || s[5] != ':' ||
	    !uart_parse_uint2(&s[6], &sec)) {
		return false;
	}
	if (h > 23U || m > 59U || sec > 59U)
		return false;
	*hour = h;
	*minute = m;
	*second = sec;
	return true;
}

static bool uart_parse_mv(const char *s, uint16_t *out_mv)
{
	uint32_t whole = 0;
	uint32_t frac = 0;
	uint8_t frac_digits = 0;
	bool has_digit = false;
	bool has_dot = false;

	if (s == NULL || out_mv == NULL)
		return false;
	s = uart_skip_spaces(s);
	while (*s >= '0' && *s <= '9') {
		has_digit = true;
		whole = whole * 10U + (uint32_t)(*s - '0');
		s++;
	}
	if (*s == '.') {
		has_dot = true;
		s++;
		while (*s >= '0' && *s <= '9' && frac_digits < 3U) {
			frac = frac * 10U + (uint32_t)(*s - '0');
			frac_digits++;
			s++;
		}
	}
	if (!has_digit)
		return false;
	while (frac_digits < 3U) {
		frac *= 10U;
		frac_digits++;
	}
	if (has_dot)
		whole = whole * 1000U + frac;
	if (whole < 2500U || whole > 4300U)
		return false;
	*out_mv = (uint16_t)whole;
	return true;
}

static bool uart_parse_int(const char *s, int32_t *out)
{
	int32_t sign = 1;
	int32_t value = 0;
	bool has_digit = false;

	if (s == NULL || out == NULL)
		return false;
	s = uart_skip_spaces(s);
	if (*s == '-') {
		sign = -1;
		s++;
	} else if (*s == '+') {
		s++;
	}
	while (*s >= '0' && *s <= '9') {
		has_digit = true;
		value = value * 10 + (*s - '0');
		s++;
	}
	if (!has_digit)
		return false;
	*out = value * sign;
	return true;
}

static bool uart_load_config(eeprom_config_t *cfg)
{
	uint32_t checkpoint_seconds = 0;

	if (cfg == NULL)
		return false;
	storage_config_set_defaults(cfg);
	if (storage_eeprom_is_present())
		(void)storage_load_config(cfg);
	if (storage_uptime_checkpoint_load(&checkpoint_seconds) &&
	    checkpoint_seconds > cfg->rtc_uptime_seconds) {
		cfg->rtc_uptime_seconds = checkpoint_seconds;
	}
	cfg->magic = EEPROM_MAGIC;
	cfg->version = EEPROM_CONFIG_VERSION;
	storage_config_sanitize(cfg);
	return storage_eeprom_is_present();
}

static bool uart_save_config(eeprom_config_t *cfg)
{
	if (cfg == NULL || !storage_eeprom_is_present())
		return false;
	cfg->magic = EEPROM_MAGIC;
	cfg->version = EEPROM_CONFIG_VERSION;
	storage_config_sanitize(cfg);
	if (!storage_save_config(cfg))
		return false;
	sensor_battery_apply_config(cfg);
	sensor_pose_apply_config(cfg);
	display_reload_persistent_config();
	return true;
}

static void uart_cmd_get_time(void)
{
	eeprom_config_t cfg;
	rtc_clock_time_t t;

	(void)uart_load_config(&cfg);
	(void)rtc_clock_poll();
	t = rtc_clock_get_time(&cfg);
	if (t.valid) {
		uart_reply("OK TIME RTC %02u:%02u:%02u\r\n",
			   (unsigned)t.hour, (unsigned)t.minute, (unsigned)t.second);
		return;
	}
	t = rtc_clock_get_uptime_time(&cfg);
	uart_reply("OK TIME UPTIME %02u:%02u:%02u\r\n",
		   (unsigned)t.hour, (unsigned)t.minute, (unsigned)t.second);
}

static void uart_cmd_get_bat(void)
{
	sensor_battery_t bat;

	if (!sensor_get_battery(&bat)) {
		uart_reply("ERR BAT unavailable\r\n");
		return;
	}
	uart_reply("OK BAT %u.%03uV raw=%u.%03uV %u%%\r\n",
		   (unsigned)(bat.millivolts / 1000U),
		   (unsigned)(bat.millivolts % 1000U),
		   (unsigned)(bat.raw_millivolts / 1000U),
		   (unsigned)(bat.raw_millivolts % 1000U),
		   (unsigned)bat.percent);
}

static void uart_cmd_get_att(void)
{
	sensor_attitude_t att;
	int roll_x10;
	int pitch_x10;

	if (!sensor_get_attitude(&att)) {
		uart_reply("ERR ATT unavailable\r\n");
		return;
	}
	roll_x10 = (int)(att.roll_deg * 10.0f);
	pitch_x10 = (int)(att.pitch_deg * 10.0f);
	uart_reply("OK ATT roll=%d.%d pitch=%d.%d rot=%d\r\n",
		   roll_x10 / 10, (roll_x10 < 0) ? -(roll_x10 % 10) : (roll_x10 % 10),
		   pitch_x10 / 10, (pitch_x10 < 0) ? -(pitch_x10 % 10) : (pitch_x10 % 10),
		   att.rotating ? 1 : 0);
}

static void uart_process_command(char *line)
{
	eeprom_config_t cfg;
	const char *arg;

	line = (char *)uart_skip_spaces(line);
	if (line[0] == '\0')
		return;
	if (strcmp(line, "HELP") == 0 || strcmp(line, "?") == 0) {
		uart_reply("OK CMDS OTA HELP TIME=HH:MM:SS GET TIME GET BAT GET ATT BATCAL=V BATGAIN=N BATOFF=N POSECAL POSESIGN=+1/-1\r\n");
		return;
	}
	if (strncmp(line, "OTA ", 4) == 0) {
		if (!ota_command_process(line, uart_ota_reply, NULL))
			uart_reply("ERR OTA unknown\r\n");
		return;
	}
	if (strcmp(line, "GET TIME") == 0 || strcmp(line, "TIME?") == 0) {
		uart_cmd_get_time();
		return;
	}
	if (strcmp(line, "GET BAT") == 0 || strcmp(line, "BAT?") == 0) {
		uart_cmd_get_bat();
		return;
	}
	if (strcmp(line, "GET ATT") == 0 || strcmp(line, "ATT?") == 0) {
		uart_cmd_get_att();
		return;
	}
	if (strncmp(line, "TIME=", 5) == 0) {
		uint8_t hour;
		uint8_t minute;
		uint8_t second;

		if (!uart_parse_time(&line[5], &hour, &minute, &second)) {
			uart_reply("ERR TIME format\r\n");
			return;
		}
		if (!rtc_clock_is_ready() && !rtc_clock_poll()) {
			uart_reply("ERR RTC unavailable\r\n");
			return;
		}
		if (!rtc_clock_set_time(hour, minute, second)) {
			uart_reply("ERR TIME set\r\n");
			return;
		}
		if (uart_load_config(&cfg)) {
			cfg.rtc_calib_anchor_raw = rtc_clock_get_raw_seconds();
			(void)uart_save_config(&cfg);
		}
		uart_reply("OK TIME %02u:%02u:%02u\r\n",
			   (unsigned)hour, (unsigned)minute, (unsigned)second);
		return;
	}
	if (strcmp(line, "BATCAL?") == 0) {
		(void)uart_load_config(&cfg);
		uart_reply("OK BATCAL gain=%u offset=%d\r\n",
			   (unsigned)cfg.battery_gain_permyriad,
			   (int)cfg.battery_offset_mv);
		return;
	}
	if (strncmp(line, "BATCAL=", 7) == 0) {
		uint16_t true_mv;
		uint16_t gain;

		if (!uart_parse_mv(&line[7], &true_mv)) {
			uart_reply("ERR BATCAL range\r\n");
			return;
		}
		if (!sensor_battery_calibrate_gain(true_mv, &gain)) {
			uart_reply("ERR BATCAL sample\r\n");
			return;
		}
		if (!uart_load_config(&cfg)) {
			uart_reply("ERR EEPROM unavailable\r\n");
			return;
		}
		cfg.battery_gain_permyriad = gain;
		if (!uart_save_config(&cfg)) {
			uart_reply("ERR BATCAL save\r\n");
			return;
		}
		uart_reply("OK BATCAL gain=%u\r\n", (unsigned)gain);
		return;
	}
	if (strncmp(line, "BATGAIN=", 8) == 0 || strncmp(line, "BATOFF=", 7) == 0) {
		int32_t value;

		arg = strchr(line, '=');
		if (arg == NULL || !uart_parse_int(arg + 1, &value)) {
			uart_reply("ERR BAT param\r\n");
			return;
		}
		if (!uart_load_config(&cfg)) {
			uart_reply("ERR EEPROM unavailable\r\n");
			return;
		}
		if (strncmp(line, "BATGAIN=", 8) == 0)
			cfg.battery_gain_permyriad = (uint16_t)value;
		else
			cfg.battery_offset_mv = (int16_t)value;
		storage_config_sanitize(&cfg);
		if (!uart_save_config(&cfg)) {
			uart_reply("ERR BAT save\r\n");
			return;
		}
		uart_reply("OK BATCAL gain=%u offset=%d\r\n",
			   (unsigned)cfg.battery_gain_permyriad,
			   (int)cfg.battery_offset_mv);
		return;
	}
	if (strcmp(line, "POSECAL") == 0) {
		if (!uart_load_config(&cfg)) {
			uart_reply("ERR EEPROM unavailable\r\n");
			return;
		}
		if (!sensor_pose_capture_config(&cfg)) {
			uart_reply("ERR POSE attitude\r\n");
			return;
		}
		if (!uart_save_config(&cfg)) {
			uart_reply("ERR POSE save\r\n");
			return;
		}
		uart_reply("OK POSECAL roll0=%d pitch0=%d sign=%d\r\n",
			   (int)cfg.pose_roll_zero_x10,
			   (int)cfg.pose_pitch_zero_x10,
			   (int)cfg.pose_roll_sign);
		return;
	}
	if (strncmp(line, "POSESIGN=", 9) == 0) {
		int32_t value;

		if (!uart_parse_int(&line[9], &value) || (value != 1 && value != -1)) {
			uart_reply("ERR POSESIGN range\r\n");
			return;
		}
		if (!uart_load_config(&cfg)) {
			uart_reply("ERR EEPROM unavailable\r\n");
			return;
		}
		cfg.pose_roll_sign = (int8_t)value;
		if (!uart_save_config(&cfg)) {
			uart_reply("ERR POSESIGN save\r\n");
			return;
		}
		uart_reply("OK POSESIGN %d\r\n", (int)cfg.pose_roll_sign);
		return;
	}
	uart_reply("ERR unknown\r\n");
}

static void uart_process_pc_bytes(const uint8_t *data, uint16_t size)
{
	for (uint16_t i = 0; i < size; i++) {
		const char ch = (char)data[i];

		if (ch == '\r' || ch == '\n') {
			if (s_pc_line_len > 0U) {
				s_pc_line[s_pc_line_len] = '\0';
				uart_process_command(s_pc_line);
				s_pc_line_len = 0U;
			}
			continue;
		}
		if (s_pc_line_len + 1U >= UART_LINE_LEN) {
			s_pc_line_len = 0U;
			uart_reply("ERR line too long\r\n");
			continue;
		}
		s_pc_line[s_pc_line_len++] = ch;
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
		if (event.dev == SLAVE_UART &&
		    event.size >= sizeof(uint32_t) &&
		    uart_load_le32(event.data) == OTA_BINARY_MAGIC) {
			(void)ota_binary_process(event.data, event.size,
						 uart_ota_reply, NULL);
			continue;
		}
		if (event.dev == SLAVE_UART)
			uart_process_pc_bytes(event.data, event.size);
		else
			forward_uart(event.dev, event.data, event.size);
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

	xTaskCreate(uart_forward_task, "uart_fwd", UART_FORWARD_TASK_STACK_WORDS,
		    NULL, 5, NULL);
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
	if (event.size > USART_LEN)
		event.size = USART_LEN;
	memcpy(event.data, dev->rx_buf, event.size);
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
