#include "app_adc_log.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "AppDebugConfig.h"
#include "RtosTypes.h"
#include <math.h>
#include <stdio.h>

#define APP_ADC_LOG_PERIOD_MS        1000U
#define APP_ADC_IQ_PREPROC_LOG_MS    1000U

/* 宏定义说明：APP_ADC_CARRIER_SYNC_LOG_MS = 100U；相位闭环调试阶段每 100ms 输出一次 carrier 状态，便于观察 DPLL 动态。 */
#ifndef APP_ADC_CARRIER_SYNC_LOG_MS
#define APP_ADC_CARRIER_SYNC_LOG_MS 100U
#endif

/* 宏定义说明：APP_ADC_IQ_AMP_PHASE_LOG_MS = 1000U；锁定后 I/Q 幅度和 atan2(Q,I) 相位日志的最小输出周期。 */
#define APP_ADC_IQ_AMP_PHASE_LOG_MS  1000U
/* 宏定义说明：APP_ADC_LOG_RAD_TO_MDEG = 57295.7795f；弧度到毫度的换算系数，便于串口直接观察角度。 */
#define APP_ADC_LOG_RAD_TO_MDEG      57295.7795f

#ifndef ADC_IQ_PREPROC_LOG_ENABLE
#define ADC_IQ_PREPROC_LOG_ENABLE ADC_ALGO_LOG_ENABLE
#endif

void app_adc_log_health_1s(const app_adc_log_health_snapshot_t *snapshot)
{
    static uint32_t last_log_tick = 0U;
    uint32_t now_tick;
    UBaseType_t adc_stack_words;
    UBaseType_t printf_stack_words;
    char log_buf[180];
    int n;

    if ((ADC_HEALTH_LOG_ENABLE == 0U) || (snapshot == NULL))
    {
        return;
    }

    now_tick = osKernelGetTickCount();
    if ((last_log_tick != 0U) &&
        ((uint32_t)(now_tick - last_log_tick) < APP_ADC_LOG_PERIOD_MS))
    {
        return;
    }
    last_log_tick = now_tick;

    if (g_uart_mode != UART_MODE_LOG)
    {
        return;
    }

    adc_stack_words = uxTaskGetStackHighWaterMark(NULL);
    printf_stack_words = (PrintfTaskHandle != NULL) ? uxTaskGetStackHighWaterMark(PrintfTaskHandle) : 0U;

    n = snprintf(log_buf,
                 sizeof(log_buf),
                 "adc_health: isr(h=%lu,f=%lu) task(h=%lu,f=%lu) pend=0x%02X ov=%lu stk_adc=%luW stk_printf=%luW\r\n",
                 (unsigned long)snapshot->isr_half,
                 (unsigned long)snapshot->isr_full,
                 (unsigned long)snapshot->task_half,
                 (unsigned long)snapshot->task_full,
                 (unsigned int)snapshot->pending_mask,
                 (unsigned long)snapshot->overrun,
                 (unsigned long)adc_stack_words,
                 (unsigned long)printf_stack_words);
    if (n > 0)
    {
        print_queue_send_log(log_buf);
    }
}

void app_adc_log_algo_1s(const app_signal_detect_status_t *sig)
{
    static uint32_t last_log_tick = 0U;
    uint32_t now_tick;
    char line[125];
    int n;

    if ((ADC_ALGO_LOG_ENABLE == 0U) || (sig == NULL))
    {
        return;
    }

    now_tick = osKernelGetTickCount();
    if ((last_log_tick != 0U) &&
        ((uint32_t)(now_tick - last_log_tick) < APP_ADC_LOG_PERIOD_MS))
    {
        return;
    }
    last_log_tick = now_tick;

    if (g_uart_mode != UART_MODE_LOG)
    {
        return;
    }

    n = snprintf(line,
                 sizeof(line),
                 "sig,s=%u,sc=%u,lk=%u,cp=%u,lo=%lu,est=%lu,k=%u/%u,v=%lu,p=%lu,n=%lu\r\n",
                 (unsigned)sig->stage,
                 (unsigned)sig->scanning,
                 (unsigned)sig->locked,
                 (unsigned)sig->carrier_present,
                 (unsigned long)sig->current_lo_hz,
                 (unsigned long)sig->estimated_carrier_hz,
                 (unsigned)(sig->step_index + 1U),
                 (unsigned)sig->step_count,
                 (unsigned long)sig->current_vpp_raw,
                 (unsigned long)sig->peak_vpp_raw,
                 (unsigned long)sig->valley_vpp_raw);
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
        print_queue_send_log(line);
    }
}

