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
#include "app_dds_ctrl.h"
#include "DemodTask.h"

#define DAC_MAX_CODE 4095U
#define DAC_VREF_MV  3300U
#define ENTER_LOG_ENABLE 0 /* 进入算法调度日志通道开关 */
#define SWEEP_RUSULT_LOG_ENABLE 1 /* 扫频结果日志通道开关 */
#define MODDETECT_ANALYZE_LOG_FLUSH_LINES 2U /* 每个 ADC 调度周期最多发送的分析日志行数，避免打印队列被频谱日志打满。 */
#define MODDETECT_LOW_IF_VALIDATE_ENABLE 1U /* 分析后检查低中频是否接近目标，偏太多说明扫频锁点不可信。 */
#define MODDETECT_LOW_IF_TARGET_HZ ((int32_t)APP_SWEEP_FINAL_LO_OFFSET_HZ) /* 正常锁定后低中频应接近最终 LO 偏移。 */
#define MODDETECT_LOW_IF_TOL_HZ 30000L /* 低中频允许误差，超过该值触发一次中心修正。 */
#define MODDETECT_LOW_IF_MAX_CORRECTION_HZ 200000L /* 单次中心修正上限，避免异常谱把中心拉太远。 */
#define MODDETECT_LOW_IF_CORRECTION_LOG_ENABLE 1U /* 是否输出低中频修正日志。 */
#define MODDETECT_AUTO_DEMOD_ENABLE 1U /* 分析完成后是否自动进入解调；当前默认关闭，便于重复扫频分析。 */

static bool sweep_rest =0;
static bool analyze_rest = 0U;
#if (MODDETECT_AUTO_DEMOD_ENABLE != 0U)
static bool demod_triggered = 0U;
#endif
static bool low_if_corrected = 0U;
static moddetect_run_mode_t g_run_mode = MODDETECT_RUN_IDLE;
static moddetect_run_mode_t g_requested_mode = MODDETECT_RUN_IDLE;

typedef struct
{
    const uint16_t *i_buf;
    const uint16_t *q_buf;
    uint32_t sample_cnt;
    uint8_t pending;
} sweep_block_t;

static sweep_block_t g_sweep_block;
static moddetect_task_stats_t g_sweep_task_stats;

static void moddetect_apply_mode_request(void);
static uint8_t moddetect_is_busy_for_new_request(void);
static void moddetect_update_cal_stats(void);
static uint8_t moddetect_try_low_if_correction(void);

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

static int32_t moddetect_abs_i32(int32_t value)
{
    return (value < 0) ? -value : value;
}

static int32_t moddetect_limit_i32(int32_t value, int32_t limit_abs)
{
    if (value > limit_abs)
    {
        return limit_abs;
    }

    if (value < -limit_abs)
    {
        return -limit_abs;
    }

    return value;
}

static uint32_t moddetect_clip_center_hz(int64_t center_hz)
{
    if (center_hz < (int64_t)APP_SWEEP_DEFAULT_START_HZ)
    {
        return APP_SWEEP_DEFAULT_START_HZ;
    }

    if (center_hz > (int64_t)APP_SWEEP_DEFAULT_STOP_HZ)
    {
        return APP_SWEEP_DEFAULT_STOP_HZ;
    }

    return (uint32_t)center_hz;
}

static void moddetect_set_final_lo(uint32_t center_hz)
{
    AppDdsCmd cmd = AppDDS_MakeSetChFreqApplyCmd(0U, center_hz + APP_SWEEP_FINAL_LO_OFFSET_HZ);

    (void)AppDDS_DispatchCmd(&cmd);
}

