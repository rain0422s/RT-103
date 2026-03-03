#ifndef __LED_CONTROL_H
#define __LED_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"

/* 灯光控制命令：1=蓝灯闪4s 2=关蓝灯 3=开蓝灯 4=开呼吸灯 5=关呼吸灯 6=就绪(呼吸+闪4s) 7=全关 */
enum {
	LED_CMD_BLINK_4S = 1,
	LED_CMD_OFF,
	LED_CMD_ON,
	LED_CMD_BREATH_ON,
	LED_CMD_BREATH_OFF,
	LED_CMD_READY,
	LED_CMD_ALL_OFF
};
#define LED_CTL_QUEUE_LEN  4

extern QueueHandle_t led_ctl_queue;

void led_control_send(uint8_t cmd);
void breathing_led_set(bool on);
/** 执行一次呼吸灯周期（若未使能则立即返回），供 sensor_task 等调用 */
void breathing_led_run_once(void);
/** 蓝灯闪烁 times×(on_ms 亮, off_ms 灭)，阻塞（仅无定时器时备用） */
void led_blink(uint8_t times, uint32_t on_ms, uint32_t off_ms);
void led_control_task(void *arg);

#endif
