#include "Types/RtosTypes.h"
#include "cmsis_os2.h"
#include "main.h"
#include "FreeRTOS.h"
#include "usart.h"
#include "string.h"
#include <stdio.h>

#define PRINTF_FW_ID_PRODUCT          "iq_verify_light_20260510"
#define PRINTF_FW_ID_BUILD            "20260510_0142"
#define PRINTF_FW_ID_FEATURE_ASK      "virtual_cd_rd"
#define PRINTF_FW_ID_FEATURE_PSK      "rotate_sparse"
#define PRINTF_FW_ID_REPEAT_COUNT     2U
#define PRINTF_FW_ID_REPEAT_PERIOD_MS 500U
#define PRINTF_TASK_QUEUE_WAIT_MS     100U

volatile uart_mode_t g_uart_mode = UART_MODE_LOG;

static void printf_task_emit_fw_id(void)
{
    if (g_uart_mode != UART_MODE_LOG)
    {
        return;
    }

    char line[128];
    int n = snprintf(line,
                     sizeof(line),
                     "fw_id,%s,build=%s,ask=%s,psk=%s\r\n",
                     PRINTF_FW_ID_PRODUCT,
                     PRINTF_FW_ID_BUILD,
                     PRINTF_FW_ID_FEATURE_ASK,
                     PRINTF_FW_ID_FEATURE_PSK);
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
        HAL_UART_Transmit(&huart1, (uint8_t *)line, (uint16_t)n, HAL_MAX_DELAY);
    }
}

static void print_queue_send_typed(const char *text, print_kind_t kind)
{
    print_msg_t msg;
    size_t len;

    if ((text == NULL) || (PrintQueueHandle == NULL))
    {
        return;
    }

    len = strlen(text);

    if (len > sizeof(msg.data))
    {
        len = sizeof(msg.data);
    }

    memcpy(msg.data, text, len);
    msg.len = (uint16_t)len;
    msg.kind = (uint8_t)kind;

    (void)osMessageQueuePut(PrintQueueHandle, &msg, 0U, 0U);
}
void print_queue_send_vofa(const char *text)
{
    print_queue_send_typed(text, PRINT_KIND_VOFA);
}

void print_queue_send_log(const char *text)
{
    print_queue_send_typed(text, PRINT_KIND_LOG);
}

void StartPrintfTask(void *argument)
{
    print_msg_t msg;
    uint32_t fw_id_repeat_left = PRINTF_FW_ID_REPEAT_COUNT;
    uint32_t next_fw_id_tick = osKernelGetTickCount();
    (void)argument;

    for (;;)
    {
        uint32_t now_tick = osKernelGetTickCount();

        if ((fw_id_repeat_left > 0U) &&
            ((int32_t)(now_tick - next_fw_id_tick) >= 0))
        {
            printf_task_emit_fw_id();
            fw_id_repeat_left--;
            next_fw_id_tick = now_tick + PRINTF_FW_ID_REPEAT_PERIOD_MS;
        }

        if (osMessageQueueGet(PrintQueueHandle, &msg, NULL, PRINTF_TASK_QUEUE_WAIT_MS) == osOK)
        {
            if ((g_uart_mode == UART_MODE_VOFA && msg.kind == PRINT_KIND_VOFA) ||
                (g_uart_mode == UART_MODE_LOG  && msg.kind == PRINT_KIND_LOG))
            {
                HAL_UART_Transmit(&huart1, (uint8_t *)msg.data, msg.len, HAL_MAX_DELAY);
            }
        }
    }
}