void app_adc_log_iq_amp_phase_1s(uint8_t tracking_active,
                                 const app_iq_preproc_result_t *result)
{
    static uint32_t last_log_tick = 0U;
    uint32_t now_tick;
    int32_t i_amp;
    int32_t q_amp;
    int32_t phase_mdeg;
    int32_t phase_mdeg_abs;
    float phase_rad;
    char line[180];
    int n;

    /* 判断：日志开关关闭或结果指针为空时直接退出，避免未锁定/未分析状态下访问无效数据。 */
    if ((ADC_IQ_PREPROC_LOG_ENABLE == 0U) || (result == NULL))
    {
        return;
    }

    /* 函数跳转：调用 osKernelGetTickCount()，读取 RTOS tick，用于限制该观测日志每秒只输出一次。 */
    now_tick = osKernelGetTickCount();
    /* 判断：距离上次输出不足 1 秒时直接退出，避免串口刷屏影响 ADC 实时性。 */
    if ((last_log_tick != 0U) &&
        ((uint32_t)(now_tick - last_log_tick) < APP_ADC_IQ_AMP_PHASE_LOG_MS))
    {
        return;
    }
    last_log_tick = now_tick;

    /* 判断：串口不是日志模式或频率锁定跟踪未打开时退出，保证只在频率锁定后输出 IQ 观测量。 */
    if ((g_uart_mode != UART_MODE_LOG) || (tracking_active == 0U))
    {
        return;
    }

    i_amp = (result->mean_i >= 0) ? result->mean_i : -result->mean_i;
    q_amp = (result->mean_q >= 0) ? result->mean_q : -result->mean_q;
    /* 函数跳转：调用 atan2f()，用去中心后的 Q/I 重心计算绝对相位，目标定义为 atan2(Q,I)。 */
    phase_rad = atan2f((float)result->mean_q, (float)result->mean_i);
    phase_mdeg = (int32_t)(phase_rad * APP_ADC_LOG_RAD_TO_MDEG);
    phase_mdeg_abs = (phase_mdeg >= 0) ? phase_mdeg : -phase_mdeg;

    /* 函数跳转：调用 snprintf()，把 I/Q 幅度和相位整理成一行串口日志。 */
    n = snprintf(line,
                 sizeof(line),
                 "iq_amp,i_amp=%ld,q_amp=%ld,i_mean=%ld,q_mean=%ld,mag=%lu,ph_urad=%ld,ph_mdeg=%ld,ph_deg=%s%ld.%03ld,blk=%lu\r\n",
                 (long)i_amp,
                 (long)q_amp,
                 (long)result->mean_i,
                 (long)result->mean_q,
                 (unsigned long)result->mean_mag,
                 (long)(phase_rad * 1000000.0f),
                 (long)phase_mdeg,
                 (phase_mdeg < 0) ? "-" : "",
                 (long)(phase_mdeg_abs / 1000),
                 (long)(phase_mdeg_abs % 1000),
                 (unsigned long)result->block_count);
    /* 判断：格式化成功且没有截断时才送入打印队列，避免输出半行日志干扰解析。 */
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
        /* 函数跳转：调用 print_queue_send_log()，把格式化后的日志文本送入打印队列，最终由串口任务输出。 */
        print_queue_send_log(line);
    }
}

/* 把闭环模式数值转换成短文本，便于串口日志直接判断当前是否相位接管。 */
static const char *app_adc_log_carrier_mode_name(uint8_t mode)
{
    /* 判断：当前仍由 residual_freq_millihz 驱动 VRFE。 */
    if (mode == APP_CARRIER_SYNC_MODE_FREQ)
    {
        return "freq";
    }

    /* 判断：频率已锁定，正在等待连续稳定计数达到相位接管门限。 */
    if (mode == APP_CARRIER_SYNC_MODE_PHASE_WAIT)
    {
        return "phase_wait";
    }

    /* 判断：相位环已经接管 VRFE 微调。 */
    if (mode == APP_CARRIER_SYNC_MODE_PHASE_LOCK)
    {
        return "phase_lock";
    }

    return "unknown";
}

