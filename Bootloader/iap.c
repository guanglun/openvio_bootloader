#include "iap.h"
#include "flash.h"
#include "string.h"
#include "uart_parse.h"

extern USBD_HandleTypeDef hUsbDeviceHS;

int isReadUpgrade = 0;
int jump_app_count = JUMP_APP_DELAY;

void boot(void)
{
    uart_recv_start();
    uart_receive_struct_init();

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

struct IAP_STRUCT
{
    uint32_t bin_size;
    uint32_t write_pos;
    uint32_t crc;
    uint16_t index;
    bool is_bin_head_ok;
};

struct IAP_STRUCT iap_s;
void iap_reset(struct IAP_STRUCT *iap)
{
    iap->bin_size = 0;
    iap->index = 0;
    iap->write_pos = 0;
    iap->is_bin_head_ok = FALSE;
    iap->crc = 0;
}

int send_iap_ack(PARSE_STRUCT *parse_uart, uint8_t cmd)
{
    FRAME_STRUCT frame_s_tmp;
    frame_s_tmp.Version = parse_uart->frame_s.Version;
    frame_s_tmp.FrameDataLen = 0;
    frame_s_tmp.SourceID = parse_uart->frame_s.TargetID;
    frame_s_tmp.TargetID = parse_uart->frame_s.SourceID;
    frame_s_tmp.Cmd = CMD_IAP_ACK;
    frame_s_tmp.DataIndex = cmd;
    frame_s_tmp.frame_data = NULL;
    frame_s_tmp.send_frame_fun = parse_uart->frame_s.send_frame_fun;
    return creat_send_cmd(parse_uart, &frame_s_tmp);
}

IAP_STATUS parse_iap_frame(PARSE_STRUCT *parse_uart)
{
    uint32_t temp32 = 0;
    IAP_STATUS result = IAPERROR_OTHER;
    static bool is_iap_busy = FALSE;

    if (is_iap_busy)
    {
        result = IAPERROR_BUSY;
        send_iap_ack(parse_uart, (uint8_t)result);
        is_iap_busy = FALSE;
        return result;
    }
    else
    {
        is_iap_busy = TRUE;

        switch (parse_uart->frame_s.Cmd)
        {
        case CMD_GET_VERSION:
            result = IAP_OK;
            break;             
        case CMD_IAP_BEGIN:
            memcpy(&temp32, parse_uart->frame_s.frame_data, 4);
            iap_reset(&iap_s);

            iap_s.bin_size = temp32;

            if (flash_erase_app() == FLASH_OK)
                result = IAP_OK;

            break;
        case CMD_IAP_TRANS:

            if (iap_s.index == parse_uart->frame_s.DataIndex)
            {
                if (parse_uart->frame_s.FrameDataLen % 4 != 0 || parse_uart->frame_s.FrameDataLen == 0)
                {
                    result = IAPERROR_FORM;
                    break;
                }

                if (parse_uart->frame_s.DataIndex == 0 && iap_s.write_pos == 0)
                {
                    memcpy((uint8_t *)&temp32, parse_uart->frame_s.frame_data, 4);
                    if ((temp32 & 0x2FFE0000) == 0x20020000)
                    {
                        iap_s.is_bin_head_ok = TRUE;
                    }
                    else
                    {
                        result = IAPERROR_HEAD;
                        break;
                    }
                }

                if (flash_write(FLASH_APP_START_ADDRESS + iap_s.write_pos, (uint32_t *)parse_uart->frame_s.frame_data, parse_uart->frame_s.FrameDataLen / 4) != FLASH_OK)
                {
                    result = IAPERROR_WRITEFLASH;
                }else
                {
                    iap_s.index++;
                    iap_s.write_pos += parse_uart->frame_s.FrameDataLen;
                    result = IAP_OK;
                }

            }
            else
            {
                result = IAPERROR_INDEX;
            }
            break;
        case CMD_IAP_VERIFY:

            break;
        case CMD_IAP_RESET:

            break;
        case CMD_IAP_READY:
            isReadUpgrade = 1;
            jump_app_count = 0;
            result = IAP_OK;
            break;
        default:
            break;
        }

        if (result != IAP_OK)
            iap_reset(&iap_s);

        is_iap_busy = FALSE;
    }

    send_iap_ack(parse_uart, result);
    return result;
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