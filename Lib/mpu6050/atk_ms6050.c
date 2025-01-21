/**
 ****************************************************************************************************
 * @file        atk_ms6050.c
 * @author      ����ԭ���Ŷ�(ALIENTEK)
 * @version     V1.0
 * @date        2022-06-21
 * @brief       ATK-MS6050ģ����������
 * @license     Copyright (c) 2020-2032, �������������ӿƼ����޹�˾
 ****************************************************************************************************
 * @attention
 *
 * ʵ��ƽ̨:����ԭ�� STM32F103������
 * ������Ƶ:www.yuanzige.com
 * ������̳:www.openedv.com
 * ��˾��ַ:www.alientek.com
 * �����ַ:openedv.taobao.com
 *
 ****************************************************************************************************
 */

#include "atk_ms6050.h"
// #include "./BSP/ATK_MS6050/atk_ms6050_iic.h"
// #include "./SYSTEM/usart/usart.h"
// #include "./SYSTEM/delay/delay.h"
#include "i2c.h"


/**
 * @brief       ��ATK-MS6050��ָ���Ĵ�������д��ָ������
 * @param       addr: ATK-MS6050��IICͨѶ��ַ
 *              reg : ATK-MS6050�Ĵ�����ַ
 *              len : д��ĳ���
 *              dat : д�������
 * @retval      ATK_MS6050_EOK : ����ִ�гɹ�
 *              ATK_MS6050_EACK: IICͨѶACK���󣬺���ִ��ʧ��
 */
uint8_t atk_ms6050_write(uint8_t addr,uint8_t reg, uint8_t len, uint8_t *dat)
{

        int ret=HAL_I2C_Mem_Write(&hi2c2, addr , reg, I2C_MEMADD_SIZE_8BIT, dat, len, 0XFFFF);
	if(ret==HAL_OK)return ATK_MS6050_EOK;
	else return ATK_MS6050_EACK;  
//     uint8_t i;
    
//     atk_ms6050_iic_start();
//     atk_ms6050_iic_send_byte((addr << 1) | 0);
//     if (atk_ms6050_iic_wait_ack() == 1)
//     {
//         atk_ms6050_iic_stop();
//         return ATK_MS6050_EACK;
//     }
//     atk_ms6050_iic_send_byte(reg);
//     if (atk_ms6050_iic_wait_ack() == 1)
//     {
//         atk_ms6050_iic_stop();
//         return ATK_MS6050_EACK;
//     }
//     for (i=0; i<len; i++)
//     {
//         atk_ms6050_iic_send_byte(dat[i]);
//         if (atk_ms6050_iic_wait_ack() == 1)
//         {
//             atk_ms6050_iic_stop();
//             return ATK_MS6050_EACK;
//         }
//     }
//     atk_ms6050_iic_stop();
//     return ATK_MS6050_EOK;
}

/**
 * @brief       ��ATK-MS6050��ָ���Ĵ���д��һ�ֽ�����
 * @param       addr: ATK-MS6050��IICͨѶ��ַ
 *              reg : ATK-MS6050�Ĵ�����ַ
 *              dat : д�������
 * @retval      ATK_MS6050_EOK : ����ִ�гɹ�
 *              ATK_MS6050_EACK: IICͨѶACK���󣬺���ִ��ʧ��
 */
uint8_t atk_ms6050_write_byte(uint8_t addr, uint8_t reg, uint8_t dat)
{
    return atk_ms6050_write(addr, reg, 1, &dat);

}

/**
 * @brief       ������ȡATK-MS6050ָ���Ĵ�����ֵ
 * @param       addr: ATK-MS6050��IICͨѶ��ַ
 *              reg : ATK-MS6050�Ĵ�����ַ
 *              len: ��ȡ�ĳ���
 *              dat: ��Ŷ�ȡ�������ݵĵ�ַ
 * @retval      ATK_MS6050_EOK : ����ִ�гɹ�
 *              ATK_MS6050_EACK: IICͨѶACK���󣬺���ִ��ʧ��
 */
uint8_t atk_ms6050_read(uint8_t addr, uint8_t reg, uint8_t len, uint8_t *dat)
{
        int ret= HAL_I2C_Mem_Read(&hi2c2, addr , reg, I2C_MEMADD_SIZE_8BIT, dat,  len, 0XFFFF);
	if(ret==HAL_OK)return ATK_MS6050_EOK;
	else return ATK_MS6050_EACK;  


//     atk_ms6050_iic_start();
//     atk_ms6050_iic_send_byte((addr << 1) | 0);
//     if (atk_ms6050_iic_wait_ack() == 1)
//     {
//         atk_ms6050_iic_stop();
//         return ATK_MS6050_EACK;
//     }
//     atk_ms6050_iic_send_byte(reg);
//     if (atk_ms6050_iic_wait_ack() == 1)
//     {
//         atk_ms6050_iic_stop();
//         return ATK_MS6050_EACK;
//     }
//     atk_ms6050_iic_start();
//     atk_ms6050_iic_send_byte((addr << 1) | 1);
//     if (atk_ms6050_iic_wait_ack() == 1)
//     {
//         atk_ms6050_iic_stop();
//         return ATK_MS6050_EACK;
//     }
//     while (len)
//     {
//         *dat = atk_ms6050_iic_read_byte((len > 1) ? 1 : 0);
//         len--;
//         dat++;
//     }
//     atk_ms6050_iic_stop();
//     return ATK_MS6050_EOK;
}