void app_adc_log_carrier_sync_1s(const app_carrier_sync_status_t *status)
{
    static uint32_t last_log_tick = 0U;
    uint32_t now_tick;
    int32_t phase_mdeg_abs;
    char line[125];
    int n;

    /* 判断：日志开关关闭或状态指针为空时直接退出，避免访问无效状态。 */
    if ((ADC_IQ_PREPROC_LOG_ENABLE == 0U) || (status == NULL))
    {
        return;
    }

    /* 函数跳转：调用 osKernelGetTickCount()，读取 RTOS tick，用于限制 carrier 日志每秒输出一次。 */
    now_tick = osKernelGetTickCount();
    /* 判断：距离上次输出不足调试周期时退出，避免闭环调试日志挤占串口。 */
    if ((last_log_tick != 0U) &&
        ((uint32_t)(now_tick - last_log_tick) < APP_ADC_CARRIER_SYNC_LOG_MS))
    {
        return;
    }
    last_log_tick = now_tick;

    /* 判断：串口不是日志模式时不输出，避免影响 VOFA 数据流。 */
    if (g_uart_mode != UART_MODE_LOG)
    {
        return;
    }

    phase_mdeg_abs = (status->phase_error_mdeg >= 0) ? status->phase_error_mdeg : -status->phase_error_mdeg;

    /* 函数跳转：调用 snprintf()，输出第一条短日志，避免超过 print_msg_t 的 125 字节队列载荷。 */
    n = snprintf(line,
                 sizeof(line),
                 "car,m=%s,en=%u,lk=%u,pt=%u,pv=%u,pu=%u,rf=%ld,uv=%ld,dc=%u,st=%lu\r\n",
                 app_adc_log_carrier_mode_name(status->mode),
                 (unsigned)status->phase_lock_enabled,
                 (unsigned)status->locked_gate,
                 (unsigned)status->phase_takeover,
                 (unsigned)status->phase_valid,
                 (unsigned)status->phase_allow_update,
                 (long)status->residual_freq_millihz,
                 (long)status->control_uv,
                 (unsigned)status->dac_code,
                 (unsigned long)status->phase_stable_count);
    /* 判断：格式化成功且没有截断时才送入日志队列，避免串口出现半行数据。 */
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
        print_queue_send_log(line);
    }

    /* 函数跳转：调用 snprintf()，输出第二条短日志，集中给出相位误差和圆均值质量。 */
    n = snprintf(line,
                 sizeof(line),
                 "carp,deg=%s%ld.%03ld,tg=%ld,de=%ld,ctl=%ld,du=%ld,u=%lu,rel=%lu\r\n",
                 (status->phase_error_mdeg < 0) ? "-" : "",
                 (long)(phase_mdeg_abs / 1000),
                 (long)(phase_mdeg_abs % 1000),
                 (long)status->phase_target_mdeg,
                 (long)status->phase_delta_mdeg,
                 (long)status->phase_control_mdeg,
                 (long)status->phase_delta_uv,
                 (unsigned long)status->phase_used_count,
                 (unsigned long)status->phase_resultant_pm);
    /* 判断：格式化成功且没有截断时才送入日志队列，避免串口出现半行数据。 */
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
        print_queue_send_log(line);
    }

    /* 函数跳转：调用 snprintf()，输出第三条相位闭环调试日志，观察 residual 低通、近区限速和跨越制动次数。 */
    n = snprintf(line,
                 sizeof(line),
                 "carx,fl=%ld,lim=%lu,br=%lu,pc=%lu,fc=%lu\r\n",
                 (long)status->phase_freq_lpf_millihz,
                 (unsigned long)status->phase_slew_limit_uv,
                 (unsigned long)status->phase_brake_count,
                 (unsigned long)status->phase_update_count,
                 (unsigned long)status->freq_update_count);
    /* 判断：格式化成功且没有截断时才送入日志队列，避免串口出现半行数据。 */
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
        print_queue_send_log(line);
    }
}

