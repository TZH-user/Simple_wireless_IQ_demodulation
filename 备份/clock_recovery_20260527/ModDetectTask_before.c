#include "ModDetectTask.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/_intsup.h>
#include <stdbool.h>

#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "app_sweep.h"
#include "RtosTypes.h"
#include "Analyze.h"
#include "app_dds_ctrl.h"
#include "app_buzzer.h"
#include "DemodTask.h"
#include "app_ocxo_cal.h"
#include "SI5351.h"

#define ENTER_LOG_ENABLE 0 /* 进入算法调度日志通道开关 */
#define SWEEP_RUSULT_LOG_ENABLE 1 /* 扫频结果日志通道开关 */
#define MODDETECT_ANALYZE_LOG_FLUSH_LINES 2U /* 每个 ADC 调度周期最多发送的分析日志行数，避免打印队列被频谱日志打满。 */
#define MODDETECT_LOW_IF_VALIDATE_ENABLE 1U /* 分析后检查低中频是否接近目标，偏太多说明扫频锁点不可信。 */
#define MODDETECT_LOW_IF_TARGET_HZ ((int32_t)APP_SWEEP_FINAL_LO_OFFSET_HZ) /* 正常锁定后低中频应接近最终 LO 偏移。 */
#define MODDETECT_LOW_IF_TOL_HZ 30000L /* 低中频允许误差，超过该值触发一次中心修正。 */
#define MODDETECT_LOW_IF_MAX_CORRECTION_HZ 200000L /* 单次中心修正上限，避免异常谱把中心拉太远。 */
#define MODDETECT_LOW_IF_CORRECTION_LOG_ENABLE 1U /* 是否输出低中频修正日志。 */
#define MODDETECT_AUTO_DEMOD_ENABLE 1U /* 分析完成后是否自动进入解调；当前默认关闭，便于重复扫频分析。 */

#define MODDETECT_DDS_REINIT_ON_START_ENABLE 1U /* 每次点击校准或任务前重新初始化 AD9959，降低 DDS 长时间运行后状态漂移的影响。 */

#define MODDETECT_ADC_REF_CLOCK_TIMEOUT_MS 1000U /* ADC 数据块超过该时间未更新，就认为 ADC 参考时钟或采样链路异常。 */
#define MODDETECT_AUTO_TASK_AFTER_SELF_TEST_ENABLE 1U /* 上电自检通过后自动进入任务；关闭后仍保持等待按钮启动。 */
#define MODDETECT_SELF_TEST_STABLE_BLOCKS 3U /* 自检通过需要连续满足的 ADC 数据块数，数值越大越不容易被瞬态状态误触发。 */

static bool sweep_rest =0;
static bool analyze_rest = 0U;
#if (MODDETECT_AUTO_DEMOD_ENABLE != 0U)
static bool demod_triggered = 0U;
#endif
static bool low_if_corrected = 0U;
static uint8_t g_auto_task_requested = 0U;
static uint8_t g_boot_auto_task_enable = 0U;
static uint8_t g_self_test_ok_blocks = 0U;
static uint8_t g_mixed_retry_used = 0U;
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
static void moddetect_request_dds_reinit(void);
static void moddetect_request_ocxo_dds_output(void);
static uint8_t moddetect_self_test_is_ready(void);
static void moddetect_auto_start_task_if_ready(void);
static uint8_t moddetect_try_low_if_correction(void);
static uint8_t moddetect_try_mixed_reanalyze(const analyze_result_t *result);

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
    g_sweep_task_stats.adc_last_tick = osKernelGetTickCount();
    g_sweep_task_stats.adc_ref_ok = 1U;
    __set_PRIMASK(primask);
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

/* 通过 DDS 任务队列重新初始化硬件，避免 ModDetectTask 直接抢占 AD9959 控制权。 */
static void moddetect_request_dds_reinit(void)
{
#if (MODDETECT_DDS_REINIT_ON_START_ENABLE != 0U)
    AppDdsCmd cmd = AppDDS_MakeInitCmd();

    (void)AppDDS_DispatchCmd(&cmd);
#endif
}

/* 进入恒温晶振校准时，让 AD9959 指定通道输出固定参考频率；具体值由 APP_OCXO_CAL_DDS_FREQ_HZ 决定。 */
static void moddetect_request_ocxo_dds_output(void)
{
    AppDdsCmd cmd;

    cmd = AppDDS_MakeSelectChCmd(APP_OCXO_CAL_DDS_CH);
    (void)AppDDS_DispatchCmd(&cmd);
    cmd = AppDDS_MakeSetFreqCmd(APP_OCXO_CAL_DDS_FREQ_HZ);
    (void)AppDDS_DispatchCmd(&cmd);
    cmd = AppDDS_MakeSetAmpCmd(APP_OCXO_CAL_DDS_AMP_CODE);
    (void)AppDDS_DispatchCmd(&cmd);
    cmd = AppDDS_MakeApplyCmd();
    (void)AppDDS_DispatchCmd(&cmd);
}