/**
 * @brief       ��ȡATK-MS6050ָ���Ĵ�����ֵ
 * @param       addr: ATK-MS6050��IICͨѶ��ַ
 *              reg : ATK-MS6050�Ĵ�����ַ
 *              dat: ��ȡ���ļĴ�����ֵ
 * @retval      ATK_MS6050_EOK : ����ִ�гɹ�
 *              ATK_MS6050_EACK: IICͨѶACK���󣬺���ִ��ʧ��
 */
uint8_t atk_ms6050_read_byte(uint8_t addr, uint8_t reg, uint8_t *dat)
{
    return atk_ms6050_read(addr, reg, 1, dat);

}

/**
 * @brief       ATK-MS6050������λ
 * @param       ��
 * @retval      ��
 */
void atk_ms6050_sw_reset(void)
{
    atk_ms6050_write_byte(ATK_MS6050_IIC_ADDR, MPU_PWR_MGMT1_REG, 0x80);
    delay_ms(100);
    atk_ms6050_write_byte(ATK_MS6050_IIC_ADDR, MPU_PWR_MGMT1_REG, 0x00);
}

/**
 * @brief       ATK-MS6050���������Ǵ��������̷�Χ
 * @param       frs: 0 --> ��250dps
 *                   1 --> ��500dps
 *                   2 --> ��1000dps
 *                   3 --> ��2000dps
 * @retval      ATK_MS6050_EOK : ����ִ�гɹ�
 *              ATK_MS6050_EACK: IICͨѶACK���󣬺���ִ��ʧ��
 */
uint8_t atk_ms6050_set_gyro_fsr(uint8_t fsr)
{
    return atk_ms6050_write_byte(ATK_MS6050_IIC_ADDR, MPU_GYRO_CFG_REG, fsr << 3);
}

/**
 * @brief       ATK-MS6050���ü��ٶȴ��������̷�Χ
 * @param       frs: 0 --> ��2g
 *                   1 --> ��4g
 *                   2 --> ��8g
 *                   3 --> ��16g
 * @retval      ATK_MS6050_EOK : ����ִ�гɹ�
 *              ATK_MS6050_EACK: IICͨѶACK���󣬺���ִ��ʧ��
 */
uint8_t atk_ms6050_set_accel_fsr(uint8_t fsr)
{
    return atk_ms6050_write_byte(ATK_MS6050_IIC_ADDR, MPU_ACCEL_CFG_REG, fsr << 3);
}

/**
 * @brief       ATK-MS6050�������ֵ�ͨ�˲���Ƶ��
 * @param       lpf: ���ֵ�ͨ�˲�����Ƶ�ʣ�Hz��
 * @retval      ATK_MS6050_EOK : ����ִ�гɹ�
 *              ATK_MS6050_EACK: IICͨѶACK���󣬺���ִ��ʧ��
 */
uint8_t atk_ms6050_set_lpf(uint16_t lpf)
{
    uint8_t dat;
    
    if (lpf >= 188)
    {
        dat = 1;
    }
    else if (lpf >= 98)
    {
        dat = 2;
    }
    else if (lpf >= 42)
    {
        dat = 3;
    }
    else if (lpf >= 20)
    {
        dat = 4;
    }
    else if (lpf >= 10)
    {
        dat = 5;
    }
    else
    {
        dat = 6;
    }
    
    return atk_ms6050_write_byte(ATK_MS6050_IIC_ADDR, MPU_CFG_REG, dat);
}

/**
 * @brief       ATK-MS6050���ò�����
 * @param       rate: �����ʣ�4~1000Hz��
 * @retval      ATK_MS6050_EOK : ����ִ�гɹ�
 *              ATK_MS6050_EACK: IICͨѶACK���󣬺���ִ��ʧ��
 */
uint8_t atk_ms6050_set_rate(uint16_t rate)
{
    uint8_t ret;
    uint8_t dat;
    
    if (rate > 1000)
    {
        rate = 1000;
    }
    
    if (rate < 4)
    {
        rate = 4;
    }
    
    dat = 1000 / rate - 1;
    ret = atk_ms6050_write_byte(ATK_MS6050_IIC_ADDR, MPU_SAMPLE_RATE_REG, dat);
    if (ret != ATK_MS6050_EOK)
    {
        return ret;
    }
    
    ret = atk_ms6050_set_lpf(rate >> 1);
    if (ret != ATK_MS6050_EOK)
    {
        return ret;
    }
    
    return ATK_MS6050_EOK;
}

/**
 * @brief       ATK-MS6050��ȡ�¶�ֵ
 * @param       temperature: ��ȡ�����¶�ֵ��������100����
 * @retval      ATK_MS6050_EOK : ����ִ�гɹ�
 *              ATK_MS6050_EACK: IICͨѶACK���󣬺���ִ��ʧ��
 */
