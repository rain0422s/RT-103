/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "i2c.h"
#include "rtc.h"
#include "spi.h"
#include "usart.h"
#include "usb.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "i2cdev.h"
#include "string.h"
#include "stdio.h"
#include "sht3x.h"
#include "crc8.h"
#include "w25qxx.h"
// #include "my_mpu6050.h"
#include "oled.h"
#include "at24cxx.h"
#include "atk_ms6050.h"
#include <utils.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint32_t  i=500; //50ms
uint8_t  wData[0x100];
uint8_t  rData[0x100];
uint8_t  ID[4];

typedef struct {
    int   i[2];
    float f;
} object_t;
  uint16_t ADC_Value[100];
  float ADC_Vol;
int adc;
        u8g2_t u8g2;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
// #define W25Q16 	0XEF14
// #define	SPI_FLASH_CS_0    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_0,GPIO_PIN_RESET)
// #define SPI_FLASH_CS_1    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_0,GPIO_PIN_SET)

// static char SPI1_ReadWriteByte(uint8_t txdata)
// {
// 	uint8_t rxdata=00;
// 	HAL_SPI_TransmitReceive(&hspi1,&txdata,&rxdata,1,3);
// 	return rxdata;
// }
 
 
//ID W25X16ID:0XEF14
// uint16_t SPI_Flash_ReadID(void)
// {
// 	uint16_t Temp = 0;	  
// 	SPI_FLASH_CS_0; 		    
// 	SPI1_ReadWriteByte(0x90);  
// 	SPI1_ReadWriteByte(0x00); 	    
// 	SPI1_ReadWriteByte(0x00); 	    
// 	SPI1_ReadWriteByte(0x00); 	 			   
// 	Temp |= SPI1_ReadWriteByte(0xFF) << 8;  
// 	Temp |= SPI1_ReadWriteByte(0xFF);	 
// 	SPI_FLASH_CS_1; 	
//            printf(" %d\n",Temp);         
// 	return Temp;
// } 
void loop2(void) {
  /*循环输出字符，同时输出两端字符，x轴不断改变实现移动�??
   * 第一段移动范�?0-----128-----256
   * 第二段移动范�?-128-----0------128
   */
   int y=1;//调节位移速度，可用于加快字符位移速度
  for(int x=0;x<256;x+=y){//x+=y ----->等价于x=x+y
      u8g2_ClearBuffer(&u8g2);         // 清除内部缓冲�?
      u8g2_SetFont(&u8g2, u8g2_font_4x6_tr);//设置字体
      u8g2_DrawStr(&u8g2, x, 10, "Hello World!");
      u8g2_DrawStr(&u8g2, x-128,10,"Hello World!");
      delay_ms(20);//延时程序，可以降低位移�?�度
      u8g2_SendBuffer(&u8g2);          // transfer internal memory to the displa
  }
}

short frame_len,frame_len_trg;//框框的宽�??
short frame_y,frame_y_trg;//框框的高�??
signed  char ui_select = 0;

typedef struct
{
  uint8_t val;
  uint8_t last_val;
}KEY_T;
typedef struct
{
  uint8_t id;
  uint8_t press;
  uint8_t update_flag;
  uint8_t res;
}KEY_MSG;

typedef struct
{
  char* str;
  char  len;
}SETTING_LIST;

SETTING_LIST list[] = 
{
  {"list",4},
  {"ab",2},
  {"abc",3},
  {"abcd",4},
};
short x,x_trg; //x当前位置数�??,x_trg 目标坐标�??
short y = 13,y_trg = 10;
int state;
KEY_T key[2] = {0};
KEY_MSG key_msg = {0};

short abs(short i)
{
    if (i<0)
        return ~(--i);
    return i;
}
#define CHECK_KEY(n)  n?  HAL_GPIO_ReadPin(GPIOB ,GPIO_PIN_5) : HAL_GPIO_ReadPin(GPIOB ,GPIO_PIN_1);
void key_scan(void)
{
  for(int i = 0;i<2;i++)
  {
   key[i].val  = CHECK_KEY(i);
    if(key[i].val != key[i].last_val)
    {
      key[i].last_val = key[i].val;
      if(key[i].val == 0)
      {
        key_msg.id = i;
        key_msg.press = 1;
        key_msg.update_flag = 1;
      }
    }
  }
}
int ui_run(short *a,short *a_trg,uint8_t step,uint8_t slow_cnt)
{
  uint8_t temp;
    temp = abs(*a_trg-*a) > slow_cnt ? step : 1;
  if(*a < *a_trg)
  {
    *a += temp;
  }
  else if( *a > *a_trg)
  {
    *a -= temp;    
  }
  else
  {
    return 0;
  }
  return 1;
}