/* 分析后用残留低中频校验锁点；偏差过大时只修正一次中心并重跑分析。 */
static uint8_t moddetect_try_low_if_correction(void)
{
#if (MODDETECT_LOW_IF_VALIDATE_ENABLE != 0U)
    analyze_result_t result;
    int32_t low_if_error_hz;
    int32_t correction_hz;
    uint32_t corrected_center_hz;
    uint32_t primask;

    if ((low_if_corrected != 0U) || (analyze_is_done() == 0U))
    {
        return 0U;
    }

    analyze_get_result(&result);
    if ((result.done == 0U) || (result.center_hz == 0UL))
    {
        return 0U;
    }

    low_if_error_hz = result.low_if_hz - MODDETECT_LOW_IF_TARGET_HZ;
    if (moddetect_abs_i32(low_if_error_hz) <= MODDETECT_LOW_IF_TOL_HZ)
    {
        return 0U;
    }

    correction_hz = moddetect_limit_i32(-low_if_error_hz, MODDETECT_LOW_IF_MAX_CORRECTION_HZ);
    corrected_center_hz = moddetect_clip_center_hz((int64_t)result.center_hz + (int64_t)correction_hz);
    if (corrected_center_hz == result.center_hz)
    {
        low_if_corrected = 1U;
        return 0U;
    }

#if (MODDETECT_LOW_IF_CORRECTION_LOG_ENABLE != 0U)
    {
        char log_buf[160];
        int n = snprintf(log_buf,
                         sizeof(log_buf),
                         "moddetect: low_if correction old=%luHz new=%luHz low_if=%ldHz target=%ldHz\r\n",
                         (unsigned long)result.center_hz,
                         (unsigned long)corrected_center_hz,
                         (long)result.low_if_hz,
                         (long)MODDETECT_LOW_IF_TARGET_HZ);

        if ((n > 0) && ((size_t)n < sizeof(log_buf)))
        {
            print_queue_send(log_buf);
        }
    }
#endif

    low_if_corrected = 1U;
    moddetect_set_final_lo(corrected_center_hz);
    analyze_start(corrected_center_hz);

    primask = __get_PRIMASK();
    __disable_irq();
    g_sweep_task_stats.center_hz = corrected_center_hz;
    g_sweep_task_stats.result_ready = 1U;
    __set_PRIMASK(primask);

    return 1U;
#else
    return 0U;
#endif
}

/* 判断当前是否正在执行不可中断流程，用于拒绝按钮重复启动。 */
static uint8_t moddetect_is_busy_for_new_request(void)
{
    if ((g_run_mode == MODDETECT_RUN_CALIBRATION) && (g_sweep_task_stats.cal_done == 0U))
    {
        return 1U;
    }

    if ((g_run_mode == MODDETECT_RUN_TASK) &&
        (g_sweep_task_stats.result_ready == 0U) &&
        (sweep_rest == 0U))
    {
        return 1U;
    }

    if (analyze_is_active() != 0U)
    {
        return 1U;
    }

    return 0U;
}

/* UI 请求切换校准或任务模式；忙碌时忽略请求，避免中途清空状态。 */
void moddetect_task_request_mode(moddetect_run_mode_t mode)
{
    uint32_t primask;

    if ((mode != MODDETECT_RUN_IDLE) &&
        (mode != MODDETECT_RUN_CALIBRATION) &&
        (mode != MODDETECT_RUN_TASK))
    {
        return;
    }

    if ((mode != MODDETECT_RUN_IDLE) && (moddetect_is_busy_for_new_request() != 0U))
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    g_requested_mode = mode;
    g_sweep_task_stats.run_mode = mode;
    g_sweep_task_stats.center_hz = 0UL;
    g_sweep_task_stats.result_ready = 0U;
    if (mode == MODDETECT_RUN_CALIBRATION)
    {
        g_sweep_task_stats.cal_done = 0U;
        g_sweep_task_stats.cal_valid = 0U;
        g_sweep_task_stats.cal_clip_cnt = 0UL;
    }
    __set_PRIMASK(primask);
}

/* 读取当前运行模式，供 UI 状态显示使用。 */
moddetect_run_mode_t moddetect_task_get_mode(void)
{
    moddetect_run_mode_t mode;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    mode = g_run_mode;
    __set_PRIMASK(primask);
    return mode;
}

/* 将 Sweep 模块的 RAM 校准状态同步到任务统计。 */
static void moddetect_update_cal_stats(void)
{
    app_sweep_calibration_stats_t cal_stats;
    uint32_t primask;

    app_sweep_get_calibration_stats(&cal_stats);
    primask = __get_PRIMASK();
    __disable_irq();
    g_sweep_task_stats.cal_clip_cnt = cal_stats.clip_count;
    g_sweep_task_stats.cal_valid = cal_stats.valid;
    __set_PRIMASK(primask);
}