void app_adc_log_iq_preproc_1s(uint8_t tracking_active,
                               const app_iq_preproc_result_t *result,
                               const app_adc_log_iq_verify_snapshot_t *verify)
{
    static uint32_t last_log_tick = 0U;
    uint32_t now_tick;
    char line[128];
    int n;

    if ((ADC_IQ_PREPROC_LOG_ENABLE == 0U) || (result == NULL))
    {
        return;
    }

    now_tick = osKernelGetTickCount();
    if ((last_log_tick != 0U) &&
        ((uint32_t)(now_tick - last_log_tick) < APP_ADC_IQ_PREPROC_LOG_MS))
    {
        return;
    }
    last_log_tick = now_tick;

    if ((g_uart_mode != UART_MODE_LOG) || (tracking_active == 0U))
    {
        return;
    }

    n = snprintf(line,
                 sizeof(line),
                 "iq_pre,mi=%ld,mq=%ld,mag=%lu,fo_mhz=%ld,ph_urad=%ld,blk=%lu\r\n",
                 (long)result->mean_i,
                 (long)result->mean_q,
                 (unsigned long)result->mean_mag,
                 (long)result->residual_freq_millihz,
                 (long)(result->phase_acc_rad * 1000000.0f),
                 (unsigned long)result->block_count);
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
        print_queue_send_log(line);
    }

    if ((verify != NULL) && (verify->valid != 0U))
    {
        n = snprintf(line,
                     sizeof(line),
                     "iq_rot,rd=%ld,cd=%ld,ip=%ld,rh=%lu,rt=%lu,ch=%lu,ct=%lu,ov=%lu,rb=%lu\r\n",
                     (long)verify->raw_dphi_urad,
                     (long)verify->rot_dphi_urad,
                     (long)verify->improve_pm,
                     (unsigned long)verify->raw_head_mag,
                     (unsigned long)verify->raw_tail_mag,
                     (unsigned long)verify->rot_head_mag,
                     (unsigned long)verify->rot_tail_mag,
                     (unsigned long)verify->overrun_snapshot,
                     (unsigned long)verify->rotate_block_cnt_snapshot);
        if ((n > 0) && ((size_t)n < sizeof(line)))
        {
            print_queue_send_log(line);
        }
    }
}

uint8_t app_adc_log_range_1s(const app_adc_log_range_snapshot_t *snapshot)
{
    static uint32_t last_log_tick = 0U;
    uint32_t now_tick;
    uint32_t i_vpp;
    uint32_t q_vpp;
    char line[200];
    int n;

    if ((ADC_RANGE_LOG_ENABLE == 0U) || (snapshot == NULL))
    {
        return 0U;
    }

    now_tick = osKernelGetTickCount();
    if ((last_log_tick != 0U) &&
        ((uint32_t)(now_tick - last_log_tick) < APP_ADC_LOG_PERIOD_MS))
    {
        return 0U;
    }
    last_log_tick = now_tick;

    if ((g_uart_mode != UART_MODE_LOG) || (snapshot->sample_cnt == 0U))
    {
        return 0U;
    }

    i_vpp = (snapshot->i_max > snapshot->i_min) ? ((uint32_t)snapshot->i_max - (uint32_t)snapshot->i_min) : 0U;
    q_vpp = (snapshot->q_max > snapshot->q_min) ? ((uint32_t)snapshot->q_max - (uint32_t)snapshot->q_min) : 0U;

    n = snprintf(line,
                 sizeof(line),
                 "adc_range,imin=%u,imax=%u,qmin=%u,qmax=%u,ivpp=%lu,qvpp=%lu,iclo=%lu,ichi=%lu,qclo=%lu,qchi=%lu,inlo=%lu,inhi=%lu,qnlo=%lu,qnhi=%lu,n=%lu\r\n",
                 (unsigned)snapshot->i_min,
                 (unsigned)snapshot->i_max,
                 (unsigned)snapshot->q_min,
                 (unsigned)snapshot->q_max,
                 (unsigned long)i_vpp,
                 (unsigned long)q_vpp,
                 (unsigned long)snapshot->i_clip_lo,
                 (unsigned long)snapshot->i_clip_hi,
                 (unsigned long)snapshot->q_clip_lo,
                 (unsigned long)snapshot->q_clip_hi,
                 (unsigned long)snapshot->i_near_lo,
                 (unsigned long)snapshot->i_near_hi,
                 (unsigned long)snapshot->q_near_lo,
                 (unsigned long)snapshot->q_near_hi,
                 (unsigned long)snapshot->sample_cnt);
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
        print_queue_send_log(line);
        return 1U;
    }

    return 0U;
}