static void moddetect_set_final_lo(uint32_t center_hz)
{
    AppDdsCmd cmd = AppDDS_MakeSetChFreqApplyCmd(0U, center_hz + APP_SWEEP_FINAL_LO_OFFSET_HZ);

    (void)AppDDS_DispatchCmd(&cmd);
}

/* 上电自检只检查任务启动所需的硬件链路：时钟、DDS 和 ADC 数据心跳。 */
static uint8_t moddetect_self_test_is_ready(void)
{
    const AppDdsStatus *dds_status = AppDDS_GetStatus();

    if (app_si5351_is_clock_ready() == false)
    {
        return 0U;
    }

    if ((dds_status == NULL) || (dds_status->hw_ready == 0U) || (dds_status->last_err != 0))
    {
        return 0U;
    }

    if (g_sweep_task_stats.adc_ref_ok == 0U)
    {
        return 0U;
    }

    return 1U;
}

/* 空闲时自检连续通过后自动进入任务，仍保留 UI 按钮的手动重新开始能力。 */
static void moddetect_auto_start_task_if_ready(void)
{
#if (MODDETECT_AUTO_TASK_AFTER_SELF_TEST_ENABLE != 0U)
    if ((g_auto_task_requested != 0U) || (g_run_mode != MODDETECT_RUN_IDLE))
    {
        return;
    }

    if (g_boot_auto_task_enable == 0U)
    {
        g_self_test_ok_blocks = 0U;
        return;
    }

    if (moddetect_self_test_is_ready() == 0U)
    {
        g_self_test_ok_blocks = 0U;
        return;
    }

    if (g_self_test_ok_blocks < MODDETECT_SELF_TEST_STABLE_BLOCKS)
    {
        g_self_test_ok_blocks++;
    }

    if (g_self_test_ok_blocks < MODDETECT_SELF_TEST_STABLE_BLOCKS)
    {
        return;
    }

    g_auto_task_requested = 1U;
    moddetect_task_request_mode(MODDETECT_RUN_TASK);
    print_queue_send("moddetect: selftest ok, auto task\r\n");
#endif
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

/* 识别结果仍为 MIXED 时，按设置次数重新分析同一锁定中心，减少偶发误判。 */
static uint8_t moddetect_try_mixed_reanalyze(const analyze_result_t *result)
{
    uint8_t retry_limit;
    uint8_t allow_retry;
    uint32_t center_hz;
    char log_buf[128];
    int n;

    if ((result == NULL) || (result->done == 0U) || (result->mode != ANALYZE_MODE_MIXED))
    {
        return 0U;
    }

    retry_limit = app_ocxo_cal_get_mixed_retry_count();
    if (retry_limit == 0U)
    {
        return 0U;
    }

    allow_retry = 0U;
    if (retry_limit == APP_OCXO_CAL_MIXED_RETRY_INFINITE)
    {
        allow_retry = 1U;
    }
    else if (g_mixed_retry_used < retry_limit)
    {
        allow_retry = 1U;
    }

    if (allow_retry == 0U)
    {
        return 0U;
    }

    center_hz = (result->center_hz != 0UL) ? result->center_hz : g_sweep_task_stats.center_hz;
    if (center_hz == 0UL)
    {
        return 0U;
    }

    g_mixed_retry_used++;
    n = snprintf(log_buf,
                 sizeof(log_buf),
                 "moddetect: mixed retry %u/%u center=%luHz\r\n",
                 (unsigned int)g_mixed_retry_used,
                 (unsigned int)retry_limit,
                 (unsigned long)center_hz);
    if ((n > 0) && ((size_t)n < sizeof(log_buf)))
    {
        print_queue_send(log_buf);
    }

    analyze_start(center_hz);
    analyze_log_flush_step(MODDETECT_ANALYZE_LOG_FLUSH_LINES);
    return 1U;
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
        (mode != MODDETECT_RUN_TASK) &&
        (mode != MODDETECT_RUN_OCXO_CAL) &&
        (mode != MODDETECT_RUN_DDS_CAL))
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
    if (mode == MODDETECT_RUN_IDLE)
    {
        g_run_mode = MODDETECT_RUN_IDLE;
        g_auto_task_requested = 1U;
    }
    g_sweep_task_stats.run_mode = mode;
    g_sweep_task_stats.center_hz = 0UL;
    g_sweep_task_stats.result_ready = 0U;
    if (mode == MODDETECT_RUN_CALIBRATION)
    {
        g_sweep_task_stats.cal_done = 0U;
        g_sweep_task_stats.cal_valid = 0U;
        g_sweep_task_stats.cal_state = MODDETECT_CAL_RUNNING;
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
    moddetect_request_dds_reinit();
    sweep_rest = 0U;
    analyze_rest = 0U;
#if (MODDETECT_AUTO_DEMOD_ENABLE != 0U)
    demod_triggered = 0U;
#endif
    low_if_corrected = 0U;
    g_mixed_retry_used = 0U;
    demod_task_stop();
    if (requested != MODDETECT_RUN_OCXO_CAL)
    {
        app_ocxo_cal_leave();
    }
    if (requested != MODDETECT_RUN_DDS_CAL)
    {
        app_ocxo_cal_dds_offset_leave();
    }

    if (requested == MODDETECT_RUN_OCXO_CAL)
    {
        app_ocxo_cal_enter();
        moddetect_request_ocxo_dds_output();
    }
    else if (requested == MODDETECT_RUN_DDS_CAL)
    {
        app_ocxo_cal_dds_offset_enter();
        moddetect_request_ocxo_dds_output();
    }

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
        g_sweep_task_stats.cal_state = MODDETECT_CAL_RUNNING;
        g_sweep_task_stats.cal_clip_cnt = 0UL;
    }
    __set_PRIMASK(primask);
}

