#include "w25qxx.h"
#include "FreeRTOS.h"
#include "task.h"

static void w25qxx_poll_delay(void)
{
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
                vTaskDelay(pdMS_TO_TICKS(1));
        }
}

static uint8_t w25qxx_wait_ready(uint32_t timeout_ms)
{
        const uint32_t tickstart = HAL_GetTick();
        uint8_t status;

        do {
                status = w25qxx_getstatus();
                if (status == W25Qx_OK)
                        return W25Qx_OK;
                if (status != W25Qx_BUSY)
                        return status;
                if ((uint32_t)(HAL_GetTick() - tickstart) > timeout_ms)
                        return W25Qx_TIMEOUT;
                w25qxx_poll_delay();
        } while (1);
}

HAL_StatusTypeDef w25qxx_transmit(uint8_t* data,uint16_t len){
        return HAL_SPI_Transmit(
                &hspi1, data, len, W25Qx_TIMEOUT_VALUE);
}

HAL_StatusTypeDef w25qxx_receive(uint8_t* buff,uint16_t len){

        return HAL_SPI_Receive(
                &hspi1, buff, len, W25Qx_TIMEOUT_VALUE);
}


/**
 * @brief  This function reset the W25Qx.
 * @retval None
 */
void w25qxx_reset(){
        uint8_t cmd[2] = {RESET_ENABLE_CMD, RESET_MEMORY_CMD};

        w25qxx_enable();
        /* Send the reset command */
        w25qxx_transmit(cmd,2);   
        w25qxx_disable();
}

/**
 * @brief  Reads current status of the W25QXX.
 * @retval W25QXX memory status
 */
uint8_t w25qxx_getstatus(){
        uint8_t cmd[] = {READ_STATUS_REG1_CMD};
        uint8_t status = 0U;
        uint8_t ret;

        w25qxx_enable();
        /* Send the read status command */
        ret = (w25qxx_transmit(cmd,1) == HAL_OK) ? W25Qx_OK : W25Qx_ERROR;
        /* Reception of the data */
        if (ret == W25Qx_OK && w25qxx_receive(&status,1) != HAL_OK)
                ret = W25Qx_ERROR;
        w25qxx_disable();

        if (ret != W25Qx_OK)
                return ret;

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
uint8_t w25qxx_write_enable(){
        uint8_t  cmd[]     = {WRITE_ENABLE_CMD};

        /*Select the FLASH: Chip Select low */
        w25qxx_enable();
        /* Send the read ID command */
        if (w25qxx_transmit(cmd,1) != HAL_OK) {
                w25qxx_disable();
                return W25Qx_ERROR;
        }
        /*Deselect the FLASH: Chip Select high */
        w25qxx_disable();

        return w25qxx_wait_ready(W25Qx_TIMEOUT_VALUE);
}

/**
 * @brief  Read Manufacture/Device ID.
 * @param  return value address
 * @retval None
 */
void w25qxx_read_id(uint8_t* ID){

        uint8_t cmd[4] = {READ_ID_CMD, 0x00, 0x00, 0x00};

        w25qxx_enable();
        /* Send the read ID command */
        w25qxx_transmit(cmd,4);
        /* Reception of the data */
        w25qxx_receive(ID,2);

        w25qxx_disable();
}

/**
 * @brief  Read JEDEC ID (9Fh): 3 bytes = Manufacturer, Memory Type, Capacity. W25Q16JV returns EFh, 40h, 15h (Device ID 4015h).
 */
void w25qxx_read_jedec_id(uint8_t *ID)
{
	uint8_t cmd = READ_JEDEC_ID_CMD;
	w25qxx_enable();
	w25qxx_transmit(&cmd, 1);
	w25qxx_receive(ID, 3);
	w25qxx_disable();
}

/**
 * @brief  Reads an amount of data from the QSPI memory.
 * @param  pData: Pointer to data to be read
 * @param  ReadAddr: Read start address
 * @param  Size: Size of data to read
 * @retval QSPI memory status
 */
uint8_t w25qxx_read(uint8_t* pData, uint32_t ReadAddr, uint32_t Size){

        uint8_t cmd[4];

        /* Configure the command */
        cmd[0] = READ_CMD;
        cmd[1] = (uint8_t)(ReadAddr >> 16);
        cmd[2] = (uint8_t)(ReadAddr >> 8);
        cmd[3] = (uint8_t)(ReadAddr);

        w25qxx_enable();
        /* Send the read ID command */
        w25qxx_transmit(cmd,4);
        /* Reception of the data */
        if (w25qxx_receive(pData, Size) != HAL_OK) 
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
uint8_t w25qxx_write(uint8_t* pData, uint32_t WriteAddr, uint32_t Size)
{
        uint8_t  cmd[4];
        uint32_t end_addr, current_size, current_addr;
        uint8_t status;

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
                status = w25qxx_write_enable();
                if (status != W25Qx_OK)
                        return status;

                w25qxx_enable();
                /* Send the command */
                if (w25qxx_transmit( cmd, 4) != HAL_OK) {
                        w25qxx_disable();
                        return W25Qx_ERROR;
                }
                

                /* Transmission of the data */
                if (w25qxx_transmit( pData, current_size) != HAL_OK) {
                        w25qxx_disable();
                        return W25Qx_ERROR;
                }
                
                w25qxx_disable();
                status = w25qxx_wait_ready(W25Qx_TIMEOUT_VALUE);
                if (status != W25Qx_OK)
                        return status;

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
uint8_t w25qxx_erase_block(uint32_t Address)
{
    uint8_t  cmd[4];
    uint8_t status;
    cmd[0]             = SECTOR_ERASE_CMD;
    cmd[1]             = (uint8_t)(Address >> 16);
    cmd[2]             = (uint8_t)(Address >> 8);
    cmd[3]             = (uint8_t)(Address);

    /* Enable write operations */
    status = w25qxx_write_enable();
    if (status != W25Qx_OK)
        return status;

    /*Select the FLASH: Chip Select low */
    w25qxx_enable();
    /* Send the read ID command */
    if (w25qxx_transmit(cmd, 4) != HAL_OK) {
        w25qxx_disable();
        return W25Qx_ERROR;
    }
    /*Deselect the FLASH: Chip Select high */
    w25qxx_disable();

    return w25qxx_wait_ready(W25QXX_SECTOR_ERASE_MAX_TIME);
}

/**
 * @brief  Erases the entire QSPI memory.This function will take a very long time.
 * @retval QSPI memory status
 */
uint8_t w25qxx_erase_chip()
{
    uint8_t  cmd[4];
    uint8_t status;
    cmd[0]             = CHIP_ERASE_CMD;

    /* Enable write operations */
    status = w25qxx_write_enable();
    if (status != W25Qx_OK)
        return status;

    /*Select the FLASH: Chip Select low */
    w25qxx_enable();
    /* Send the read ID command */
    if (w25qxx_transmit(cmd, 1) != HAL_OK) {
        w25qxx_disable();
        return W25Qx_ERROR;
    }
    /*Deselect the FLASH: Chip Select high */
    w25qxx_disable();
    return w25qxx_wait_ready(W25QXX_BULK_ERASE_MAX_TIME);
}