void ui_show(void)
{
  int list_len = sizeof(list) / sizeof(SETTING_LIST);
  u8g2_ClearBuffer(&u8g2);         // 清除内部缓冲�??

  for(int i = 0 ;i < list_len;i++)
  {
    u8g2_DrawStr(&u8g2,x+2,y+i*18,list[i].str);  // 第一段输出位�??
  }
  u8g2_DrawRFrame(&u8g2,x, frame_y,frame_len, 20, 3);
  ui_run(&frame_y,&frame_y_trg,5,4);
  ui_run(&frame_len,&frame_len_trg,10,5);
  //ui_run(&y,&y_trg);
  u8g2_SendBuffer(&u8g2);      // transfer internal memory to the displa
}
bool ui_flag = true;
void ui_proc(void)
{
  int list_len = sizeof(list) / sizeof(SETTING_LIST);
  if(key_msg.update_flag && key_msg.press)
  {
    key_msg.update_flag = 0;
   // if(!key_msg.id || ui_flag)
   if( ui_flag)
    {
      ui_select++;
     // y_trg += 10;
        // if(ui_select >= list_len)
        if(ui_select == list_len-1)
     {
        ui_flag = false;
     //  ui_select = list_len - 1;  

     }
    }
    else
    {
      ui_select--;
     // y_trg -= 10;
//        if(ui_select < 0)
       if(ui_select == 0)
      {
        //ui_select = 0;  
        ui_flag = true;
      }
    }
    frame_y_trg = ui_select*15;
    frame_len_trg = list[ui_select].len*13;
  }
  ui_show();
}
void loop1(void) 
{
		//printf("Hello");
  key_scan();
  ui_proc();  
}


/**
 * @brief       例程演示入口函数
 * @param       �??
 * @retval      �??
 */
void demo_run(void)
{
    uint8_t ret;

    uint8_t niming_report = 0;
    float pit, rol, yaw;
    int16_t acc_x, acc_y, acc_z;
    int16_t gyr_x, gyr_y, gyr_z;
    int16_t temp;
    

    
//     /* LCD UI初始�?? */
//     demo_lcd_ui_init();
            printf("11111111111111122222222233333333\r\n");
//     while (1)
//     {
        // key = key_scan(0);
        // if (key == KEY0_PRES)
        // {
        //     /* 当按�??0按下后，切换串口的上传状�??
        //      * 当niming_report�??0时，上传相关信息至串口调试助�??
        //      * 当niming_report�??1时，上传相关信息至匿名地面站V4
        //      */
        //     niming_report = 1 - niming_report;
        //     if (niming_report == 0)
        //     {
        //         /* 串口调试助手的串口�?�讯波特率为115200 */
        //         usart_init(115200);
        //     }
        //     else
        //     {
        //         /* 匿名地面站V4的串口�?�讯波特率为500000 */
        //         usart_init(500000);
        //     }
        // }
                          while(atk_ms6050_dmp_get_data(&pit,&rol,&yaw)!=0){}
        /* 获取ATK-MS6050 DMP处理后的数据 */
       // ret  = atk_ms6050_dmp_get_data(&pit, &rol, &yaw);
            printf("[ line: %d | function: %s ] ret = %d" "\r\n", __LINE__, __FUNCTION__, ret);
        /* 获取ATK-MS6050加�?�度�?? */
        ret += atk_ms6050_get_accelerometer(&acc_x, &acc_y, &acc_z);
            printf("[ line: %d | function: %s ] ret = %d" "\r\n", __LINE__, __FUNCTION__, ret);
        /* 获取ATK-MS6050�??螺仪�?? */
        ret += atk_ms6050_get_gyroscope(&gyr_x, &gyr_y, &gyr_z);
            printf("[ line: %d | function: %s ] ret = %d" "\r\n", __LINE__, __FUNCTION__, ret);
        /* 获取ATK-MS6050温度�?? */
        ret += atk_ms6050_get_temperature(&temp);
            printf("[ line: %d | function: %s ] ret = %d" "\r\n", __LINE__, __FUNCTION__, ret);
        if (ret == 0)
        {
        //     if (niming_report == 0)
        //     {
                /* 上传相关数据信息至串口调试助�?? */
                printf("pit: %.2f, rol: %.2f, yaw: %.2f, ", pit, rol, yaw);
                printf("acc_x: %d, acc_y: %d, acc_z: %d, ", acc_x, acc_y, acc_z);
                printf("gyr_x: %d, gyr_y: %d, gyr_z: %d, ", gyr_x, gyr_y, gyr_z);
                printf("temp: %d\r\n", temp);
        //    }
        //     else
        //     {
        //         /* 上传状�?�帧和传感器帧至匿名地面站V4 */
        //         demo_niming_report_status((int16_t)(rol * 100), (int16_t)((pit) * 100), (int16_t)(yaw * 100), 0, 0, 0);
        //         demo_niming_report_senser(acc_x, acc_y, acc_z, gyr_x, gyr_y, gyr_z, 0, 0, 0);
        //     }
            

        }
//     }
}