void moddetect_task_get_stats(moddetect_task_stats_t *stats_out)
{
    uint32_t primask;
    uint32_t now_tick;

    if (stats_out == NULL)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    *stats_out = g_sweep_task_stats;
    __set_PRIMASK(primask);

    now_tick = osKernelGetTickCount();
    if ((stats_out->adc_last_tick == 0UL) ||
        ((uint32_t)(now_tick - stats_out->adc_last_tick) > MODDETECT_ADC_REF_CLOCK_TIMEOUT_MS))
    {
        stats_out->adc_ref_ok = 0U;
    }
}

void StartModDetectTask(void *argument)
{
    (void)argument;

    app_ocxo_cal_init();
    if (app_ocxo_cal_load_from_flash() != 0U)
    {
        print_queue_send("ocxo:flash,load,1\r\n");
    }
    else
    {
        print_queue_send("ocxo:flash,load,0\r\n");
    }
    g_boot_auto_task_enable = app_ocxo_cal_get_auto_task_enable();

    if (app_sweep_load_calibration_from_flash() != 0U)
    {
        uint32_t primask;

        moddetect_update_cal_stats();
        primask = __get_PRIMASK();
        __disable_irq();
        g_sweep_task_stats.cal_done = 1U;
        g_sweep_task_stats.cal_state = MODDETECT_CAL_HISTORY;
        __set_PRIMASK(primask);
        print_queue_send("cal:flash,load,1\r\n");
    }
    else
    {
        print_queue_send("cal:flash,load,0\r\n");
    }

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
            moddetect_auto_start_task_if_ready();
            continue;
        }

        if ((g_run_mode == MODDETECT_RUN_OCXO_CAL) || (g_run_mode == MODDETECT_RUN_DDS_CAL))
        {
            continue;
        }

        if (g_run_mode == MODDETECT_RUN_CALIBRATION)
        {
            uint8_t cal_done;

            if (g_sweep_task_stats.cal_done != 0U)
            {
                continue;
            }

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
                primask = __get_PRIMASK();
                __disable_irq();
                g_sweep_task_stats.cal_state = MODDETECT_CAL_SAVING;
                __set_PRIMASK(primask);
                if (app_sweep_save_calibration_to_flash() != 0U)
                {
                    print_queue_send("cal:flash,save,1\r\n");
                    app_buzzer_notify_cal_done();
                    primask = __get_PRIMASK();
                    __disable_irq();
                    g_sweep_task_stats.cal_state = MODDETECT_CAL_SAVE_OK;
                    __set_PRIMASK(primask);
                }
                else
                {
                    print_queue_send("cal:flash,save,0\r\n");
                    primask = __get_PRIMASK();
                    __disable_irq();
                    g_sweep_task_stats.cal_state =
                        (g_sweep_task_stats.cal_valid != 0U) ? MODDETECT_CAL_CURRENT : MODDETECT_CAL_NONE;
                    __set_PRIMASK(primask);
                }
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
            analyze_result_t result;

            if (moddetect_try_low_if_correction() != 0U)
            {
                continue;
            }
            analyze_get_result(&result);
            if ((result.done != 0U) && (moddetect_try_mixed_reanalyze(&result) != 0U))
            {
                continue;
            }
            /* CW 没有基带信息需要输出，只停在分析结果；其它类型按设置自动进入解调。 */
#if (MODDETECT_AUTO_DEMOD_ENABLE != 0U)
            if (demod_triggered == 0U)
            {
                if (result.done != 0U)
                {
                    app_buzzer_notify_analyze_done();
                    if (result.mode != ANALYZE_MODE_CW)
                    {
                        demod_task_start_with_result(&result);
                    }
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
            app_buzzer_notify_sweep_lock();
            sweep_rest = 1U;
            /* sweep_rest has been latched above: do not reset the completed sweep here. */
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
