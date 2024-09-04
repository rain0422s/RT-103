#include "storage.h"

// #define W25Q16 	0XEF14
// #define	SPI_FLASH_CS_0    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_0,GPIO_PIN_RESET)
// #define      SPI_FLASH_CS_1    HAL_GPIO_WritePin(GPIOB,GPIO_PIN_0,GPIO_PIN_SET)

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

void SPI_Flash_Test(W25QxObjectType *w25qx,uint8_t *ID)
{
        uint8_t  i;
        uint8_t  wData[0x100];
        uint8_t  rData[0x100];
	printf("SPI-W25Qxxx Example \n");

        /*1- Read the device ID */
        w25qxx_Init(w25qx,hspi1,W25Qx_TIMEOUT_VALUE);
        w25qxx_read_id(w25qx,ID);
        printf("W25Qxxx ID is : ");
        for (i = 0; i < 2; i++) {
                printf("0x%02X ", ID[i]);
        }
        printf("\n");

    /* 2- Erase */
    if (w25qxx_erase_block(w25qx,0) == W25Qx_OK)
        printf(" SPI Erase Block ok\n");
    else
        Error_Handler();

    /*-2- Written to the flash */
    /* fill buffer */
    for (i = 0; i < 0x100; i++) {
        wData[i] = i;
        rData[i] = 0;
    }

    if (w25qxx_write(w25qx,wData, 0x00, 0x100) == W25Qx_OK)
        printf(" SPI Write ok\n");
    else
        Error_Handler();

    /* 3- Read the flash */
    if (w25qxx_read(w25qx,rData, 0x00, 0x100) == W25Qx_OK)
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


void at24_test(void){
        int i = 0;
        object_t obj;
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


        obj.i[0] = 0;
        obj.i[1] = 0;
        obj.f    = 0;

        at24cxx_read_variable(m24c02 , 0x00 , obj);


        println("i[0]=%d,i[1]=%d,f=%f", obj.i[0], obj.i[1], obj.f);
        println("i[0]=%d", i);

        obj.i[0] = 2568;  // 
        obj.i[1] = -888;
        obj.f    = 6.289;
        i = 6;

        at24cxx_write_variable(m24c02,0x00, obj);
        obj.i[0] = 0;  // 
        obj.i[1] = 0;
        obj.f    = 0;
        i = 0;
        at24cxx_read_variable(m24c02,0x00, obj);


        println("1111111111 i[0]=%d,i[1]=%d,f=%f", obj.i[0], obj.i[1], obj.f);

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
}