void SPI_Flash_Test(void)
{
	printf("SPI-W25Qxxx Example \n");

    /*1- Read the device ID */
    W25QXX_Init();
    W25QXX_Read_ID(ID);
    printf("W25Qxxx ID is : ");
    for (i = 0; i < 2; i++) {
        printf("0x%02X ", ID[i]);
    }
    printf("\n");

    /* 2- Erase */
    if (W25QXX_Erase_Block(0) == W25Qx_OK)
        printf(" SPI Erase Block ok\n");
    else
        Error_Handler();

    /*-2- Written to the flash */
    /* fill buffer */
    for (i = 0; i < 0x100; i++) {
        wData[i] = i;
        rData[i] = 0;
    }

    if (W25QXX_Write(wData, 0x00, 0x100) == W25Qx_OK)
        printf(" SPI Write ok\n");
    else
        Error_Handler();

    /* 3- Read the flash */
    if (W25QXX_Read(rData, 0x00, 0x100) == W25Qx_OK)
        printf(" SPI Read ok\n");
    else
        Error_Handler();

    printf("SPI Read Data : \n");
    for (i = 0; i < 0x100; i++)
        printf("0x%02X  ", rData[i]);
    printf("\n");

    /* 4- check date */
    if (memcmp(wData, rData, 0x100) == 0)
        printf(" W25Q64FV SPI Test OK\n");
    else
        printf(" W25Q64FV SPI Test False\n");

        return ;
} 

void draw(u8g2_t *u8g2)
{
	u8g2_ClearBuffer(u8g2);
    u8g2_SetFontMode(u8g2, 1); /*字体模式选择*/
    u8g2_SetFontDirection(u8g2, 0); /*字体方向选择*/
    u8g2_SetFont(u8g2, u8g2_font_inb24_mf); /*字库选择*/
    u8g2_DrawStr(u8g2, 0, 20, "U");
    
    u8g2_SetFontDirection(u8g2, 1);
    u8g2_SetFont(u8g2, u8g2_font_inb30_mn);
    u8g2_DrawStr(u8g2, 21,8,"8");
        
    u8g2_SetFontDirection(u8g2, 0);
    u8g2_SetFont(u8g2, u8g2_font_inb24_mf);
    u8g2_DrawStr(u8g2, 51,30,"g");
    u8g2_DrawStr(u8g2, 67,30,"\xb2");
    
    u8g2_DrawHLine(u8g2, 2, 35, 47);
    u8g2_DrawHLine(u8g2, 3, 36, 47);
    u8g2_DrawVLine(u8g2, 45, 32, 12);
    u8g2_DrawVLine(u8g2, 46, 33, 12);
  
    u8g2_SetFont(u8g2, u8g2_font_4x6_tr);
    u8g2_DrawStr(u8g2, 1,54,"github.com/olikraus/u8g2");
		  u8g2_SendBuffer(u8g2);
}

