#include "sht3x.h"

HAL_StatusTypeDef sht3x_writcmd(SHT3xObjectType *sht,uint16_t cmd){
        uint8_t buff[2] = {cmd >> 8, cmd & 0xFF};
        return HAL_I2C_Master_Transmit(
                &sht->i2c,sht->addr,buff,2, 0XFFFF);
}

HAL_StatusTypeDef sht3x_receive(SHT3xObjectType *sht,uint8_t* buff,uint16_t len){
        return HAL_I2C_Master_Receive(
                &sht->i2c,sht->addr | 1,buff,len,0xFFFF);
}


SHT3xErrorType sht3x_get_sensor_value(SHT3xObjectType *sht)
{
        uint16_t data = 0;
        uint8_t  buff[6],crc8;

        sht3x_writcmd(sht,READOUT_FOR_PERIODIC_MODE);
        // 6个值：温度(高八位,低八位,CRC位), 湿度(高八位,低八位,CRC位)
        sht3x_receive(sht,buff,sizeof(buff));

        crc8 = cal_crc_table(buff,2);
        if(crc8 != buff[2]) 
                return SHT3X_PARM_ERROR;

        data = (buff[0] << 8) | buff[1];
        sht->temp = -45 + 175 * ((float)data/65535);
        
        crc8 = cal_crc_table(buff+3,2);
        if(crc8 != buff[5]) 
                return SHT3X_PARM_ERROR;

        data = (buff[3] << 8) | buff[4];
        sht->rh = 100 * ((float)data/65535);

        sht3x_log("succeed! temp:%4f, ht:%4f\r\n", sht->temp, sht->rh);
        return SHT3X_NO_ERROR;
}


static SHT3xErrorType sht3x_read_status_reg(SHT3xObjectType *sht ){

        uint8_t buff[3],crc8;

        sht3x_writcmd(sht,READ_STATUS_CMD);
        sht3x_receive (sht,buff,sizeof(buff));

        crc8 = cal_crc_table(buff,2);
        if(crc8 != buff[2]) 
                return SHT3X_PARM_ERROR;

        sht->status = ((uint16_t)buff[0] << 8) | buff[1];

        sht3x_log("succeed! status:%#x\r\n",sht->status);
        return SHT3X_NO_ERROR;
}




/* SHT3x initialization */
SHT3xErrorType sht3x_init(SHT3xObjectType *sht,uint8_t address,I2C_HandleTypeDef hi2c){

        SHT3xErrorType error = SHT3X_NO_ERROR;

        if(sht == NULL)
                return SHT3X_PARM_ERROR;
  

        sht->temp= 0;
        sht->rh  = 0;
        sht->addr= 0;

        if((address == 0x44) || (address == 0x45))
                sht->addr = address << 1;
        if((address == 0x88) || (address == 0x8A))
                sht->addr = address;
        if(!sht->addr)
                return SHT3X_PARM_ERROR;
  
        sht->i2c = hi2c;
        sht3x_writcmd(sht,SOFT_RESET_CMD);
        HAL_Delay(20);
        sht3x_writcmd(sht,PERI_MEDIUM_2_CMD);

        error |= sht3x_read_status_reg(sht);
        return error;

}
