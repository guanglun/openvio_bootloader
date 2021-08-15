#include "iap.h"
#include "flash.h"
#include "string.h"
#include "uart_parse.h"
#include "usb_parse.h"

extern USBD_HandleTypeDef hUsbDeviceHS;
extern struct EEPROM_CONFIG_STRUCT eeprom;

int isReadUpgrade = 0;
int jump_app_count = JUMP_APP_DELAY;

void boot(void)
{
    int cnt_1s = 0;

    uart_recv_start();

    uart_receive_struct_init();

    usb_receive_struct_init();

    flash_eeprom_load();

    if (eeprom.reboot_to_bootloader == 1 ||
        HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin) == GPIO_PIN_RESET)
    {
        if (eeprom.reboot_to_bootloader == 1)
        {
            eeprom.reboot_to_bootloader = 0;
            flash_eeprom_save();
        }
        isReadUpgrade = 1;
    }

    if (isReadUpgrade == 0)
    {
        while (jump_app_count--)
        {
            cnt_1s++;
            if (cnt_1s >= 100)
            {
                HAL_GPIO_TogglePin(LED_R_GPIO_Port, LED_R_Pin);
                cnt_1s = 0;
            }

            if (uart_parse_loop() == 0)
                break;

            if (usb_parse_loop() == 0)
                break;
            HAL_Delay(1);
        }

        if (jump_app_count <= 0)
        {
            printf("start jump to app ...\r\n");

            if (!iap_jump_to(FLASH_APP_START_ADDRESS))
            {
                printf("jump fail, loop\r\n");
            }
        }
    }

    printf("loop...\r\n");
    while (1)
    {
        cnt_1s++;
        if (cnt_1s >= 1000)
        {
            HAL_GPIO_TogglePin(LED_G_GPIO_Port, LED_G_Pin);
            cnt_1s = 0;
        }

        uart_parse_loop();
        usb_parse_loop();
        HAL_Delay(1);
    }
}

//OUT
#define REQUEST_SET_REBOOT 0x01

//IN
#define REQUEST_GET_VERSION 0x00

extern struct EEPROM_CONFIG_STRUCT eeprom;

int usb_recv(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    int ret = 0;
    switch (cmd)
    {
    case REQUEST_SET_REBOOT:
        if (pbuf[0] == 0)
        {
            eeprom.reboot_to_bootloader = 1;
            flash_eeprom_save();
            __set_FAULTMASK(1);
            NVIC_SystemReset();
        }
        else if (pbuf[0] == 1)
        {
            __set_FAULTMASK(1);
            NVIC_SystemReset();
        }
        break;
    default:
        ret = -1;
        break;
    }
    return ret;
}

int usb_ctrl(uint8_t cmd, uint8_t *pbuf)
{
    int ret = 0;
    switch (cmd)
    {
    case REQUEST_GET_VERSION:
        pbuf[0] = VERSION_1;
        pbuf[1] = VERSION_2;
        pbuf[2] = VERSION_3;
        ret = 3;
        break;
    default:
        ret = -1;
        break;
    }
    return ret;
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

#define ALIGN 32

IAP_STATUS parse_iap_frame(PARSE_STRUCT *parse_uart)
{
    uint32_t temp32 = 0;
    static uint32_t crc = 0;
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
            printf("start erase...\r\n");
            memcpy(&temp32, parse_uart->frame_s.frame_data, 4);
            iap_reset(&iap_s);
            crc = 0;
            iap_s.bin_size = temp32;

            if (flash_erase_app(iap_s.bin_size) == FLASH_OK)
            {
                result = IAP_OK;
            }
            printf("end erase...\r\n");
            break;
        case CMD_IAP_TRANS:

            if (iap_s.index == parse_uart->frame_s.DataIndex)
            {
                // if (parse_uart->frame_s.FrameDataLen % ALIGN != 0 || parse_uart->frame_s.FrameDataLen == 0)
                // {
                //     result = IAPERROR_FORM;
                //     break;
                // }

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

                if (flash_write(FLASH_APP_START_ADDRESS + iap_s.write_pos, (uint8_t *)parse_uart->frame_s.frame_data, parse_uart->frame_s.FrameDataLen) != FLASH_OK)
                {
                    result = IAPERROR_WRITEFLASH;
                }
                else
                {

                    iap_s.index++;
                    iap_s.write_pos += parse_uart->frame_s.FrameDataLen;

                    for (int i = 0; i < parse_uart->frame_s.FrameDataLen; i++)
                    {
                        crc += parse_uart->frame_s.frame_data[i];
                    }

                    crc = ~crc;
                    result = IAP_OK;
                }
            }
            else
            {
                result = IAPERROR_INDEX;
            }
            break;
        case CMD_IAP_VERIFY:
            if (parse_uart->frame_s.FrameDataLen == 4)
            {
                memcpy(&temp32, parse_uart->frame_s.frame_data, 4);
                //printf("%02X %02X\r\n",crc,temp32);

                if (*(((uint8_t *)&crc) + 0) == parse_uart->frame_s.frame_data[0] &&
                    *(((uint8_t *)&crc) + 1) == parse_uart->frame_s.frame_data[1] &&
                    *(((uint8_t *)&crc) + 2) == parse_uart->frame_s.frame_data[2] &&
                    *(((uint8_t *)&crc) + 3) == parse_uart->frame_s.frame_data[3])
                {
                    result = IAP_OK;
                }
            }
            else
            {
                result = IAPERROR_CRC;
            }

            break;
        case CMD_IAP_RESET:
            printf("reboot...\r\n");
            __set_FAULTMASK(1);
            NVIC_SystemReset();
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
    if (((*(uint32_t *)address) & 0x20000000) == 0x20000000 && (*(uint32_t *)address) != 0xFFFFFFFF)
    {
        printf("jump check ok ...\r\n");
        __HAL_UART_DISABLE_IT(&huart2, UART_IT_IDLE);
        HAL_UART_MspDeInit(&huart2);

        USBD_DeInit(&hUsbDeviceHS);

        system_jump_to(address);
        return 1;
    }
    return 0;
}