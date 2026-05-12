#ifndef APP_RTOS_H
#define APP_RTOS_H

#include "cmsis_os2.h"
#include <stdint.h>

typedef struct
{
    uint16_t len;
    uint8_t kind;
    char data[125];
} print_msg_t;

typedef enum
{
    UART_MODE_VOFA = 0,
    UART_MODE_LOG
} uart_mode_t;

typedef enum
{
    PRINT_KIND_VOFA = 0,
    PRINT_KIND_LOG
} print_kind_t;

extern volatile uart_mode_t g_uart_mode;

#define VOFA_SEND_STEP 4U

extern osMessageQueueId_t PrintQueueHandle;
extern osMessageQueueId_t DDSQueueHandle;
extern osMessageQueueId_t ModDetectQueueHandle;
extern osMessageQueueId_t DemodQueueHandle;

extern osSemaphoreId_t AdcFrameReadySemHandle;

extern osThreadId_t AdcTaskHandle;
extern osThreadId_t PrintfTaskHandle;
extern osThreadId_t DDSTaskHandle;
extern osThreadId_t ModDetectTaskHandle;
extern osThreadId_t DemodTaskHandle;

void print_queue_send_vofa(const char *text);
void print_queue_send_log(const char *text);

void StartDDSTask(void *argument);
void StartPrintfTask(void *argument);
void StartAdcTask(void *argument);
void StartModDetectTask(void *argument);
void StartDemodTask(void *argument);

#endif /* APP_RTOS_H */
