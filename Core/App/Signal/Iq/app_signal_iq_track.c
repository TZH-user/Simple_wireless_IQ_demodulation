#include "app_signal_iq_track.h"

#include <math.h>
#include <string.h>

#include "adc.h"
#include "cmsis_os2.h"

/* IQ 预处理验证日志的最小更新周期，单位 ms。 */
#define IQ_PREPROC_LOG_PERIOD_MS 1000U

/* 边缘验证窗口长度，单位为采样点数。 */
#define IQ_PREPROC_VERIFY_WINDOW_SAMPLES 32U

#if (ADC_IQ_PREPROC_ENABLE != 0U)
/* IQ 预处理上下文，保存跨 block 的频偏跟踪状态。 */
static app_iq_preproc_ctx_t g_iq_track_ctx;
#if (ADC_IQ_PREPROC_ROTATE_RUNTIME_ENABLE != 0U)
/* 运行时旋转模式下的 I 路输出缓冲区。 */
static uint16_t g_iq_track_i_buf[ADC_BLOCK_N];
/* 运行时旋转模式下的 Q 路输出缓冲区。 */
static uint16_t g_iq_track_q_buf[ADC_BLOCK_N];
#endif
/* IQ 跟踪快照。 */
static app_signal_iq_track_snapshot_t g_iq_track_snapshot;
/* 最近一次验证快照的输出时间。 */
static uint32_t g_iq_track_last_verify_tick = 0U;
#else
static app_signal_iq_track_snapshot_t g_iq_track_snapshot;
#endif

#if (ADC_IQ_PREPROC_ENABLE != 0U)
/* 将相位限制到 [-pi, pi]，避免跨越边界时出现相位突跳。 */
static float app_signal_iq_track_wrap_pi(float angle)
{
    const float pi = 3.14159265359f;
    const float two_pi = 6.28318530718f;

    while (angle > pi)
    {
        angle -= two_pi;
    }

    while (angle < -pi)
    {
        angle += two_pi;
    }

    return angle;
}

/* 计算一段原始 IQ 样本的平均相位和平均矢量幅度。 */
static uint8_t app_signal_iq_track_calc_edge_phase_u16(const uint16_t *i_buf,
                                                       const uint16_t *q_buf,
                                                       uint32_t start_index,
                                                       uint32_t sample_cnt,
                                                       uint16_t adc_mid,
                                                       float *phase_out,
                                                       uint32_t *mag_out)
{
    int64_t sum_i = 0;
    int64_t sum_q = 0;

    /* 判断输入缓冲区和输出指针是否有效。 */
    if ((i_buf == NULL) || (q_buf == NULL) || (phase_out == NULL) || (mag_out == NULL))
    {
        return 0U;
    }

    for (uint32_t idx = 0U; idx < sample_cnt; idx++)
    {
        sum_i += (int32_t)i_buf[start_index + idx] - (int32_t)adc_mid;
        sum_q += (int32_t)q_buf[start_index + idx] - (int32_t)adc_mid;
    }

    /* 判断该窗口内是否没有有效矢量。 */
    if ((sum_i == 0) && (sum_q == 0))
    {
        *phase_out = 0.0f;
        *mag_out = 0U;
        return 0U;
    }

    *phase_out = atan2f((float)sum_q, (float)sum_i);
    *mag_out = (uint32_t)sqrtf((float)((sum_i * sum_i) + (sum_q * sum_q)));
    return 1U;
}

/* 按当前残余频偏估计做虚拟反旋转，再计算窗口平均相位。 */
static uint8_t app_signal_iq_track_calc_edge_phase_virtual_rot_u16(const app_iq_preproc_ctx_t *ctx,
                                                                   const uint16_t *i_buf,
                                                                   const uint16_t *q_buf,
                                                                   uint32_t start_index,
                                                                   uint32_t sample_cnt,
                                                                   float phase0,
                                                                   float phase_step,
                                                                   float *phase_out,
                                                                   uint32_t *mag_out)
{
    float c;
    float s;
    float c_step;
    float s_step;
    float sum_i = 0.0f;
    float sum_q = 0.0f;

    /* 判断上下文、输入缓冲区和输出指针是否有效。 */
    if ((ctx == NULL) || (i_buf == NULL) || (q_buf == NULL) || (phase_out == NULL) || (mag_out == NULL))
    {
        return 0U;
    }

    c = cosf(phase0 + ((float)start_index * phase_step));
    s = sinf(phase0 + ((float)start_index * phase_step));
    c_step = cosf(phase_step);
    s_step = sinf(phase_step);

    for (uint32_t idx = 0U; idx < sample_cnt; idx++)
    {
        float i = (float)((int32_t)i_buf[start_index + idx] - (int32_t)ctx->adc_mid);
        float q = (float)((int32_t)q_buf[start_index + idx] - (int32_t)ctx->adc_mid);
        float rot_i = (c * i) + (s * q);
        float rot_q = (-s * i) + (c * q);
        float next_c = (c * c_step) - (s * s_step);
        float next_s = (s * c_step) + (c * s_step);

        sum_i += rot_i;
        sum_q += rot_q;
        c = next_c;
        s = next_s;
    }

    /* 判断旋转后的窗口是否仍然没有有效矢量。 */
    if ((sum_i == 0.0f) && (sum_q == 0.0f))
    {
        *phase_out = 0.0f;
        *mag_out = 0U;
        return 0U;
    }

    *phase_out = atan2f(sum_q, sum_i);
    *mag_out = (uint32_t)(sqrtf((sum_i * sum_i) + (sum_q * sum_q)) + 0.5f);
    return 1U;
}

