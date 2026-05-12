#include "ModDetectTask.h"

#include <string.h>

#include "cmsis_os2.h"

uint32_t moddetect_task_submit_block(const uint16_t *i_buf,
                                     const uint16_t *q_buf,
                                     uint32_t sample_cnt,
                                     uint32_t demod_lo_hz,
                                     uint32_t carrier_hz,
                                     uint32_t tick_ms)
{
    (void)i_buf;
    (void)q_buf;
    (void)sample_cnt;
    (void)demod_lo_hz;
    (void)carrier_hz;
    (void)tick_ms;
    return 0U;
}

void moddetect_task_get_stats(moddetect_task_stats_t *stats_out)
{
    if (stats_out == NULL)
    {
        return;
    }

    memset(stats_out, 0, sizeof(*stats_out));
}

void StartModDetectTask(void *argument)
{
    (void)argument;

    for (;;)
    {
        osDelay(1000U);
    }
}
