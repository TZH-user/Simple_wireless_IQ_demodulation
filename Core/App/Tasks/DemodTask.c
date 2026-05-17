#include "DemodTask.h"

#include "RtosTypes.h"

static volatile uint8_t g_demod_task_enabled = 0U;

void demod_task_set_enabled(uint8_t enabled)
{
    g_demod_task_enabled = (enabled != 0U) ? 1U : 0U;

    if ((g_demod_task_enabled != 0U) && (DemodTaskHandle != NULL))
    {
        (void)osThreadResume(DemodTaskHandle);
    }
}

void StartDemodTask(void *argument)
{
    (void)argument;

    for (;;)
    {
        if (g_demod_task_enabled == 0U)
        {
            osThreadSuspend(DemodTaskHandle);
            continue;
        }

        osDelay(20U);
    }
}
