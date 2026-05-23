#include "Types/RtosTypes.h"
#include "AppDebugConfig.h"
#include "cmsis_os2.h"
#include "main.h"
#include "FreeRTOS.h"
#include "usart.h"
#include "string.h"
#include <stdio.h>

#define PRINTF_FW_ID_PRODUCT          "IQ_Sweep_test"
#define PRINTF_FW_ID_BUILD            "20260523_05:36"
#define PRINTF_FW_ID_FEATURE_ASK      "virtual_cd_rd"
#define PRINTF_FW_ID_FEATURE_PSK      "fm_guard_psk"
#define PRINTF_FW_ID_REPEAT_COUNT     1U
#define PRINTF_FW_ID_REPEAT_PERIOD_MS 500U
#define PRINTF_TASK_QUEUE_WAIT_MS     100U
/* 总日志开关在 AppDebugConfig.h 中定义 */

static void printf_task_emit_fw_id(void)
{
#if (APP_PRINT_LOG_ENABLE != 0U)
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
#endif
}

void print_queue_send(const char *text)
{
#if (APP_PRINT_LOG_ENABLE != 0U)
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

    if ((len == sizeof(msg.data)) && (len >= 2U))
    {
        msg.data[len - 2U] = '\r';
        msg.data[len - 1U] = '\n';
    }
    msg.len = (uint16_t)len;

    (void)osMessageQueuePut(PrintQueueHandle, &msg, 0U, 0U);
#else
    (void)text;
#endif
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
#if (APP_PRINT_LOG_ENABLE != 0U)
            HAL_UART_Transmit(&huart1, (uint8_t *)msg.data, msg.len, HAL_MAX_DELAY);
#else
            (void)msg;
#endif
        }
    }
}