uint8_t atk_ms6050_get_temperature(int16_t *temp)
{
    uint8_t dat[2];
    uint8_t ret;
    int16_t raw = 0;
    
    ret = atk_ms6050_read(ATK_MS6050_IIC_ADDR, MPU_TEMP_OUTH_REG, 2, dat);
    if (ret == ATK_MS6050_EOK)
    {
        raw = ((uint16_t)dat[0] << 8) | dat[1];
        *temp = (int16_t)((36.53f + ((float)raw / 340)) * 100);
    }
    
    return ret;
}

/**
 * @brief       ATK-MS6050��ȡ������ֵ
 * @param       gx��gy��gz: ������x��y��z���ԭʼ�����������ţ�
 * @retval      ATK_MS6050_EOK : ����ִ�гɹ�
 *              ATK_MS6050_EACK: IICͨѶACK���󣬺���ִ��ʧ��
 */
uint8_t atk_ms6050_get_gyroscope(int16_t *gx, int16_t *gy, int16_t *gz)
{
    uint8_t dat[6];
    uint8_t ret;
    
    ret =  atk_ms6050_read(ATK_MS6050_IIC_ADDR, MPU_GYRO_XOUTH_REG, 6, dat);
    if (ret == ATK_MS6050_EOK)
    {
        *gx = ((uint16_t)dat[0] << 8) | dat[1];
        *gy = ((uint16_t)dat[2] << 8) | dat[3];
        *gz = ((uint16_t)dat[4] << 8) | dat[5];
    }
    
    return ret;
}

/**
 * @brief       ATK-MS6050��ȡ���ٶ�ֵ
 * @param       ax��ay��az: ���ٶ�x��y��z���ԭʼ�����������ţ�
 * @retval      ATK_MS6050_EOK : ����ִ�гɹ�
 *              ATK_MS6050_EACK: IICͨѶACK���󣬺���ִ��ʧ��
 */
uint8_t atk_ms6050_get_accelerometer(int16_t *ax, int16_t *ay, int16_t *az)
{
    uint8_t dat[6];
    uint8_t ret;
    
    ret =  atk_ms6050_read(ATK_MS6050_IIC_ADDR, MPU_ACCEL_XOUTH_REG, 6, dat);
    if (ret == ATK_MS6050_EOK)
    {
        *ax = ((uint16_t)dat[0] << 8) | dat[1];
        *ay = ((uint16_t)dat[2] << 8) | dat[3];
        *az = ((uint16_t)dat[4] << 8) | dat[5];
    }
    
    return ret;
}

/**
 * @brief       ATK-MS6050��ʼ��
 * @param       ��
 * @retval      ATK_MS6050_EOK: ����ִ�гɹ�
 *              ATK_MS6050_EID: ��ȡID���󣬺���ִ��ʧ��
 */
uint8_t atk_ms6050_init(void)
{
    uint8_t id;
    
//     atk_ms6050_hw_init();                                                   /* ATK-MS6050Ӳ����ʼ�� */
//     atk_ms6050_iic_init();                                                  /* ��ʼ��IIC�ӿ� */
    atk_ms6050_sw_reset();                                                  /* ATK-MS050������λ */
    atk_ms6050_set_gyro_fsr(3);                                             /* �����Ǵ���������2000dps */
    atk_ms6050_set_accel_fsr(0);                                            /* ���ٶȴ���������2g */
    atk_ms6050_set_rate(50);                                                /* �����ʣ�50Hz */
    atk_ms6050_write_byte(ATK_MS6050_IIC_ADDR, MPU_INT_EN_REG, 0X00);       /* �ر������ж� */
    atk_ms6050_write_byte(ATK_MS6050_IIC_ADDR, MPU_USER_CTRL_REG, 0X00);    /* �ر�IIC��ģʽ */
    atk_ms6050_write_byte(ATK_MS6050_IIC_ADDR, MPU_FIFO_EN_REG, 0X00);      /* �ر�FIFO */
    atk_ms6050_write_byte(ATK_MS6050_IIC_ADDR, MPU_INTBP_CFG_REG, 0X80);    /* INT���ŵ͵�ƽ��Ч */
    atk_ms6050_read_byte(ATK_MS6050_IIC_ADDR, MPU_DEVICE_ID_REG, &id);      /* ��ȡ�豸ID */
    if (id != 0x68)
    {
        return ATK_MS6050_EID;
    }
    atk_ms6050_write_byte(ATK_MS6050_IIC_ADDR, MPU_PWR_MGMT1_REG, 0x01);    /* ����CLKSEL��PLL X��Ϊ�ο� */
    atk_ms6050_write_byte(ATK_MS6050_IIC_ADDR, MPU_PWR_MGMT2_REG, 0x00);    /* ���ٶ��������Ƕ����� */
    atk_ms6050_set_rate(50);                                                /* �����ʣ�50Hz */
    
    return ATK_MS6050_EOK;
}
