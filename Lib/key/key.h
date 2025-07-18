#ifndef __KEY_H_
#define __KEY_H_
// #include "main.h"
#include "FreeRTOS.h"
#include "gpio.h"
typedef enum
{
        IDLE_STATE,   //空闲
        PRESS_DETECTED_STATE,  //按键按下
        RELEASE_DETECTED_STATE, //按键释放
        SHORT_PRESS_STATE,     //短按
        LONG_PRESS_STATE,     //长按
        DOUBLE_PRESS_STATE,    //双击
        LONG_PRESS_STATE_END //长按结束状态
} ButtonState;
ButtonState button_press_pattern_scan(void);

#endif
