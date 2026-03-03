#include "key.h"
#include "task.h"

#if 0
#define KEY1_GPIO_PORT   GPIOB
#define KEY1_GPIO_PIN    GPIO_PIN_12
#endif
#define KEY1_GPIO_PORT   GPIOA
#define KEY1_GPIO_PIN    GPIO_PIN_1

/** Confirm timeout when istiming: 3s then return IDLE_STATE */
#define CONFIRM_TIMEOUT_TICKS  pdMS_TO_TICKS(3000)
#define SCAN_INTERVAL_MS       10   /* 扫描间隔 10ms，提高响应 */

uint16_t SHORT_CLICK_THRESHOLD = 400;   /* ms: max gap between two clicks for double */
uint16_t LONG_PRESS_THRESHOLD  = 1000;  /* ticks: long press threshold */
uint16_t PRESS_Time            = 50;    /* ticks: 防抖 50ms，更快识别短按 */
uint16_t BUTTON_ERROR_Time     = 1500;  /* ticks: stuck timeout */

ButtonState button_scan(bool istiming, ButtonState *buttonState)
{
        TickType_t pressStartTime = 0;
        TickType_t releaseTime = 0;
        TickType_t startTime = 0;

        if (istiming)
                startTime = xTaskGetTickCount();

        for (;;) {
                vTaskDelay(pdMS_TO_TICKS(SCAN_INTERVAL_MS));

                GPIO_PinState keyStatus = HAL_GPIO_ReadPin(KEY1_GPIO_PORT, KEY1_GPIO_PIN);
                TickType_t now = xTaskGetTickCount();

                switch (*buttonState) {
                case IDLE_STATE:
                        if (istiming && (now - startTime > CONFIRM_TIMEOUT_TICKS))
                                return IDLE_STATE;
                        if (keyStatus == GPIO_PIN_RESET) {
                                *buttonState = PRESS_DETECTED_STATE;
                                pressStartTime = now;
                        }
                        break;

                case PRESS_DETECTED_STATE:
                        if (keyStatus == GPIO_PIN_SET) {
                                releaseTime = now;
                                *buttonState = RELEASE_DETECTED_STATE;
                        } else if (now - pressStartTime > LONG_PRESS_THRESHOLD) {
                                *buttonState = LONG_PRESS_STATE;
                        } else if (now - pressStartTime > BUTTON_ERROR_Time) {
                                *buttonState = IDLE_STATE;
                        }
                        break;

                case LONG_PRESS_STATE_END:
                        if (keyStatus == GPIO_PIN_SET)
                                *buttonState = IDLE_STATE;
                        break;

                case RELEASE_DETECTED_STATE:
                        if (keyStatus == GPIO_PIN_RESET && (now - releaseTime < SHORT_CLICK_THRESHOLD))
                                *buttonState = DOUBLE_PRESS_STATE;
                        else if ((releaseTime - pressStartTime > PRESS_Time) &&
                                 (now - releaseTime > SHORT_CLICK_THRESHOLD))
                                *buttonState = SHORT_PRESS_STATE;
                        else if (now - releaseTime > BUTTON_ERROR_Time)
                                *buttonState = IDLE_STATE;
                        break;

                case SHORT_PRESS_STATE:
                        *buttonState = LONG_PRESS_STATE_END;
                        return SHORT_PRESS_STATE;

                case LONG_PRESS_STATE:
                        *buttonState = LONG_PRESS_STATE_END;
                        return LONG_PRESS_STATE;

                case DOUBLE_PRESS_STATE:
                        *buttonState = LONG_PRESS_STATE_END;
                        return DOUBLE_PRESS_STATE;
                }
        }
}
