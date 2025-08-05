#include "key.h"
#define KEY1_GPIO_PORT      GPIOB
#define KEY1_GPIO_PIN       GPIO_PIN_12
uint16_t SHORT_CLICK_THRESHOLD = 400;  // 这个是第一次松开时间和第二次按下时间的判断时长
uint16_t LONG_PRESS_THRESHOLD = 1000;   // 定义长按的时间阈值（以FreeRTOS时基为单位）
uint16_t PRESS_Time = 150;             // 判断毛刺时长
uint16_t BUTTON_ERROR_Time = 1500;   // 按键长久状态卡死阈值

ButtonState button_scan(bool istiming ,ButtonState *buttonState){

        TickType_t pressStartTime = 0;        //记录按下时间
        TickType_t lastReleaseTime = 0;       //记录上一次释放时间
        TickType_t ReleaseTime = 0;
        TickType_t startTime;
        if(istiming){
                startTime = xTaskGetTickCount();
                printf ("startTime:%d\r\n",startTime);
        }

        while (1){
                delay_ms(20);
                int keyStatus = HAL_GPIO_ReadPin(KEY1_GPIO_PORT, KEY1_GPIO_PIN);
                TickType_t currentTime = xTaskGetTickCount();

                switch (*buttonState){
                        case IDLE_STATE:
                                if(istiming && currentTime - startTime > 4000){
                                        printf ("IDLE_STATE\r\n");
                                        return IDLE_STATE;
                                }
                                if (keyStatus == 0){
                                        *buttonState = PRESS_DETECTED_STATE;
                                        pressStartTime = currentTime;
                                }
                                break;
                        case PRESS_DETECTED_STATE:
                                if (keyStatus == 1){
                                        ReleaseTime = currentTime;
                                        *buttonState = RELEASE_DETECTED_STATE;
                                }else if (currentTime - pressStartTime > LONG_PRESS_THRESHOLD){
                                        *buttonState = LONG_PRESS_STATE;
                                }else if(currentTime - pressStartTime > BUTTON_ERROR_Time){
                                        *buttonState = IDLE_STATE;
                                }
                                break;
                        case LONG_PRESS_STATE_END:
                                // printf ("LONG_PRESS_STATE_END\r\n");
                                if (keyStatus == 1){
                                        *buttonState = IDLE_STATE;
                                }
                                break;
                        case RELEASE_DETECTED_STATE:
                                if ( (keyStatus == 0) && (currentTime - ReleaseTime < SHORT_CLICK_THRESHOLD)){
                                        *buttonState = DOUBLE_PRESS_STATE;
                                }else if ( (ReleaseTime - pressStartTime > PRESS_Time) && (currentTime - ReleaseTime > SHORT_CLICK_THRESHOLD)){
                                        *buttonState = SHORT_PRESS_STATE;
                                }else if (currentTime - ReleaseTime > BUTTON_ERROR_Time){
                                        *buttonState = IDLE_STATE;
                                }
                                break;

                        case SHORT_PRESS_STATE:
                                printf ("SHORT\r\n");
                                *buttonState = LONG_PRESS_STATE_END;
                                return SHORT_PRESS_STATE;
                        case LONG_PRESS_STATE:
                                printf ("LONG\r\n");
                                *buttonState = LONG_PRESS_STATE_END;
                                return LONG_PRESS_STATE;

                        case DOUBLE_PRESS_STATE:
                                printf ("DOUBLE\r\n");
                                *buttonState = LONG_PRESS_STATE_END;
                                return DOUBLE_PRESS_STATE;
                }

        }
}