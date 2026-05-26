#include "Types/RtosTypes.h"
#include "AppDebugConfig.h"
#include "cmsis_os2.h"
#include "main.h"
#include "FreeRTOS.h"
#include "usart.h"
#include "string.h"
#include <stdio.h>
#include <ctype.h>

#include "ModDetectTask.h"

#define PRINTF_FW_ID_PRODUCT          "IQ_Sweep_test"
#define PRINTF_FW_ID_BUILD            "20260523_05:36"
#define PRINTF_FW_ID_FEATURE_ASK      "virtual_cd_rd"
#define PRINTF_FW_ID_FEATURE_PSK      "fm_guard_psk"
#define PRINTF_FW_ID_REPEAT_COUNT     1U
#define PRINTF_FW_ID_REPEAT_PERIOD_MS 500U
#define PRINTF_TASK_QUEUE_WAIT_MS     100U
#define PRINTF_SERIAL_CMD_ENABLE      1U    /* 临时串口控制入口：置 0 即可关闭，方便赛后移除。 */
#define PRINTF_SERIAL_CMD_MAX_LEN     48U   /* 单条控制命令最大长度，命令只设计为短 ASCII 文本。 */
#define PRINTF_SERIAL_CMD_PREFIX      '@'   /* 控制命令必须以 @ 开头，避免普通日志或 VOFA 数据被误判。 */
/* 总日志开关在 AppDebugConfig.h 中定义 */

#if (PRINTF_SERIAL_CMD_ENABLE != 0U)
static uint8_t s_cmd_rx_byte;
static char s_cmd_line[PRINTF_SERIAL_CMD_MAX_LEN];
static char s_cmd_pending_line[PRINTF_SERIAL_CMD_MAX_LEN];
static volatile uint8_t s_cmd_line_len;
static volatile uint8_t s_cmd_pending;
static volatile uint8_t s_cmd_overflow;

static void printf_serial_cmd_start_rx(void);
static void printf_serial_cmd_poll(void);
static void printf_serial_cmd_handle(const char *cmd);
static void printf_serial_cmd_emit(const char *text);
#endif

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

#if (PRINTF_SERIAL_CMD_ENABLE != 0U)
/* 串口控制接收：启动 USART1 单字节中断接收，只解析以 @ 开头的短行命令。 */
static void printf_serial_cmd_start_rx(void)
{
    (void)HAL_UART_Receive_IT(&huart1, &s_cmd_rx_byte, 1U);
}

static void printf_serial_cmd_emit(const char *text)
{
    if (text == NULL)
    {
        return;
    }

    (void)HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)strlen(text), HAL_MAX_DELAY);
}

static char printf_serial_cmd_upper(char c)
{
    return (char)toupper((unsigned char)c);
}

static uint8_t printf_serial_cmd_equal(const char *cmd, const char *expect)
{
    while ((*cmd != '\0') && (*expect != '\0'))
    {
        if (printf_serial_cmd_upper(*cmd) != printf_serial_cmd_upper(*expect))
        {
            return 0U;
        }
        cmd++;
        expect++;
    }

    return ((*cmd == '\0') && (*expect == '\0')) ? 1U : 0U;
}

