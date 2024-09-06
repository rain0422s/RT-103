#include "storage.h"


void spi_flash_test(W25QxObjectType *w25qx)
{
        uint8_t  ID[4];
        uint8_t  i;
        uint8_t  wData[0x100];
        uint8_t  rData[0x100];
	printf("SPI-W25Qxxx Example \n");

        /*1- Read the device ID */
        w25qxx_init(w25qx,hspi1,W25Qx_TIMEOUT_VALUE);

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
        if (memcmp(wData, rData, 0x100) == 0){
                printf(" W25Q64FV SPI Test OK\n");
        }
        else{
                printf(" W25Q64FV SPI Test False\n");
        }

        return ;
} 


void i2c_eeprom_init(struct i2c_cli at24cxx ){

        static soft_i2c_t swi2c3_bus = {
                .SCL_Port = GPIOB,
                .SCL_Pin  = GPIO_PIN_3,
                .SDA_Port = GPIOB,
                .SDA_Pin  = GPIO_PIN_4,
                .Interval = 6,
        };


        at24cxx.bus = &swi2c3_bus,
        at24cxx.drv = &swi2c_drv,
        at24cxx.dev = AT24CXX_DEV,
        at24cxx.ops = I2C_DEV_7BIT | I2C_REG_8BIT,

        swi2c_drv.init(&swi2c3_bus);
        
        i2cdrv_detector(&swi2c3_bus, at24cxx.drv);

}


void i2c_eeprom_test(struct i2c_cli at24cxx){
        int i = 0;
        object_t obj;


        i2c_eeprom_init(at24cxx);



        obj.i[0] = 0;
        obj.i[1] = 0;
        obj.f    = 0;

        at24cxx_read_variable(at24cxx , 0x00 , obj);


        println("i[0]=%d,i[1]=%d,f=%f", obj.i[0], obj.i[1], obj.f);
        println("i[0]=%d", i);

        obj.i[0] = 2568; 
        obj.i[1] = -888;
        obj.f    = 6.289;
        i = 6;

        at24cxx_write_variable(at24cxx,0x00, obj);
        obj.i[0] = 0;  
        obj.i[1] = 0;
        obj.f    = 0;
        i = 0;
        at24cxx_read_variable(at24cxx,0x00, obj);


        println("i[0]=%d,i[1]=%d,f=%f", obj.i[0], obj.i[1], obj.f);

}