#include "key.h"


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



#define KEY1_INT_GPIO_PORT      GPIOB
#define KEY1_INT_GPIO_PIN       GPIO_PIN_11
uint16_t SHORT_CLICK_THRESHOLD = 400;  // 这个是第一次松开时间和第二次按下时间的判断时长
uint16_t LONG_PRESS_THRESHOLD = 1000;   // 定义长按的时间阈值（以FreeRTOS时基为单位）
uint16_t PRESS_Time = 100;             // 判断毛刺时长
uint16_t BUTTON_ERROR_Time = 1500;   // 按键长久状态卡死阈值

void key_scan1111(void){

        ButtonState buttonState = IDLE_STATE;  //按键状态
        TickType_t pressStartTime = 0;        //记录按下时间
        TickType_t lastReleaseTime = 0;       //记录上一次释放时间
        TickType_t ReleaseTime = 0;

        while (1){
                int keyStatus = GPIO_ReadInputDataBit (KEY1_INT_GPIO_PORT, KEY1_INT_GPIO_PIN);  //检测按键
                TickType_t currentTime = xTaskGetTickCount();

                switch (buttonState){
                        case IDLE_STATE:
                                //如果按键按下
                                if (keyStatus == 0){
                                        buttonState = PRESS_DETECTED_STATE; //切换到按键按下状态
                                        pressStartTime = currentTime;       //记录按下时间
                                }
                                break;

                        case PRESS_DETECTED_STATE:    //按键 按下状态
                                //检测按键松开时间    
                                if (keyStatus == 1){
                                        ReleaseTime = currentTime;  //记录按键放松时间
                                        buttonState = RELEASE_DETECTED_STATE;    //如果按下后释放则进入这里
                                }
                                else if (currentTime - pressStartTime > LONG_PRESS_THRESHOLD) //判断按键长按的时长如果符合则跳转到这里。
                                {
                                        buttonState = LONG_PRESS_STATE;
                                }
                                else if(currentTime - pressStartTime>BUTTON_ERROR_Time) //防止按键卡死
                                {
                                        buttonState = IDLE_STATE;
                                }
                                break;

                        case LONG_PRESS_STATE_END:    //长按结束后的状态跳转这里等待按键松开 防止一直处于长按状态   /
                                if (keyStatus == 1){
                                        buttonState = IDLE_STATE;
                                }
                                break;

                        case RELEASE_DETECTED_STATE:    //按键释放过后的状态
                                if ( (keyStatus == 0) && (currentTime - ReleaseTime < SHORT_CLICK_THRESHOLD))        //如果按键再次按下并且第二次时长是在500ms以内按下的
                                {
                                        buttonState = DOUBLE_PRESS_STATE;
                                }
                                else if ( (ReleaseTime - pressStartTime > PRESS_Time) && (currentTime - ReleaseTime > SHORT_CLICK_THRESHOLD)) //判断按键按下时长防止毛刺 在判断当前是否在双击范围内
                                {
                                        buttonState = SHORT_PRESS_STATE;
                                }
                                else if (currentTime - ReleaseTime > BUTTON_ERROR_Time) //按键出现无法判断情况回到初态
                                {
                                        buttonState = IDLE_STATE;
                                }
                                break;

                        case SHORT_PRESS_STATE:
                                printf ("单击\r\n");
                                buttonState = LONG_PRESS_STATE_END;
                                break;

                        case LONG_PRESS_STATE:
                                printf ("长按\r\n");
                                buttonState = LONG_PRESS_STATE_END;
                                break;

                        case DOUBLE_PRESS_STATE:
                                buttonState = LONG_PRESS_STATE_END;
                                printf ("双击\r\n");
                                break;
                }
                vTaskDelay (10);
        }
}