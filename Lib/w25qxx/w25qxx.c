#include "w25qxx.h"

HAL_StatusTypeDef w25qxx_transmit(W25QxObjectType *w25qx,uint16_t data,uint16_t len){
        return HAL_SPI_Transmit(
                &w25qx->spi, data, len, w25qx->timeout);
}

HAL_StatusTypeDef w25qxx_receive(W25QxObjectType *w25qx,uint8_t* buff,uint16_t len){

        return HAL_SPI_Receive(
                &w25qx->spi, buff, len, w25qx->timeout);
}
/**
 * @brief
 *
 *
 *@retval None*/
uint8_t w25qxx_init(W25QxObjectType *w25qx,SPI_HandleTypeDef spi,uint32_t timeout){

        w25qx->spi     = spi;         
        w25qx->timeout = timeout;

        /* Reset W25Qxxx */
        w25qxx_reset(w25qx);

        return w25qxx_getstatus(w25qx);
}

/**
 * @brief  This function reset the W25Qx.
 * @retval None
 */
static void w25qxx_reset(W25QxObjectType *w25qx){
        uint8_t cmd[2] = {RESET_ENABLE_CMD, RESET_MEMORY_CMD};

        w25qxx_enable();
        /* Send the reset command */
        w25qxx_transmit(w25qx,cmd,2);   
        w25qxx_disable();
}

/**
 * @brief  Reads current status of the W25QXX.
 * @retval W25QXX memory status
 */
static uint8_t w25qxx_getstatus(W25QxObjectType *w25qx){
        uint8_t cmd[] = {READ_STATUS_REG1_CMD};
        uint8_t status;

        w25qxx_enable();
        /* Send the read status command */
        w25qxx_transmit(w25qx,cmd,1);                 
        /* Reception of the data */
        w25qxx_receive(w25qx,&status,1);
        w25qxx_disable();

        /* Check the value of the register */
        if ((status & W25QXX_FSR_BUSY) != 0)
                return W25Qx_BUSY;
        else 
                return W25Qx_OK;
    
}

/**
 * @brief  This function send a Write Enable and wait it is effective.
 * @retval None
 */
uint8_t w25qxx_write_enable(W25QxObjectType *w25qx){
        uint8_t  cmd[]     = {WRITE_ENABLE_CMD};
        uint32_t tickstart = HAL_GetTick();

        /*Select the FLASH: Chip Select low */
        w25qxx_enable();
        /* Send the read ID command */
        w25qxx_transmit(w25qx,cmd,1);
        /*Deselect the FLASH: Chip Select high */
        w25qxx_disable();

        /* Wait the end of Flash writing */
        while (w25qxx_getstatus(w25qx) == W25Qx_BUSY)
                ;
        {
                /* Check for the Timeout */
                if ((HAL_GetTick() - tickstart) > W25Qx_TIMEOUT_VALUE) {
                        return W25Qx_TIMEOUT;
                }
        }

        return W25Qx_OK;
}

/**
 * @brief  Read Manufacture/Device ID.
 * @param  return value address
 * @retval None
 */
void w25qxx_read_id(W25QxObjectType *w25qx,uint8_t* ID){

        uint8_t cmd[4] = {READ_ID_CMD, 0x00, 0x00, 0x00};

        w25qxx_enable();
        /* Send the read ID command */
        w25qxx_transmit(w25qx,cmd,4);
        /* Reception of the data */
        w25qxx_receive(w25qx,ID,2);

        w25qxx_disable();
}

/**
 * @brief  Reads an amount of data from the QSPI memory.
 * @param  pData: Pointer to data to be read
 * @param  ReadAddr: Read start address
 * @param  Size: Size of data to read
 * @retval QSPI memory status
 */
uint8_t w25qxx_read(W25QxObjectType *w25qx,uint8_t* pData, uint32_t ReadAddr, uint32_t Size){

        uint8_t cmd[4];

        /* Configure the command */
        cmd[0] = READ_CMD;
        cmd[1] = (uint8_t)(ReadAddr >> 16);
        cmd[2] = (uint8_t)(ReadAddr >> 8);
        cmd[3] = (uint8_t)(ReadAddr);

        w25qxx_enable();
        /* Send the read ID command */
        w25qxx_transmit(w25qx,cmd,4);
        /* Reception of the data */
        if (w25qxx_receive(w25qx, pData, Size) != HAL_OK) 
        return W25Qx_ERROR;

        w25qxx_disable();
        return W25Qx_OK;
}

/**
 * @brief  Writes an amount of data to the QSPI memory.
 * @param  pData: Pointer to data to be written
 * @param  WriteAddr: Write start address
 * @param  Size: Size of data to write,No more than 256byte.
 * @retval QSPI memory status
 */
