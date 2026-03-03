#ifndef __KEY_H_
#define __KEY_H_
// #include "main.h"
#include "FreeRTOS.h"
#include "gpio.h"
#include "utils.h"

/** 电源使能脚（板级定义）：上电拉高、关机拉低 */
#define PowerOn   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET)
#define PowerDown HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET)

typedef enum{
        IDLE_STATE=0,   //空闲
        PRESS_DETECTED_STATE,  //按键按下
        RELEASE_DETECTED_STATE, //按键释放
        SHORT_PRESS_STATE,     //短按
        LONG_PRESS_STATE,     //长按
        DOUBLE_PRESS_STATE,    //双击
        LONG_PRESS_STATE_END //长按结束状态
}ButtonState;

ButtonState button_scan(bool istiming, ButtonState *buttonState);
#endif
