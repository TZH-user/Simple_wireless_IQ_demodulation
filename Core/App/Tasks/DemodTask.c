#include "DemodTask.h"

#include "app_demod.h"
#include "RtosTypes.h"

static volatile uint8_t g_demod_task_enabled = 0U;

void demod_task_set_enabled(uint8_t enabled)
{
    g_demod_task_enabled = (enabled != 0U) ? 1U : 0U;

    if (g_demod_task_enabled == 0U)
    {
        app_demod_reset_output();
    }

    if ((g_demod_task_enabled != 0U) && (DemodTaskHandle != NULL))
    {
        (void)osThreadResume(DemodTaskHandle);
    }
}

void StartDemodTask(void *argument)
{
    (void)argument;

    app_demod_reset_output();

    for (;;)
    {
        if (g_demod_task_enabled == 0U)
        {
            osThreadSuspend(DemodTaskHandle);
            continue;
        }

        /* Demod is intentionally detached from the live IQ path.
         * Keep the task alive only as an idle shell for the page.
         */
        osDelay(20U);
    }
}
