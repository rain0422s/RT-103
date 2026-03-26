#ifndef __UTILS_H__
#define __UTILS_H__
///////////////////////////////////////////////// delay

#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
extern void vPortSetupTimerInterrupt(void);
// static void HAL_Delay_us(uint32_t us)
// {
//     HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000000);
//     HAL_Delay(us - 1);
//     HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000);
// }

static void os_delay_us(uint32_t nus)
{
        if (nus == 0u)
                return;

        if ((SysTick->CTRL & 0x0001u) == 0u)   // 定时器未工作
                vPortSetupTimerInterrupt();     // 初始化定时器

        // SysTick 每 1us 的 tick 数（72MHz 下为 72）
        const uint32_t ticks_per_us = (SystemCoreClock / 1000000u);
        if (ticks_per_us == 0u)
                return;

        // 防止 nus * ticks_per_us 溢出
        uint64_t target_ticks_64 = (uint64_t)nus * (uint64_t)ticks_per_us;
        uint32_t target_ticks = (target_ticks_64 > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)target_ticks_64;

        const uint32_t reload_plus_one = SysTick->LOAD + 1u;
        uint32_t elapsed_ticks = 0u;
        uint32_t told = SysTick->VAL;
        uint32_t tnow;

        BaseType_t sched_suspended = pdFALSE;
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
                vTaskSuspendAll();  // 阻止 OS 调度，防止打断 us 延时
                sched_suspended = pdTRUE;
        }

        while (elapsed_ticks < target_ticks) {
                tnow = SysTick->VAL;
                if (tnow != told) {
                        if (tnow < told) {
                                elapsed_ticks += (told - tnow);                     // 未回绕
                        } else {
                                elapsed_ticks += (told + reload_plus_one - tnow);   // 已回绕
                        }
                        told = tnow;
                }
        }

        if (sched_suspended == pdTRUE)
                xTaskResumeAll();  // 恢复 OS 调度
} 

// #define delay_ms(ms) HAL_Delay(ms)
#define delay_ms(ms) vTaskDelay(ms)
#define delay_us(us) os_delay_us(us)

/** 忙等延时（毫秒），不让出 CPU，慎在任务中使用 */
static inline void dly_ms(uint32_t ms)
{
	/* 每 1 毫秒约需循环 72000 次（72MHz/1000），加倍保险系数约 10 */
	const uint32_t count_per_ms = 7200;
	for (uint32_t i = 0; i < (ms * count_per_ms); i++) {
		__NOP();
	}
}

///////////////////////////////////////////////// log


#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* Compile-time debug printing.
 * DEBUG_PRINT=0: compile out all DBG_PRINTF calls.
 * DEBUG_PRINT=1: DBG_PRINTF maps to printf.
 */
#ifndef DEBUG_PRINT
#define DEBUG_PRINT 0
#endif
/*
 * Use a normal if-statement so call sites don't need #if blocks.
 * With DEBUG_PRINT=0, the compiler will optimize the whole branch away.
 */
#define DBG_PRINTF(...) do { if (DEBUG_PRINT) printf(__VA_ARGS__); } while (0)

// separator
#define SEPARATOR30       "-----------------------------"
#define SEPARATOR60       "----------------------------------------------------------"

// print with newline
#define println(fmt, ...) printf(fmt "\r\n", ##__VA_ARGS__)

// print variable
#define printv(fmt, var)  printf("[ line: %d | function: %s ] %s = " fmt "\r\n", __LINE__, __FUNCTION__, #var, var)

// print code
// #define print_code(code) printf("[ " #code "]"), code
#define print_code(code)  println("[ " #code "]"), code

static void print_binary(uint8_t n)
{
    for (uint8_t i = 0x80; i > 0; i >>= 1)
        printf("%c", n & i ? '1' : '0');
}

#if 1
// #define INLINE #pragma inline
// #define INLINE __inline
// #define INLINE __forceinline
#define INLINE __attribute__((always_inline))
#else
// #define INLINE
#define INLINE inline
#endif

///////////////////////////////////////////////// other

// number of elements in an array
#define ARRAY_SIZE(a)                    (sizeof(a) / sizeof((a)[0]))
// byte offset of member in structure
#define MEMBER_OFFSET(structure, member) ((int)&(((structure*)0)->member))
// size of a member of a structure
#define MEMBER_SIZE(structure, member)   (sizeof(((structure*)0)->member))

#define swap_int(a, b)                   (a ^= b, b ^= a, a ^= b)  // a ^= b ^= a ^= b

static uint8_t calc_crc(uint8_t arr[], uint8_t len)
{
    uint8_t i, j, byte, carry, crc = 0;
#if 0
    for (i = 0; i < len; ++i) {
        byte = arr[i];
        for (j = 8; j != 0; --j) {
            carry = (crc ^ byte) & 0x80;
            crc <<= 1;
            if (carry) crc ^= 0x7;
            byte <<= 1;
        }
    }
#else
    for (i = 0; i < len; ++i) {
        byte = crc ^ arr[i];
        for (j = 0; j < 8; ++j) {
            carry = byte & 0x80;
            byte <<= 1;
            if (carry) byte ^= 0x7;
        }
        crc = byte;
    }
#endif
    return crc;
}

#endif