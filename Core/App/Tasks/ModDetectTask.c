#include "ModDetectTask.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/_intsup.h>
#include <stdbool.h>
#include "dac.h"

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "app_sweep.h"
#include "RtosTypes.h"
#include "Analyze.h"

#define DAC_MAX_CODE 4095U
#define DAC_VREF_MV  3300U
#define ENTER_LOG_ENABLE 0 /* 进入算法调度日志通道开关 */
#define SWEEP_RUSULT_LOG_ENABLE 1 /* 扫频结果日志通道开关 */

static bool sweep_rest =0;
static bool analyze_rest = 0U;

typedef struct
{
    const uint16_t *i_buf;
    const uint16_t *q_buf;
    uint32_t sample_cnt;
    uint8_t pending;
} sweep_block_t;

static sweep_block_t g_sweep_block;
static moddetect_task_stats_t g_sweep_task_stats;

/* 将 ADC 数据块发布到 ModDetectTask 以供处理；如果上一个块仍在处理中，则增加丢弃计数。 */
void sweep_task_publish_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    if (g_sweep_block.pending != 0U)
    {
        g_sweep_task_stats.submit_drop_cnt++;
    }

    g_sweep_block.i_buf = i_buf;
    g_sweep_block.q_buf = q_buf;
    g_sweep_block.sample_cnt = sample_cnt;
    g_sweep_block.pending = 1U;
    g_sweep_task_stats.submit_ok_cnt++;
    g_sweep_task_stats.last_sequence++;
    __set_PRIMASK(primask);
}

static uint32_t dac_mv_to_code(uint32_t mv)
{
    if (mv >= DAC_VREF_MV)
    {
        return DAC_MAX_CODE;
    }

    return (mv * DAC_MAX_CODE + (DAC_VREF_MV / 2U)) / DAC_VREF_MV;
}

void moddetect_task_get_stats(moddetect_task_stats_t *stats_out)
{
    uint32_t primask;

    if (stats_out == NULL)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    *stats_out = g_sweep_task_stats;
    __set_PRIMASK(primask);
}

void StartModDetectTask(void *argument)
{
    (void)argument;
    uint32_t dac_code = dac_mv_to_code(1400U);
    HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
    HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, dac_code);   

    for (;;)
    {
        sweep_block_t block;
        uint32_t center_hz;
        uint32_t primask;

        if (SweepBlockReadySemHandle == NULL)
        {
            osDelay(20U);
            continue;
        }

        if (osSemaphoreAcquire(SweepBlockReadySemHandle, osWaitForever) != osOK)
        {
            continue;
        }

        /* 进入临界区以避免与ADC ISR赛跑,并领取数据块 */
        primask = __get_PRIMASK();
        __disable_irq();
        block = g_sweep_block;
        g_sweep_block.pending = 0U;
        __set_PRIMASK(primask);

        if (block.pending == 0U)
        {
            continue;
        }

        /* 检测算法入口是否能够收到ADC任务通知 */
    #if (ENTER_LOG_ENABLE != 0U)
        print_queue_send("moddetect: block ready\r\n");
    #endif

        /* 调用扫频获取频点信息*/
        center_hz = app_sweep_find_center_hz(APP_SWEEP_DEFAULT_START_HZ,
                                             APP_SWEEP_DEFAULT_STOP_HZ,
                                             APP_SWEEP_DEFAULT_STEP_HZ,
                                             block.i_buf,
                                             block.q_buf,
                                             block.sample_cnt);

        /* 更新 ModDetectTask 的对外状态统计 */
        primask = __get_PRIMASK();
        __disable_irq();
        g_sweep_task_stats.process_cnt++;
        if (center_hz != 0U)
        {
            g_sweep_task_stats.center_hz = center_hz;
            g_sweep_task_stats.result_ready = 1U;
        }
        __set_PRIMASK(primask);

        if ((center_hz != 0U) && (sweep_rest == 0U))
        {
            #if (SWEEP_RUSULT_LOG_ENABLE != 0U)
            char sweep_result_log[128];
            int n = snprintf(sweep_result_log,
                     sizeof(sweep_result_log),
                     "moddetect: sweep done center=%luHz\r\n",
                     (unsigned long)center_hz);

            if ((n > 0) && ((size_t)n < sizeof(sweep_result_log)))
            {
            print_queue_send(sweep_result_log);
            }
            #endif

            /* 扫频结果已处理，重置扫频状态以准备下一次扫频，通过sweep_rest=0开启循环扫频 */
            sweep_rest = 1U;
            if(sweep_rest == 0U)    app_sweep_reset();
        }

        if ((center_hz != 0U) && (analyze_rest == 0U))
        {
            analyze_start(center_hz,block.i_buf,block.q_buf,block.sample_cnt);
            /* 分析结果已处理，重置分析状态以准备下一次发分析，通过analyze_rest=0开启循环分析 */
            analyze_rest = 1U;
        }
    }
}
