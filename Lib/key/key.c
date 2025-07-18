#include "key.h"






#define KEY1_INT_GPIO_PORT      GPIOB
#define KEY1_INT_GPIO_PIN       GPIO_PIN_12
uint16_t SHORT_CLICK_THRESHOLD = 400;  // 这个是第一次松开时间和第二次按下时间的判断时长
uint16_t LONG_PRESS_THRESHOLD = 1000;   // 定义长按的时间阈值（以FreeRTOS时基为单位）
uint16_t PRESS_Time = 200;             // 判断毛刺时长
uint16_t BUTTON_ERROR_Time = 1500;   // 按键长久状态卡死阈值

ButtonState button_press_pattern_scan(void){

        ButtonState buttonState = IDLE_STATE;  //按键状态
        TickType_t pressStartTime = 0;        //记录按下时间
        TickType_t lastReleaseTime = 0;       //记录上一次释放时间
        TickType_t ReleaseTime = 0;

        while (1){
                vTaskDelay (10);
                int keyStatus = HAL_GPIO_ReadPin(KEY1_INT_GPIO_PORT, KEY1_INT_GPIO_PIN);  //检测按键
                TickType_t currentTime = xTaskGetTickCount();

                switch (buttonState){
                        case IDLE_STATE:
                                //如果按键按下
                                if (keyStatus == 0){
                                        buttonState = PRESS_DETECTED_STATE; //切换到按键按下状态
                                        pressStartTime = currentTime;       //记录按下时间
                                }
                                break;
                        //按键 按下状态
                        case PRESS_DETECTED_STATE:
                                //检测按键松开时间    
                                if (keyStatus == 1){
                                        ReleaseTime = currentTime;  //记录按键放松时间
                                        buttonState = RELEASE_DETECTED_STATE;    //如果按下后释放则进入这里
                                }
                                 //判断按键长按的时长如果符合则跳转到这里。
                                else if (currentTime - pressStartTime > LONG_PRESS_THRESHOLD){
                                        buttonState = LONG_PRESS_STATE;
                                }
                                //防止按键卡死
                                else if(currentTime - pressStartTime>BUTTON_ERROR_Time){
                                        buttonState = IDLE_STATE;
                                }
                                break;
                        //长按结束后的状态跳转这里等待按键松开 防止一直处于长按状态
                        case LONG_PRESS_STATE_END:
                                if (keyStatus == 1){
                                        buttonState = IDLE_STATE;
                                }
                                break;
                        //按键释放过后的状态
                        case RELEASE_DETECTED_STATE:
                                //如果按键再次按下并且第二次时长是在500ms以内按下的
                                if ( (keyStatus == 0) && (currentTime - ReleaseTime < SHORT_CLICK_THRESHOLD)){
                                        buttonState = DOUBLE_PRESS_STATE;
                                }
                                 //判断按键按下时长防止毛刺 在判断当前是否在双击范围内
                                else if ( (ReleaseTime - pressStartTime > PRESS_Time) && (currentTime - ReleaseTime > SHORT_CLICK_THRESHOLD)){
                                        buttonState = SHORT_PRESS_STATE;
                                }
                                //按键出现无法判断情况回到初态
                                else if (currentTime - ReleaseTime > BUTTON_ERROR_Time){
                                        buttonState = IDLE_STATE;
                                }
                                break;

                        case SHORT_PRESS_STATE:
                                printf ("SHORT\r\n");
                                return SHORT_PRESS_STATE;
                                // buttonState = LONG_PRESS_STATE_END;
                                // break;

                        case LONG_PRESS_STATE:
                                printf ("LONG\r\n");
                                return LONG_PRESS_STATE;
                                // buttonState = LONG_PRESS_STATE_END;
                                // break;

                        case DOUBLE_PRESS_STATE:
                                printf ("DOUBLE\r\n");
                                return DOUBLE_PRESS_STATE;
                                // buttonState = LONG_PRESS_STATE_END;
                                // break;
                }

        }
}