/* 生成 IQ 预处理验证快照，用于观察旋转前后的边缘相位差。 */
static void app_signal_iq_track_update_verify(const uint16_t *raw_i_buf,
                                              const uint16_t *raw_q_buf,
                                              const app_iq_preproc_ctx_t *ctx,
                                              uint32_t sample_cnt)
{
    const uint32_t edge_n = IQ_PREPROC_VERIFY_WINDOW_SAMPLES;
    float raw_head_phase;
    float raw_tail_phase;
    float rot_head_phase;
    float rot_tail_phase;
    uint32_t raw_head_mag;
    uint32_t raw_tail_mag;
    uint32_t rot_head_mag;
    uint32_t rot_tail_mag;
    float raw_dphi;
    float rot_dphi;
    float phase0;
    float phase_step;
    int32_t raw_abs;

    g_iq_track_snapshot.verify.valid = 0U;

    /* 判断上下文和样本长度是否满足验证窗口要求。 */
    if ((ctx == NULL) || (sample_cnt < (edge_n * 2U)))
    {
        return;
    }

    phase_step = ctx->residual_rad_per_sample;
    phase0 = app_signal_iq_track_wrap_pi(ctx->phase_acc_rad - (phase_step * (float)sample_cnt));

    /* 判断头尾窗口和虚拟旋转后的头尾窗口是否都能得到有效相位。 */
    if ((app_signal_iq_track_calc_edge_phase_u16(raw_i_buf, raw_q_buf, 0U, edge_n, ctx->adc_mid, &raw_head_phase, &raw_head_mag) == 0U) ||
        (app_signal_iq_track_calc_edge_phase_u16(raw_i_buf, raw_q_buf, sample_cnt - edge_n, edge_n, ctx->adc_mid, &raw_tail_phase, &raw_tail_mag) == 0U) ||
        (app_signal_iq_track_calc_edge_phase_virtual_rot_u16(ctx, raw_i_buf, raw_q_buf, 0U, edge_n, phase0, phase_step, &rot_head_phase, &rot_head_mag) == 0U) ||
        (app_signal_iq_track_calc_edge_phase_virtual_rot_u16(ctx, raw_i_buf, raw_q_buf, sample_cnt - edge_n, edge_n, phase0, phase_step, &rot_tail_phase, &rot_tail_mag) == 0U))
    {
        return;
    }

    raw_dphi = app_signal_iq_track_wrap_pi(raw_tail_phase - raw_head_phase);
    rot_dphi = app_signal_iq_track_wrap_pi(rot_tail_phase - rot_head_phase);

    g_iq_track_snapshot.verify.raw_dphi_urad = (int32_t)(raw_dphi * 1000000.0f);
    g_iq_track_snapshot.verify.rot_dphi_urad = (int32_t)(rot_dphi * 1000000.0f);
    g_iq_track_snapshot.verify.raw_head_mag = raw_head_mag;
    g_iq_track_snapshot.verify.raw_tail_mag = raw_tail_mag;
    g_iq_track_snapshot.verify.rot_head_mag = rot_head_mag;
    g_iq_track_snapshot.verify.rot_tail_mag = rot_tail_mag;
    g_iq_track_snapshot.verify.overrun_snapshot = adc_overrun_cnt;
    g_iq_track_snapshot.verify.rotate_block_cnt_snapshot = app_iq_preproc_rotate_block_cnt;

    raw_abs = (g_iq_track_snapshot.verify.raw_dphi_urad >= 0) ?
              g_iq_track_snapshot.verify.raw_dphi_urad :
              -g_iq_track_snapshot.verify.raw_dphi_urad;

    /* 判断原始相位差是否非零，只有这样才能计算改善比例。 */
    if (raw_abs > 0)
    {
        int32_t rot_abs = (g_iq_track_snapshot.verify.rot_dphi_urad >= 0) ?
                          g_iq_track_snapshot.verify.rot_dphi_urad :
                          -g_iq_track_snapshot.verify.rot_dphi_urad;
        g_iq_track_snapshot.verify.improve_pm =
            (int32_t)(((int64_t)(raw_abs - rot_abs) * 1000LL) / (int64_t)raw_abs);
    }
    else
    {
        g_iq_track_snapshot.verify.improve_pm = 0;
    }

    g_iq_track_snapshot.verify.valid = 1U;
}
#endif

