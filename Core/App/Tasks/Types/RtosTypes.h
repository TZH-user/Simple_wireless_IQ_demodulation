#ifndef APP_RTOS_H
#define APP_RTOS_H

#include "cmsis_os2.h"
#include <stdint.h>

typedef struct
{
    uint16_t len;
    char data[126];
} print_msg_t;

#define VOFA_SEND_STEP 4U

extern osMessageQueueId_t PrintQueueHandle;
extern osMessageQueueId_t DDSQueueHandle;
extern osMessageQueueId_t ModDetectQueueHandle;
extern osMessageQueueId_t DemodQueueHandle;

extern osSemaphoreId_t AdcFrameReadySemHandle;
extern osSemaphoreId_t SweepBlockReadySemHandle;

extern osThreadId_t AdcTaskHandle;
extern osThreadId_t PrintfTaskHandle;
extern osThreadId_t DDSTaskHandle;
extern osThreadId_t ModDetectTaskHandle;
extern osThreadId_t DemodTaskHandle;

void print_queue_send(const char *text);

void StartDDSTask(void *argument);
void StartPrintfTask(void *argument);
void StartAdcTask(void *argument);
void StartModDetectTask(void *argument);
void StartDemodTask(void *argument);

#endif /* APP_RTOS_H */