void loop(u8g2_t *u8g2) {
  u8g2_ClearBuffer(u8g2);
  u8g2_SetFont(u8g2,u8g2_font_ncenB14_tr);
  u8g2_DrawStr(u8g2,0,20,"Hello World!");
  u8g2_SendBuffer(u8g2);
}


 /**
   * @brief   读取MPU6050的ID
   * @param
   * @retval  正常返回1，异常返�??0
   */
 uint8_t MPU6050ReadID(void)
 {
     unsigned char Re = 0;
//       HAL_I2C_Master_Receive(&hi2c2, 0xd0<1,&Re,1,20);    //读器件地�??
      HAL_I2C_Mem_Read(&hi2c2, 0xd0, 0X75, 1, &Re, 1, 0xffff);
      if (Re != 0x68) {
          println("�??测不到MPU6050模块，请�??查模块与�??发板的接�??");
          return 0;
      } else {
          println("MPU6050 ID = %x\r\n",Re);
          return 1;
      }

  }
 static const unsigned char u8g_logo_bits[] U8X8_PROGMEM =
{
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xC0,0x00,0x00,0x00,0xE0,0x01,0x00,0x00,0xF0,0x01,0x00,0x00,0xF8,0x01,0x00,0x00,0xFC,0x01,0x00,0x00,0xFE,0x01,0x00,
	0x30,0xFE,0xFF,0x01,0x3C,0xFE,0xFF,0x01,0x3C,0xFE,0xFF,0x01,0x3C,0xFE,0xFF,0x01,0x3C,0xFE,0xFF,0x01,0x3C,0xFE,0xFF,0x01,0x3C,0xFE,0xFF,0x01,0x3C,0xFE,0xFF,0x01,
	0x3C,0xFE,0xFF,0x00,0x3C,0xFE,0xFF,0x00,0x3C,0xFE,0xFF,0x00,0x3C,0xFE,0x7F,0x00,0x3C,0xFE,0x7F,0x00,0x3C,0xFE,0x7F,0x00,0x3C,0xFE,0x3F,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,
};
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */


  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_RTC_Init();
  MX_ADC1_Init();
  MX_I2C1_Init();
  MX_I2C2_Init();
  MX_SPI1_Init();
  MX_SPI2_Init();
  MX_USART1_UART_Init();
  MX_USB_PCD_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

        u8g2Init(&u8g2);
        SHT3xObjectType sht;
        sht3x_init(&sht ,0x44,hi2c1);
	//u8g2_FirstPage(&u8g2);

        SPI_Flash_Test();


        static soft_i2c_t swi2c3_bus = {
                .SCL_Port = GPIOB,
                .SCL_Pin  = GPIO_PIN_3,
                .SDA_Port = GPIOB,
                .SDA_Pin  = GPIO_PIN_4,
                .Interval = 6,
        };
        static struct i2c_cli m24c02 = {
                .bus = &swi2c3_bus,
                .drv = &swi2c_drv,
                .dev = AT24CXX_DEV,
                .ops = I2C_DEV_7BIT | I2C_REG_8BIT,
        };

        swi2c_drv.init(&swi2c3_bus);
        i2cdrv_detector(&swi2c3_bus, m24c02.drv);
//     // 存储结构

#define ADDR 0x00

    object_t obj;

    obj.i[0] = 0;
    obj.i[1] = 0;
    obj.f    = 0;
    int i = 0;
at24cxx_read_variable(m24c02,ADDR, obj);
                // soft_i2c_receive(m24c02.bus, m24c02.dev, 
                //                         &obj, sizeof(obj), m24c02.ops);

        // soft_i2c_read_mem(m24c02.bus, m24c02.dev,ADDR, 
        //                                 (uint8_t*)&obj, sizeof(obj), m24c02.ops);        
  //  ReadBytesFromAT24CXX(&m24cxx,ADDR,(uint8_t*)&obj,sizeof(obj));

    println("i[0]=%d,i[1]=%d,f=%f", obj.i[0], obj.i[1], obj.f);
println("i[0]=%d", i);

    obj.i[0] = 2568;  // 
    obj.i[1] = -888;
    obj.f    = 6.289;
   i = 6;
//     WriteBytesToAT24CXX(&m24cxx,ADDR,(uint8_t*)&i,sizeof(i));
        // soft_i2c_write_mem(m24c02.bus, m24c02.dev,ADDR, 
        //                                 (uint8_t*)&obj, sizeof(obj), m24c02.ops);
        at24cxx_write_variable(m24c02,ADDR, obj);
    obj.i[0] = 0;  // 
    obj.i[1] = 0;
    obj.f    = 0;
       i = 0;
       at24cxx_read_variable(m24c02,ADDR, obj);
        // soft_i2c_read_mem(m24c02.bus, m24c02.dev,ADDR, 
        //                                (uint8_t*) &obj, sizeof(obj), m24c02.ops);        
//     ReadBytesFromAT24CXX(&m24cxx,ADDR,(uint8_t*)&obj,sizeof(obj));

    println("1111111111 i[0]=%d,i[1]=%d,f=%f", obj.i[0], obj.i[1], obj.f);

	MPU6050ReadID();

        // println("i[0]=%d", mpu_dmp_init());
// #define LEN 32

//     uint8_t buff[LEN] = {0};

//     println(SEPARATOR60);

// #define OFFSET 0x3FF

//     // write

//     println("write eeprom - head");