/* 串口控制执行：只转发到既有任务接口，不直接改扫频或分析内部状态。 */
static void printf_serial_cmd_handle(const char *cmd)
{
    moddetect_task_stats_t stats;

    if ((cmd == NULL) || (cmd[0] != PRINTF_SERIAL_CMD_PREFIX))
    {
        return;
    }

    cmd++;
    while ((*cmd == ' ') || (*cmd == '\t'))
    {
        cmd++;
    }

    if (printf_serial_cmd_equal(cmd, "TASK") || printf_serial_cmd_equal(cmd, "START"))
    {
        moddetect_task_request_mode(MODDETECT_RUN_TASK);
        printf_serial_cmd_emit("cmd:ok,TASK\r\n");
    }
    else if (printf_serial_cmd_equal(cmd, "CAL"))
    {
        moddetect_task_request_mode(MODDETECT_RUN_CALIBRATION);
        printf_serial_cmd_emit("cmd:ok,CAL\r\n");
    }
    else if (printf_serial_cmd_equal(cmd, "IDLE") || printf_serial_cmd_equal(cmd, "STOP"))
    {
        moddetect_task_request_mode(MODDETECT_RUN_IDLE);
        printf_serial_cmd_emit("cmd:ok,IDLE\r\n");
    }
    else if (printf_serial_cmd_equal(cmd, "OCXO"))
    {
        moddetect_task_request_mode(MODDETECT_RUN_OCXO_CAL);
        printf_serial_cmd_emit("cmd:ok,OCXO\r\n");
    }
    else if (printf_serial_cmd_equal(cmd, "PING"))
    {
        printf_serial_cmd_emit("cmd:pong\r\n");
    }
    else if (printf_serial_cmd_equal(cmd, "STAT"))
    {
        char line[128];
        int n;

        moddetect_task_get_stats(&stats);
        n = snprintf(line,
                     sizeof(line),
                     "cmd:stat,mode=%u,center=%lu,ready=%u,cal=%u,adc=%u\r\n",
                     (unsigned int)stats.run_mode,
                     (unsigned long)stats.center_hz,
                     (unsigned int)stats.result_ready,
                     (unsigned int)stats.cal_valid,
                     (unsigned int)stats.adc_ref_ok);
        if ((n > 0) && ((size_t)n < sizeof(line)))
        {
            printf_serial_cmd_emit(line);
        }
    }
    else if (printf_serial_cmd_equal(cmd, "HELP"))
    {
        printf_serial_cmd_emit("cmd:help,@TASK,@CAL,@IDLE,@OCXO,@PING,@STAT\r\n");
    }
    else
    {
        printf_serial_cmd_emit("cmd:err,unknown\r\n");
    }
}

/* 串口控制轮询：在 PrintfTask 任务上下文处理命令，避免中断里调用任务状态接口。 */
static void printf_serial_cmd_poll(void)
{
    char cmd[PRINTF_SERIAL_CMD_MAX_LEN];
    uint32_t primask;
    uint8_t pending;
    uint8_t overflow;

    primask = __get_PRIMASK();
    __disable_irq();
    pending = s_cmd_pending;
    overflow = s_cmd_overflow;
    if (pending != 0U)
    {
        memcpy(cmd, s_cmd_pending_line, sizeof(cmd));
        s_cmd_pending = 0U;
    }
    s_cmd_overflow = 0U;
    __set_PRIMASK(primask);

    if (overflow != 0U)
    {
        printf_serial_cmd_emit("cmd:err,overflow\r\n");
    }

    if (pending != 0U)
    {
        cmd[sizeof(cmd) - 1U] = '\0';
        printf_serial_cmd_handle(cmd);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((huart != NULL) && (huart->Instance == USART1))
    {
        uint8_t byte = s_cmd_rx_byte;

        if ((byte == '\r') || (byte == '\n'))
        {
            if (s_cmd_line_len > 0U)
            {
                if (s_cmd_pending == 0U)
                {
                    uint8_t i;

                    for (i = 0U; i < s_cmd_line_len; i++)
                    {
                        s_cmd_pending_line[i] = s_cmd_line[i];
                    }
                    s_cmd_pending_line[s_cmd_line_len] = '\0';
                    s_cmd_pending = 1U;
                }
                s_cmd_line_len = 0U;
            }
        }
        else if (byte >= 0x20U)
        {
            if (s_cmd_line_len < (PRINTF_SERIAL_CMD_MAX_LEN - 1U))
            {
                s_cmd_line[s_cmd_line_len] = (char)byte;
                s_cmd_line_len++;
            }
            else
            {
                s_cmd_line_len = 0U;
                s_cmd_overflow = 1U;
            }
        }

        printf_serial_cmd_start_rx();
    }
}
#endif

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

#if (PRINTF_SERIAL_CMD_ENABLE != 0U)
    printf_serial_cmd_start_rx();
#endif

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

#if (PRINTF_SERIAL_CMD_ENABLE != 0U)
        printf_serial_cmd_poll();
#endif
    }
}
