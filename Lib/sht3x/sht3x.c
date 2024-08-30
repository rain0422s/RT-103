#include "sht3x.h"

HAL_StatusTypeDef SHT3x_WriteCmd(uint16_t cmd)
{
    uint8_t buff[2] = {cmd >> 8, cmd & 0xFF};
    return HAL_I2C_Master_Transmit(&SHT3X_I2C, SHT3X_DEV, buff, 2, 0XFFFF);
}

HAL_StatusTypeDef SHT3x_ReadRaw(uint8_t* buff)
{
    SHT3x_WriteCmd(READOUT_FOR_PERIODIC_MODE);
    return HAL_I2C_Master_Receive(&SHT3X_I2C, SHT3X_DEV | 1, buff, 6, 0xFFFF);
    // 6个值：温度(高八位,低八位,CRC位), 湿度(高八位,低八位,CRC位)
}




uint8_t SHT3x_Read(SHT3xObjectType *sht)
{
    uint16_t data = 0;
    uint8_t  buff[6];
    SHT3x_ReadRaw(buff);
    if (CalTableCrc8(buff, 2, buff[2])) return 1;
    data         = ((uint16_t)buff[0] << 8) | buff[1];
    sht->temperature = -45 + 175 * ((float)data / 65535);
    if (CalTableCrc8(buff + 3, 2, buff[5])) return 2;
    data      = ((uint16_t)buff[3] << 8) | buff[4];
    sht->humidity = 100 * ((float)data / 65535);
    SHT3X_log("temperature = %4f, humidity = %4f\r\n", sht->temperature, sht->humidity);
    return 0;
}


static uint8_t SHT3xReadStatusRegister(SHT3xObjectType *sht )
{

    uint8_t  buff[3];
    SHT3x_WriteCmd(READ_STATUS_CMD);
    HAL_I2C_Master_Receive(&SHT3X_I2C, sht->devAddress | 1, buff, 3, 0xFFFF);
    if (CalTableCrc8(buff, 2, buff[2])) 
      return 1;
    sht->status  = ((uint16_t)buff[0] << 8) | buff[1];
    SHT3X_log("status = %#x\r\n",sht->status);
    return 0;
}




/* SHT3x initialization */
SHT3xErrorType SHT3xInitialization(SHT3xObjectType *sht , uint8_t address){
  SHT3xErrorType error=SHT3X_NO_ERROR;
  if((sht==NULL))
  {
      return SHT3X_PARM_ERROR;
  }

  sht->temperature=0.0;
  sht->humidity=0.0;

  if((address==0x44)||(address==0x45))
  {
    sht->devAddress=(address<<1);
  }
  else if((address==0x88)||(address==0x8A)) {
    sht->devAddress=address;
  }
  else{
    sht->devAddress=0;
    error|=SHT3X_PARM_ERROR;
  }

    SHT3x_WriteCmd(SOFT_RESET_CMD);
    HAL_Delay(20);
    SHT3x_WriteCmd(PERI_MEDIUM_2_CMD);

  error|= SHT3xReadStatusRegister(sht);

  return error;
}