uint8_t w25qxx_write(W25QxObjectType *w25qx,uint8_t* pData, uint32_t WriteAddr, uint32_t Size)
{
        uint8_t  cmd[4];
        uint32_t end_addr, current_size, current_addr;
        uint32_t tickstart = HAL_GetTick();

        /* Calculation of the size between the write address and the end of the page */
        current_addr = 0;

        while (current_addr <= WriteAddr) {
        current_addr += W25QXX_PAGE_SIZE;
        }
        current_size = current_addr - WriteAddr;

        /* Check if the size of the data is less than the remaining place in the page */
        if (current_size > Size) {
        current_size = Size;
        }

        /* Initialize the adress variables */
        current_addr = WriteAddr;
        end_addr     = WriteAddr + Size;

        /* Perform the write page by page */
        do {
                /* Configure the command */
                cmd[0] = PAGE_PROG_CMD;
                cmd[1] = (uint8_t)(current_addr >> 16);
                cmd[2] = (uint8_t)(current_addr >> 8);
                cmd[3] = (uint8_t)(current_addr);

                /* Enable write operations */
                w25qxx_write_enable(w25qx);

                w25qxx_enable();
                /* Send the command */
                if (w25qxx_transmit(w25qx, cmd, 4) != HAL_OK)
                        return W25Qx_ERROR;
                

                /* Transmission of the data */
                if (w25qxx_transmit(w25qx, pData, current_size) != HAL_OK)
                        return W25Qx_ERROR;
                
                w25qxx_disable();
                /* Wait the end of Flash writing */
                while (w25qxx_getstatus(w25qx) == W25Qx_BUSY)
                        ;
                {
                        /* Check for the Timeout */
                        if ((HAL_GetTick() - tickstart) > W25Qx_TIMEOUT_VALUE)
                                return W25Qx_TIMEOUT;
                        
                }

                /* Update the address and size variables for next page programming */
                current_addr += current_size;
                pData += current_size;
                current_size = ((current_addr + W25QXX_PAGE_SIZE) > end_addr) ? (end_addr - current_addr) : W25QXX_PAGE_SIZE;
        } while (current_addr < end_addr);

        return W25Qx_OK;
}

/**
 * @brief  Erases the specified block of the QSPI memory.
 * @param  BlockAddress: Block address to erase
 * @retval QSPI memory status
 */
uint8_t w25qxx_erase_block(W25QxObjectType *w25qx,uint32_t Address)
{
    uint8_t  cmd[4];
    uint32_t tickstart = HAL_GetTick();
    cmd[0]             = SECTOR_ERASE_CMD;
    cmd[1]             = (uint8_t)(Address >> 16);
    cmd[2]             = (uint8_t)(Address >> 8);
    cmd[3]             = (uint8_t)(Address);

    /* Enable write operations */
    w25qxx_write_enable(w25qx);

    /*Select the FLASH: Chip Select low */
    w25qxx_enable();
    /* Send the read ID command */
    w25qxx_transmit(w25qx, cmd, 4);
    /*Deselect the FLASH: Chip Select high */
    w25qxx_disable();

    /* Wait the end of Flash writing */
    while (w25qxx_getstatus(w25qx) == W25Qx_BUSY)
        ;
    {
        /* Check for the Timeout */
        if ((HAL_GetTick() - tickstart) > W25QXX_SECTOR_ERASE_MAX_TIME) {
            return W25Qx_TIMEOUT;
        }
    }
    return W25Qx_OK;
}

/**
 * @brief  Erases the entire QSPI memory.This function will take a very long time.
 * @retval QSPI memory status
 */
uint8_t w25qxx_erase_chip(W25QxObjectType *w25qx)
{
    uint8_t  cmd[4];
    uint32_t tickstart = HAL_GetTick();
    cmd[0]             = SECTOR_ERASE_CMD;

    /* Enable write operations */
    w25qxx_write_enable(w25qx);

    /*Select the FLASH: Chip Select low */
    w25qxx_enable();
    /* Send the read ID command */
    w25qxx_transmit(w25qx, cmd, 1);
    /*Deselect the FLASH: Chip Select high */
    w25qxx_disable();

    /* Wait the end of Flash writing */
    while (w25qxx_getstatus(w25qx) != W25Qx_BUSY)
        ;
    {
        /* Check for the Timeout */
        if ((HAL_GetTick() - tickstart) > W25QXX_BULK_ERASE_MAX_TIME) {
            return W25Qx_TIMEOUT;
        }
    }
    return W25Qx_OK;
}