/* 消费 UI 模式请求并重置本任务的一次性状态。 */
static void moddetect_apply_mode_request(void)
{
    moddetect_run_mode_t requested;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    requested = g_requested_mode;
    g_requested_mode = MODDETECT_RUN_IDLE;
    __set_PRIMASK(primask);

    if (requested == MODDETECT_RUN_IDLE)
    {
        return;
    }

    app_sweep_reset();
    sweep_rest = 0U;
    analyze_rest = 0U;
#if (MODDETECT_AUTO_DEMOD_ENABLE != 0U)
    demod_triggered = 0U;
#endif
    low_if_corrected = 0U;
    demod_task_stop();

    primask = __get_PRIMASK();
    __disable_irq();
    g_run_mode = requested;
    g_sweep_task_stats.run_mode = requested;
    g_sweep_task_stats.center_hz = 0UL;
    g_sweep_task_stats.result_ready = 0U;
    if (requested == MODDETECT_RUN_CALIBRATION)
    {
        g_sweep_task_stats.cal_done = 0U;
        g_sweep_task_stats.cal_valid = 0U;
        g_sweep_task_stats.cal_clip_cnt = 0UL;
    }
    __set_PRIMASK(primask);
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

        analyze_log_flush_step(MODDETECT_ANALYZE_LOG_FLUSH_LINES);

        /* 统计已经领取的有效 ADC 块；后续可能进入扫频、分析或只刷新日志。 */
        primask = __get_PRIMASK();
        __disable_irq();
        g_sweep_task_stats.process_cnt++;
        g_sweep_task_stats.run_mode = g_run_mode;
        __set_PRIMASK(primask);

        moddetect_apply_mode_request();

        if (g_run_mode == MODDETECT_RUN_IDLE)
        {
            continue;
        }

        if (g_run_mode == MODDETECT_RUN_CALIBRATION)
        {
            uint8_t cal_done;

            cal_done = app_sweep_calibrate_baseline(APP_SWEEP_DEFAULT_START_HZ,
                                                    APP_SWEEP_DEFAULT_STOP_HZ,
                                                    APP_SWEEP_DEFAULT_STEP_HZ,
                                                    block.i_buf,
                                                    block.q_buf,
                                                    block.sample_cnt);
            if (cal_done != 0U)
            {
                primask = __get_PRIMASK();
                __disable_irq();
                g_sweep_task_stats.cal_done = 1U;
                __set_PRIMASK(primask);
                moddetect_update_cal_stats();
            }
            continue;
        }

        if ((analyze_rest != 0U) && (analyze_is_done() == 0U))
        {
            (void)analyze_process_block(block.i_buf, block.q_buf, block.sample_cnt);
            analyze_log_flush_step(MODDETECT_ANALYZE_LOG_FLUSH_LINES);
            continue;
        }

        if ((analyze_rest != 0U) && (analyze_is_done() != 0U))
        {
            if (moddetect_try_low_if_correction() != 0U)
            {
                continue;
            }
        }

        if ((analyze_rest != 0U) && (analyze_log_is_busy() != 0U))
        {
            analyze_log_flush_step(MODDETECT_ANALYZE_LOG_FLUSH_LINES);
            continue;
        }

        if (analyze_rest != 0U)
        {
            if (moddetect_try_low_if_correction() != 0U)
            {
                continue;
            }
            /* Analyze 完成后默认停在识别结果；如需直通解调，打开 MODDETECT_AUTO_DEMOD_ENABLE。 */
#if (MODDETECT_AUTO_DEMOD_ENABLE != 0U)
            if (demod_triggered == 0U)
            {
                analyze_result_t result;
                analyze_get_result(&result);
                if (result.done != 0U)
                {
                    demod_task_start_with_result(&result);
                    demod_triggered = 1U;
                }
            }
#endif
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

        /* 更新 ModDetectTask 的对外扫频状态统计 */
        primask = __get_PRIMASK();
        __disable_irq();
        if ((center_hz != 0U) || (app_sweep_is_done() != 0U))
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

        if ((center_hz == 0U) && (app_sweep_is_done() != 0U) && (sweep_rest == 0U))
        {
            #if (SWEEP_RUSULT_LOG_ENABLE != 0U)
            print_queue_send("moddetect: sweep unlocked center=0Hz\r\n");
            #endif
            sweep_rest = 1U;
        }

        if ((center_hz != 0U) && (analyze_rest == 0U))
        {
            analyze_start(center_hz);
            analyze_rest = 1U;
            analyze_log_flush_step(MODDETECT_ANALYZE_LOG_FLUSH_LINES);
            continue;
        }
    }
}