//     for (uint8_t i = 0; i < LEN; ++i) buff[i] = i * 2;
//     println("[%d...%d]", buff[0], buff[LEN - 1]);

//     if (!at24cxx_write(OFFSET, buff, LEN)) {
//         println("fail to write at24cxx [head]");
//         return 0;
//     }

//     println("write eeprom - tail");

//     for (uint8_t i = 0; i < LEN; ++i) buff[i] = i * 3;
//     println("[%d...%d]", buff[0], buff[LEN - 1]);

//     if (!at24cxx_write(AT24CXX_MAX_ADDR - LEN - OFFSET, buff, LEN)) {
//         println("fail to write at24cxx [tail]");
//         return 0;
//     }

// #if 0
//     // 总字�?????32678, 每次�?????8字节, 写完后大约等10s, 全写大约40s/�????? (32678/8*10ms)
//     for (uint16_t i = 0; i < AT24CXX_MAX_SIZE; i += LEN)
//         if (!at24cxx_write(i, buff, LEN)) {
//             println("fail to write at24cxx [%d]", i);
//             return 0;
//         }
// #endif

//     // read

//     println("read eeprom - head");

//     if (!at24cxx_print(OFFSET, LEN, LEN)) return 0;

//     println("read eeprom - tail");

//     if (!at24cxx_print(AT24CXX_MAX_ADDR - LEN - OFFSET, LEN, LEN)) return 0;
HAL_ADCEx_Calibration_Start(&hadc1);
HAL_ADC_Start_DMA(&hadc1,(uint32_t*)&ADC_Value, 100);
    uint8_t ret;
    /* 初始化ATK-MS6050 */
    ret = atk_ms6050_init();
    if (ret != 0)
    {
        printf("ATK-MS6050 init failed!\r\n");
        // while (1)
        // {
        //     LED0_TOGGLE();
        //     delay_ms(200);
        // }
    }
     printf("ATK-MS6050 1111111222222222222!\r\n");
    /* 初始化ATK-MS6050 DMP */
    ret = atk_ms6050_dmp_init();
    printf("[ line: %d | function: %s ] ret = %d" "\r\n", __LINE__, __FUNCTION__, ret);
    if (ret != 0)
    {
        printf("ATK-MS6050 DMP init failed!\r\n");
        // while (1)
        // {
        //     LED0_TOGGLE();
        //     delay_ms(200);
        // }
    }
      delay_ms(1000);
	u8g2_DrawLine(&u8g2, 0,0, 127, 63);//测试：起点：横坐标，纵坐标；终点：横坐标，纵坐标
	u8g2_DrawLine(&u8g2, 127,0, 0, 63);
	u8g2_SendBuffer(&u8g2);
	delay_ms(1000);
	u8g2_ClearBuffer(&u8g2);

    delay_ms(1000);
    u8g2_ClearBuffer(&u8g2);
    u8g2_DrawXBMP(&u8g2, 30, 20, 25, 25, u8g_logo_bits);//显示点赞图案
    u8g2_SendBuffer(&u8g2);


  u8g2_SetFont(&u8g2, u8g2_font_t0_22_mf);//设置字体	
  frame_len = frame_len_trg = list[ui_select].len*13;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1){
           //     printf("hello! 这是printf输出！\n");
loop1();
/*
                        demo_run();
                        //delay_ms(200);
// dmp_getdata();
// MPU6050_Read_Temp();
	  	// 	printf("pitch:%.2f\r\n",My_Mpu6050.pitch);
		// printf("roll:%.2f\r\n",My_Mpu6050.roll);
		// printf("yaw:%.2f\r\n",My_Mpu6050.yaw);
		// printf("temperature:%.2f\r\n",My_Mpu6050.temperature);
		// HAL_Delay(10);
		for(i = 0,adc =0; i < 100;)
		{
			adc += ADC_Value[i++];
		}
		adc /= 100;

    ADC_Vol =(float) adc/4096*3.3;
    HAL_Delay(1);		

    printf("\r\n %f \r\n",ADC_Vol);
		printf("\r\n The adc value is %f \r\n",ADC_Vol);
		HAL_Delay(300);

                if (!SHT3x_Read(&sht))
                        printf("%4f, %4f\n", sht.temp, sht.rh);
                draw(&u8g2);
                HAL_Delay(500);
      
*/
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
/*
 		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0,GPIO_PIN_RESET );
 		HAL_Delay(500);
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0,GPIO_PIN_SET );
 		HAL_Delay(500);
*/
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_ADC
                              |RCC_PERIPHCLK_USB;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
