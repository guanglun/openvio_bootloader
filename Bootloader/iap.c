#include "iap.h"

extern USBD_HandleTypeDef hUsbDeviceHS;

int isReadUpgrade = 0;
int jump_app_count = JUMP_APP_DELAY;

void boot(void)
{
    while (jump_app_count--)
    {
        HAL_Delay(1);
    }

    printf("start jump to app ...\r\n");
    //HAL_Delay(1000);

    if(!iap_jump_to(FLASH_APP_START_ADDRESS))
    {
        printf("jump fail, loop\r\n");
    }
}

static void system_jump_to(uint32_t ApplicationAddress)
{
    uint32_t JumpAddress;
    __disable_irq();  
    
    JumpAddress = *(uint32_t *)(ApplicationAddress + 4);
    /* Initialize user application's Stack Pointer */
    __set_MSP(*(uint32_t *)ApplicationAddress);

    (*((void (*)(void))JumpAddress))();
}

int iap_jump_to(uint32_t address)
{
    if (((*(uint32_t *)address) & 0x20000000) == 0x20000000)
    {
        __HAL_UART_DISABLE_IT(&huart2, UART_IT_IDLE); 
        HAL_UART_MspDeInit(&huart2);

        USBD_DeInit(&hUsbDeviceHS);

        system_jump_to(address);
        return 1;
    }
    return 0;
}