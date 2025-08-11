#ifndef __UTILS_H__
#define __UTILS_H__
// #define U8G2_ENABLED
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
        uint32_t ticks;
        uint32_t told,tnow,reload,tcnt=0;
        if((0x0001&(SysTick->CTRL)) ==0)    //定时器未工作
                vPortSetupTimerInterrupt();  //初始化定时器

        reload = SysTick->LOAD;                     //获取重装载寄存器值
        ticks = nus * (SystemCoreClock / 1000000);  //计数时间值

        vTaskSuspendAll();//阻止OS调度，防止打断us延时
        told=SysTick->VAL;  //获取当前数值寄存器值（开始时数值）
        while(1)
        {
                tnow=SysTick->VAL; //获取当前数值寄存器值
                if(tnow!=told)  //当前值不等于开始值说明已在计数
                {         
                        if(tnow<told)  //当前值小于开始数值，说明未计到0
                                tcnt+=told-tnow; //计数值=开始值-当前值

                        else     //当前值大于开始数值，说明已计到0并重新计数
                                tcnt+=reload-tnow+told;   //计数值=重装载值-当前值+开始值  （
                                                        //已从开始值计到0） 

                        told=tnow;   //更新开始值
                        if(tcnt>=ticks)break;  //时间超过/等于要延迟的时间,则退出.
                } 
        }  
        xTaskResumeAll();	//恢复OS调度		   
} 

// #define delay_ms(ms) HAL_Delay(ms)
#define delay_ms(ms) vTaskDelay(ms)
#define delay_us(us) os_delay_us(us)

///////////////////////////////////////////////// log


#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

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