void app_signal_iq_track_init(uint32_t sample_rate_hz)
{
    memset(&g_iq_track_snapshot, 0, sizeof(g_iq_track_snapshot));
#if (ADC_IQ_PREPROC_ENABLE != 0U)
    app_iq_preproc_init(&g_iq_track_ctx, sample_rate_hz);
    g_iq_track_snapshot.adc_mid = g_iq_track_ctx.adc_mid;
    g_iq_track_last_verify_tick = 0U;
#else
    (void)sample_rate_hz;
#endif
}

void app_signal_iq_track_reset(void)
{
    memset(&g_iq_track_snapshot, 0, sizeof(g_iq_track_snapshot));
#if (ADC_IQ_PREPROC_ENABLE != 0U)
    app_iq_preproc_reset(&g_iq_track_ctx);
    g_iq_track_snapshot.adc_mid = g_iq_track_ctx.adc_mid;
    g_iq_track_last_verify_tick = 0U;
#endif
}

uint8_t app_signal_iq_track_process_locked_block(const uint16_t *i_buf,
                                                 const uint16_t *q_buf,
                                                 uint32_t sample_cnt)
{
#if (ADC_IQ_PREPROC_ENABLE != 0U)
    uint8_t analyzed = 0U;

    /* 判断输入缓冲区是否有效。 */
    if ((i_buf == NULL) || (q_buf == NULL) || (sample_cnt == 0U))
    {
        g_iq_track_snapshot.result_valid = 0U;
        return 0U;
    }

    /* 判断当前是否刚进入锁定阶段，若是则先标记跟踪已激活。 */
    if (g_iq_track_snapshot.active == 0U)
    {
        app_iq_preproc_reset(&g_iq_track_ctx);
        memset(&g_iq_track_snapshot.result, 0, sizeof(g_iq_track_snapshot.result));
        memset(&g_iq_track_snapshot.verify, 0, sizeof(g_iq_track_snapshot.verify));
        g_iq_track_snapshot.active = 1U;
    }

#if (ADC_IQ_PREPROC_ROTATE_RUNTIME_ENABLE != 0U)
    analyzed = app_iq_preproc_rotate_block_u16(&g_iq_track_ctx,
                                               i_buf,
                                               q_buf,
                                               sample_cnt,
                                               g_iq_track_i_buf,
                                               g_iq_track_q_buf,
                                               &g_iq_track_snapshot.result);
#else
    analyzed = app_iq_preproc_analyze_block_u16(&g_iq_track_ctx,
                                                i_buf,
                                                q_buf,
                                                sample_cnt,
                                                &g_iq_track_snapshot.result);
#endif

    g_iq_track_snapshot.adc_mid = g_iq_track_ctx.adc_mid;
    g_iq_track_snapshot.result_valid = analyzed;

    /* 判断当前 block 是否已经得到有效分析结果。 */
    if (analyzed != 0U)
    {
        uint32_t now_tick = osKernelGetTickCount();

        /* 判断是否到达下一次验证快照更新时间。 */
        if ((uint32_t)(now_tick - g_iq_track_last_verify_tick) >= IQ_PREPROC_LOG_PERIOD_MS)
        {
            app_signal_iq_track_update_verify(i_buf, q_buf, &g_iq_track_ctx, sample_cnt);
            g_iq_track_last_verify_tick = now_tick;
        }
    }

    return analyzed;
#else
    (void)i_buf;
    (void)q_buf;
    (void)sample_cnt;
    g_iq_track_snapshot.result_valid = 0U;
    return 0U;
#endif
}

void app_signal_iq_track_get_snapshot(app_signal_iq_track_snapshot_t *out)
{
    /* 判断输出指针是否有效。 */
    if (out == NULL)
    {
        return;
    }

    *out = g_iq_track_snapshot;
}
