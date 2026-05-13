#include "app_adc_log.h"

#include <math.h>
#include <stdio.h>

#include "AppDebugConfig.h"
#include "FreeRTOS.h"
#include "RtosTypes.h"
#include "cmsis_os2.h"
#include "task.h"

#define APP_ADC_LOG_PERIOD_MS        1000U
#define APP_ADC_IQ_PREPROC_LOG_MS    1000U

/* 宏定义说明：APP_ADC_CARRIER_SYNC_LOG_MS = 1000U；当前只保留频率闭环和相位观测，carrier 日志恢复为 1s 输出。 */
#ifndef APP_ADC_CARRIER_SYNC_LOG_MS
#define APP_ADC_CARRIER_SYNC_LOG_MS 1000U
#endif

/* 宏定义说明：APP_ADC_IQ_AMP_PHASE_LOG_MS = 1000U；锁定后 I/Q 幅度和绝对相位日志的最小输出周期。 */
#define APP_ADC_IQ_AMP_PHASE_LOG_MS 1000U

/* 宏定义说明：APP_ADC_LOG_RAD_TO_MDEG = 57295.7795f；弧度转毫度换算系数。 */
#define APP_ADC_LOG_RAD_TO_MDEG 57295.7795f

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

    if ((ADC_IQ_PREPROC_LOG_ENABLE == 0U) || (result == NULL))
    {
        return;
    }

    now_tick = osKernelGetTickCount();
    if ((last_log_tick != 0U) &&
        ((uint32_t)(now_tick - last_log_tick) < APP_ADC_IQ_AMP_PHASE_LOG_MS))
    {
        return;
    }
    last_log_tick = now_tick;

    if ((g_uart_mode != UART_MODE_LOG) || (tracking_active == 0U))
    {
        return;
    }

    i_amp = (result->mean_i >= 0) ? result->mean_i : -result->mean_i;
    q_amp = (result->mean_q >= 0) ? result->mean_q : -result->mean_q;
    phase_rad = atan2f((float)result->mean_q, (float)result->mean_i);
    phase_mdeg = (int32_t)(phase_rad * APP_ADC_LOG_RAD_TO_MDEG);
    phase_mdeg_abs = (phase_mdeg >= 0) ? phase_mdeg : -phase_mdeg;

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
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
        print_queue_send_log(line);
    }
}

/* 把载波同步模式转成短文本，便于串口快速观察。 */
static const char *app_adc_log_carrier_mode_name(uint8_t mode)
{
    if (mode == APP_CARRIER_SYNC_MODE_FREQ)
    {
        return "freq";
    }

    return "unknown";
}

void app_adc_log_carrier_sync_1s(const app_carrier_sync_status_t *status)
{
    static uint32_t last_log_tick = 0U;
    uint32_t now_tick;
    int32_t phase_mdeg_abs;
    int32_t phase_abs_mdeg_abs;
    char line[125];
    int n;

    if ((ADC_IQ_PREPROC_LOG_ENABLE == 0U) || (status == NULL))
    {
        return;
    }

    now_tick = osKernelGetTickCount();
    if ((last_log_tick != 0U) &&
        ((uint32_t)(now_tick - last_log_tick) < APP_ADC_CARRIER_SYNC_LOG_MS))
    {
        return;
    }
    last_log_tick = now_tick;

    if (g_uart_mode != UART_MODE_LOG)
    {
        return;
    }

    phase_mdeg_abs = (status->phase_error_mdeg >= 0) ? status->phase_error_mdeg : -status->phase_error_mdeg;
    phase_abs_mdeg_abs = (status->phase_abs_mdeg >= 0) ? status->phase_abs_mdeg : -status->phase_abs_mdeg;

    n = snprintf(line,
                 sizeof(line),
                 "car,m=%s,lk=%u,rf=%ld,uv=%ld,dc=%u,dcd=%d,chg=%u,fc=%lu,hc=%lu\r\n",
                 app_adc_log_carrier_mode_name(status->mode),
                 (unsigned)status->locked_gate,
                 (long)status->residual_freq_millihz,
                 (long)status->control_uv,
                 (unsigned)status->dac_code,
                 (int)status->dac_code_delta,
                 (unsigned)status->dac_code_changed,
                 (unsigned long)status->freq_update_count,
                 (unsigned long)status->hold_count);
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
        print_queue_send_log(line);
    }

    n = snprintf(line,
                 sizeof(line),
                 "carp,pv=%u,ph=%s%ld.%03ld,er=%s%ld.%03ld,u=%lu,rel=%lu,le=%ld\r\n",
                 (unsigned)status->phase_valid,
                 (status->phase_abs_mdeg < 0) ? "-" : "",
                 (long)(phase_abs_mdeg_abs / 1000),
                 (long)(phase_abs_mdeg_abs % 1000),
                 (status->phase_error_mdeg < 0) ? "-" : "",
                 (long)(phase_mdeg_abs / 1000),
                 (long)(phase_mdeg_abs % 1000),
                 (unsigned long)status->phase_used_count,
                 (unsigned long)status->phase_resultant_pm,
                 (long)status->last_error);
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
