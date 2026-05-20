#include "Analyze.h"
#include <stdint.h>
#include <stdio.h>
#include "RtosTypes.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "arm_math.h"
#include "arm_const_structs.h"
#include <string.h>
#include <math.h>

#define ANALYZE_DEPTH_BLOCKS 4U
#define ANALYZE_FFT_N 4096U
#define ANALYZE_FFT_HALF_N (ANALYZE_FFT_N / 2U)
#define ANALYZE_SAMPLE_RATE_HZ 2048000U /* FFT 频率换算使用的 ADC 采样率，单位 Hz。 */
#define ANALYZE_PI_F 3.14159265358979323846f
#define ANALYZE_TWO_PI_F (2.0f * ANALYZE_PI_F)

#define ANALYZE_IQ_SPECTRUM_ENABLE 1U /* 是否计算 IQ 复数谱。 */
#define ANALYZE_ENV_SPECTRUM_ENABLE 1U /* 是否计算包络谱。 */
#define ANALYZE_PHASE_SPECTRUM_ENABLE 1U /* 是否计算相位展开谱。 */
#define ANALYZE_LOG_ENABLE U /* 算法日志总控；置 0 后本文件不向打印队列投递日志。 */
#define ANALYZE_SPECTRUM_SUMMARY_LOG_ENABLE 0U /* 是否输出每类谱的主峰摘要。 */
#define ANALYZE_IQ_SPECTRUM_FULL_LOG_ENABLE 1U /* 是否逐 bin 输出 IQ 复数谱。 */
#define ANALYZE_ENV_SPECTRUM_FULL_LOG_ENABLE 1U /* 是否逐 bin 输出包络谱。 */
#define ANALYZE_PHASE_SPECTRUM_FULL_LOG_ENABLE 1U /* 是否逐 bin 输出相位展开谱。 */
#define ANALYZE_START_LOG_ENABLE 1U /* 是否输出分析启动日志。 */
#define ANALYZE_DEPTH_LOG_ENABLE 1U /* 是否输出每块包络深度与平均深度日志。 */
#define ANALYZE_RESULT_LOG_ENABLE 1U /* 是否输出一次性分析完成日志。 */

static uint16_t g_analyze_i_raw[ANALYZE_FFT_N];
static uint16_t g_analyze_q_raw[ANALYZE_FFT_N];

typedef struct
{
    uint32_t bin;
    uint32_t freq_hz;
    float mag;
    float phase;
} analyze_spectrum_peak_t;


typedef struct
{
    uint8_t active;         /* active == 0 && done == 0：表示还没开始分析 */
    uint8_t done;           /* active == 0 && done == 1：表示已经分析完成 */
    uint8_t settle_left;    /* active == 1 && done == 0：表示正在分析 */
    uint8_t vote_count;
    uint32_t center_hz;
    analyze_mode_t mode;
    uint32_t mod_hz;
    uint32_t depth_pm;
    uint32_t depth_sum_pm;
} analyze_state_t;

typedef struct
{
    uint8_t result_pending;    /* 分析完成结果日志等待发送。 */
    uint8_t depth_avg_pending; /* 平均包络深度日志等待发送。 */
    uint8_t summary_stage;     /* 频谱摘要慢速发送阶段：0空闲，1 IQ，2 包络，3 相位。 */
    uint8_t full_stage;        /* 全量频谱慢速发送阶段：0空闲，1 IQ，2 包络，3 相位。 */
    uint8_t format_pending;    /* 频谱输出完成后的格式说明日志等待发送。 */
    uint32_t full_bin;         /* 当前全量频谱发送到的 bin。 */
} analyze_log_state_t;

#define ANALYZE_SETTLE_BLOCKS 2U
#define ANALYZE_SETTLE_LOG_ENABLE 1U

static analyze_state_t g_analyze;
static analyze_log_state_t g_analyze_log;
static uint8_t g_fft_ready;
static analyze_spectrum_peak_t g_iq_peak;
static analyze_spectrum_peak_t g_env_peak;
static analyze_spectrum_peak_t g_phase_peak;

static arm_rfft_fast_instance_f32 g_env_fft_inst;
static arm_rfft_fast_instance_f32 g_phase_fft_inst;

__attribute__((section(".ram_d1_buffer")))
static float g_i_buf[ANALYZE_FFT_N];
__attribute__((section(".ram_d1_buffer")))
static float g_q_buf[ANALYZE_FFT_N];
__attribute__((section(".ram_d1_buffer")))
static float g_env_buf[ANALYZE_FFT_N];
__attribute__((section(".ram_d1_buffer")))
static float g_phase_buf[ANALYZE_FFT_N];
__attribute__((section(".ram_d1_buffer")))
static float g_phase_unwrap_buf[ANALYZE_FFT_N];
__attribute__((section(".ram_d1_buffer")))
static float g_iq_cfft_buf[ANALYZE_FFT_N * 2U];
__attribute__((section(".ram_d1_buffer")))
static float g_env_rfft_buf[ANALYZE_FFT_N];
__attribute__((section(".ram_d1_buffer")))
static float g_phase_rfft_buf[ANALYZE_FFT_N];
__attribute__((section(".ram_d1_buffer")))
static float g_iq_spec_mag[ANALYZE_FFT_N];
__attribute__((section(".ram_d1_buffer")))
static float g_iq_spec_phase[ANALYZE_FFT_N];
__attribute__((section(".ram_d1_buffer")))
static float g_env_spec_mag[ANALYZE_FFT_HALF_N];
__attribute__((section(".ram_d1_buffer")))
static float g_env_spec_phase[ANALYZE_FFT_HALF_N];
__attribute__((section(".ram_d1_buffer")))
static float g_phase_spec_mag[ANALYZE_FFT_HALF_N];
__attribute__((section(".ram_d1_buffer")))
static float g_phase_spec_phase[ANALYZE_FFT_HALF_N];

static float cumsum_mean(const uint16_t *buf, uint32_t sample_cnt, uint8_t step)
{
    float mean = 0.0f;
    uint32_t count = 0U;

    if ((buf == NULL) || (sample_cnt == 0U) || (step == 0U))    return 0.0f;

    for (uint32_t k = 0; k < sample_cnt; k += step)
    {
        mean += (float)buf[k];
        count++;
    }

    if (count == 0U) return 0.0f;

    return mean / (float)count;
}

static uint8_t analyze_fft_init_once(void)
{
    if (g_fft_ready != 0U)
    {
        return 1U;
    }

    if (arm_rfft_fast_init_f32(&g_env_fft_inst, ANALYZE_FFT_N) != ARM_MATH_SUCCESS)
    {
        return 0U;
    }

    if (arm_rfft_fast_init_f32(&g_phase_fft_inst, ANALYZE_FFT_N) != ARM_MATH_SUCCESS)
    {
        return 0U;
    }

    g_fft_ready = 1U;
    return 1U;
}

static uint32_t analyze_bin_to_hz(uint32_t bin)
{
    return (uint32_t)((((uint64_t)bin * ANALYZE_SAMPLE_RATE_HZ) + (ANALYZE_FFT_N / 2U)) / ANALYZE_FFT_N);
}

static uint32_t analyze_mag_to_u32(float mag)
{
    if (mag <= 0.0f)
    {
        return 0U;
    }

    return (uint32_t)(mag + 0.5f);
}

static void analyze_log_send(const char *text)
{
#if (ANALYZE_LOG_ENABLE != 0U)
    print_queue_send(text);
#else
    (void)text;
#endif
}

static int32_t analyze_phase_to_mrad(float phase)
{
    if (phase >= 0.0f)
    {
        return (int32_t)((phase * 1000.0f) + 0.5f);
    }

    return (int32_t)((phase * 1000.0f) - 0.5f);
}

static void analyze_log_spectrum_point(uint8_t spec_id,
                                       uint32_t freq_hz,
                                       float mag,
                                       float phase,
                                       uint32_t bin)
{
    char log_buf[96];
    int32_t phase_mrad = analyze_phase_to_mrad(phase);
    uint32_t phase_abs = (phase_mrad < 0) ? (uint32_t)(-phase_mrad) : (uint32_t)phase_mrad;
    int n = snprintf(log_buf,
                     sizeof(log_buf),
                     "spec:%lu,%lu,%s%lu.%03lu,%lu,%u\r\n",
                     (unsigned long)freq_hz,
                     (unsigned long)analyze_mag_to_u32(mag),
                     (phase_mrad < 0) ? "-" : "",
                     (unsigned long)(phase_abs / 1000U),
                     (unsigned long)(phase_abs % 1000U),
                     (unsigned long)bin,
                     (unsigned int)spec_id);

    if ((n > 0) && ((size_t)n < sizeof(log_buf)))
    {
        analyze_log_send(log_buf);
    }
}

static void analyze_log_spectrum_summary(uint8_t spec_id, const analyze_spectrum_peak_t *peak)
{
    if (peak == NULL)
    {
        return;
    }

    analyze_log_spectrum_point(spec_id, peak->freq_hz, peak->mag, peak->phase, peak->bin);
}

static void analyze_log_spectrum_format_line(void)
{
    analyze_log_send("format:ch0=freq_hz,ch1=mag,ch2=phase_rad,ch3=bin,ch4=spec_id(1=iq,2=env,3=phase)\r\n");
}

static void analyze_log_result(void)
{
    char log_buf[128];
    int n = snprintf(log_buf,
                     sizeof(log_buf),
                     "analyze: calculation complete center=%luHz mod=%luHz depth=%lupm\r\n",
                     (unsigned long)g_analyze.center_hz,
                     (unsigned long)g_analyze.mod_hz,
                     (unsigned long)g_analyze.depth_pm);

    if ((n > 0) && ((size_t)n < sizeof(log_buf)))
    {
        analyze_log_send(log_buf);
    }
}

static void analyze_log_depth_avg(void)
{
    char log_buf[96];
    int n = snprintf(log_buf,
                     sizeof(log_buf),
                     "analyze: depth_avg=%lupm\r\n",
                     (unsigned long)g_analyze.depth_pm);

    if ((n > 0) && ((size_t)n < sizeof(log_buf)))
    {
        analyze_log_send(log_buf);
    }
}

static void analyze_update_peak(analyze_spectrum_peak_t *peak,
                                uint32_t bin,
                                float mag,
                                float phase)
{
    if ((peak == NULL) || (bin == 0U))
    {
        return;
    }

    if ((peak->bin == 0U) || (mag > peak->mag))
    {
        peak->bin = bin;
        peak->freq_hz = analyze_bin_to_hz(bin);
        peak->mag = mag;
        peak->phase = phase;
    }
}

static void analyze_calc_mag_phase(float re, float im, float *mag_out, float *phase_out)
{
    float mag = 0.0f;
    float phase = 0.0f;

    (void)arm_sqrt_f32((re * re) + (im * im), &mag);
    phase = atan2f(im, re);

    if (mag_out != NULL)
    {
        *mag_out = mag;
    }

    if (phase_out != NULL)
    {
        *phase_out = phase;
    }
}

static uint32_t analyze_fill_env_buffer(const uint16_t *i_buf,const uint16_t *q_buf,uint32_t sample_cnt)
{
    float i_mean = 0.0f;
    float q_mean = 0.0f;
    float env_mean = 0.0f;
    float phase_mean = 0.0f;
    float env_min = 0.0f;
    float env_max = 0.0f;
    float phase_offset = 0.0f;

    if ((i_buf == NULL) || (q_buf == NULL) || (sample_cnt < ANALYZE_FFT_N))
    {
        return 0U;
    }

    i_mean = cumsum_mean(i_buf,ANALYZE_FFT_N,1);
    q_mean = cumsum_mean(q_buf,ANALYZE_FFT_N,1);

    for (uint32_t k = 0; k < ANALYZE_FFT_N; k++)
    {
        float i = (float)i_buf[k] - i_mean;
        float q = (float)q_buf[k] - q_mean;
        float env = sqrtf((i * i) + (q * q));
        float phase = atan2f(q, i);

        g_i_buf[k] = i;
        g_q_buf[k] = q;
        g_env_buf[k] = env;
        g_phase_buf[k] = phase;

        if (k == 0U)
        {
            g_phase_unwrap_buf[k] = phase;
        }
        else
        {
            float delta = phase - g_phase_buf[k - 1U];

            if (delta > (ANALYZE_PI_F * 0.8f))
            {
                phase_offset -= ANALYZE_TWO_PI_F;
            }
            else if (delta < (-ANALYZE_PI_F * 0.8f))
            {
                phase_offset += ANALYZE_TWO_PI_F;
            }

            g_phase_unwrap_buf[k] = phase + phase_offset;
        }

        env_mean += env;
        phase_mean += g_phase_unwrap_buf[k];

        if (k == 0U)
        {
            env_min = env;
            env_max = env;
        }
        else
        {
            if (env < env_min) env_min = env;
            if (env > env_max) env_max = env;
        }
    }

    env_mean /= (float)ANALYZE_FFT_N;
    phase_mean /= (float)ANALYZE_FFT_N;

    for (uint32_t k = 0; k < ANALYZE_FFT_N; k++)
    {
        g_env_buf[k] -= env_mean;
        g_phase_unwrap_buf[k] -= phase_mean;
    }

    if ((env_max + env_min) <= 1.0f)
    {
        return 0U;
    }

    return (uint32_t)(((env_max - env_min) * 1000.0f) / (env_max + env_min));
}

static analyze_spectrum_peak_t analyze_process_iq_spectrum(void)
{
    analyze_spectrum_peak_t peak = {0U, 0U, 0.0f, 0.0f};

    for (uint32_t k = 0; k < ANALYZE_FFT_N; k++)
    {
        g_iq_cfft_buf[2U * k] = g_i_buf[k];
        g_iq_cfft_buf[(2U * k) + 1U] = g_q_buf[k];
    }

    arm_cfft_f32(&arm_cfft_sR_f32_len4096, g_iq_cfft_buf, 0, 1);

    for (uint32_t k = 0; k < ANALYZE_FFT_N; k++)
    {
        float mag = 0.0f;
        float phase = 0.0f;
        float re = g_iq_cfft_buf[2U * k];
        float im = g_iq_cfft_buf[(2U * k) + 1U];

        analyze_calc_mag_phase(re, im, &mag, &phase);
        g_iq_spec_mag[k] = mag;
        g_iq_spec_phase[k] = phase;
        analyze_update_peak(&peak, k, mag, phase);

    }

    return peak;
}

static analyze_spectrum_peak_t analyze_process_rfft_spectrum(const char *tag,
                                                             arm_rfft_fast_instance_f32 *fft_inst,
                                                             float *input_buf,
                                                             float *fft_buf,
                                                             float *mag_buf,
                                                             float *phase_buf)
{
    analyze_spectrum_peak_t peak = {0U, 0U, 0.0f, 0.0f};

    if ((tag == NULL) || (fft_inst == NULL) || (input_buf == NULL) ||
        (fft_buf == NULL) || (mag_buf == NULL) || (phase_buf == NULL))
    {
        return peak;
    }

    arm_rfft_fast_f32(fft_inst, input_buf, fft_buf, 0);

    for (uint32_t k = 0; k < ANALYZE_FFT_HALF_N; k++)
    {
        float re;
        float im;
        float mag = 0.0f;
        float phase = 0.0f;

        if (k == 0U)
        {
            re = fft_buf[0U];
            im = 0.0f;
        }
        else
        {
            re = fft_buf[2U * k];
            im = fft_buf[(2U * k) + 1U];
        }

        analyze_calc_mag_phase(re, im, &mag, &phase);
        mag_buf[k] = mag;
        phase_buf[k] = phase;
        analyze_update_peak(&peak, k, mag, phase);

    }

    return peak;
}

static void analyze_process_spectra(void)
{
    if (analyze_fft_init_once() == 0U)
    {
        analyze_log_send("analyze: fft init failed\r\n");
        return;
    }

#if (ANALYZE_IQ_SPECTRUM_ENABLE != 0U)
    {
        g_iq_peak = analyze_process_iq_spectrum();
    }
#endif

#if (ANALYZE_ENV_SPECTRUM_ENABLE != 0U)
    {
        g_env_peak = analyze_process_rfft_spectrum("env",
                                                   &g_env_fft_inst,
                                                   g_env_buf,
                                                   g_env_rfft_buf,
                                                   g_env_spec_mag,
                                                   g_env_spec_phase);
        g_analyze.mod_hz = g_env_peak.freq_hz;
    }
#endif

#if (ANALYZE_PHASE_SPECTRUM_ENABLE != 0U)
    {
        g_phase_peak = analyze_process_rfft_spectrum("phase",
                                                     &g_phase_fft_inst,
                                                     g_phase_unwrap_buf,
                                                     g_phase_rfft_buf,
                                                     g_phase_spec_mag,
                                                     g_phase_spec_phase);
    }
#endif
}

static uint8_t analyze_summary_stage_enabled(uint8_t stage)
{
    switch (stage)
    {
    case 1U:
#if (ANALYZE_IQ_SPECTRUM_ENABLE != 0U)
        return 1U;
#else
        return 0U;
#endif
    case 2U:
#if (ANALYZE_ENV_SPECTRUM_ENABLE != 0U)
        return 1U;
#else
        return 0U;
#endif
    case 3U:
#if (ANALYZE_PHASE_SPECTRUM_ENABLE != 0U)
        return 1U;
#else
        return 0U;
#endif
    default:
        return 0U;
    }
}

static uint8_t analyze_full_stage_enabled(uint8_t stage)
{
    switch (stage)
    {
    case 1U:
#if ((ANALYZE_IQ_SPECTRUM_ENABLE != 0U) && (ANALYZE_IQ_SPECTRUM_FULL_LOG_ENABLE != 0U))
        return 1U;
#else
        return 0U;
#endif
    case 2U:
#if ((ANALYZE_ENV_SPECTRUM_ENABLE != 0U) && (ANALYZE_ENV_SPECTRUM_FULL_LOG_ENABLE != 0U))
        return 1U;
#else
        return 0U;
#endif
    case 3U:
#if ((ANALYZE_PHASE_SPECTRUM_ENABLE != 0U) && (ANALYZE_PHASE_SPECTRUM_FULL_LOG_ENABLE != 0U))
        return 1U;
#else
        return 0U;
#endif
    default:
        return 0U;
    }
}

static uint32_t analyze_full_stage_limit(uint8_t stage)
{
    if (stage == 1U)
    {
        return ANALYZE_FFT_N;
    }

    if ((stage == 2U) || (stage == 3U))
    {
        return ANALYZE_FFT_HALF_N;
    }

    return 0U;
}

static void analyze_advance_summary_stage(void)
{
    while ((g_analyze_log.summary_stage != 0U) &&
           (g_analyze_log.summary_stage <= 3U) &&
           (analyze_summary_stage_enabled(g_analyze_log.summary_stage) == 0U))
    {
        g_analyze_log.summary_stage++;
    }

    if (g_analyze_log.summary_stage > 3U)
    {
        g_analyze_log.summary_stage = 0U;
    }
}

static void analyze_advance_full_stage(void)
{
    while ((g_analyze_log.full_stage != 0U) &&
           (g_analyze_log.full_stage <= 3U) &&
           (analyze_full_stage_enabled(g_analyze_log.full_stage) == 0U))
    {
        g_analyze_log.full_stage++;
        g_analyze_log.full_bin = 0U;
    }

    if (g_analyze_log.full_stage > 3U)
    {
        g_analyze_log.full_stage = 0U;
        g_analyze_log.full_bin = 0U;
    }
}

static void analyze_log_prepare_after_done(void)
{
#if (ANALYZE_RESULT_LOG_ENABLE != 0U)
    g_analyze_log.result_pending = 1U;
#endif

#if (ANALYZE_DEPTH_LOG_ENABLE != 0U)
    g_analyze_log.depth_avg_pending = 1U;
#endif

#if (ANALYZE_SPECTRUM_SUMMARY_LOG_ENABLE != 0U)
    g_analyze_log.summary_stage = 1U;
    analyze_advance_summary_stage();
#endif

    g_analyze_log.full_stage = 1U;
    g_analyze_log.full_bin = 0U;
    analyze_advance_full_stage();

    if ((g_analyze_log.summary_stage != 0U) || (g_analyze_log.full_stage != 0U))
    {
        g_analyze_log.format_pending = 1U;
    }
}

static uint8_t analyze_log_emit_summary_once(void)
{
    analyze_advance_summary_stage();

    switch (g_analyze_log.summary_stage)
    {
    case 1U:
        analyze_log_spectrum_summary(1U, &g_iq_peak);
        break;
    case 2U:
        analyze_log_spectrum_summary(2U, &g_env_peak);
        break;
    case 3U:
        analyze_log_spectrum_summary(3U, &g_phase_peak);
        break;
    default:
        return 0U;
    }

    g_analyze_log.summary_stage++;
    analyze_advance_summary_stage();
    return 1U;
}

static uint8_t analyze_log_emit_full_once(void)
{
    uint8_t stage;
    uint32_t bin;
    uint32_t limit;

    analyze_advance_full_stage();
    stage = g_analyze_log.full_stage;
    bin = g_analyze_log.full_bin;
    limit = analyze_full_stage_limit(stage);

    if ((stage == 0U) || (bin >= limit))
    {
        if (stage != 0U)
        {
            g_analyze_log.full_stage++;
            g_analyze_log.full_bin = 0U;
            analyze_advance_full_stage();
        }
        return 0U;
    }

    if (stage == 1U)
    {
        analyze_log_spectrum_point(1U, analyze_bin_to_hz(bin), g_iq_spec_mag[bin], g_iq_spec_phase[bin], bin);
    }
    else if (stage == 2U)
    {
        analyze_log_spectrum_point(2U, analyze_bin_to_hz(bin), g_env_spec_mag[bin], g_env_spec_phase[bin], bin);
    }
    else
    {
        analyze_log_spectrum_point(3U, analyze_bin_to_hz(bin), g_phase_spec_mag[bin], g_phase_spec_phase[bin], bin);
    }

    g_analyze_log.full_bin++;
    if (g_analyze_log.full_bin >= limit)
    {
        g_analyze_log.full_stage++;
        g_analyze_log.full_bin = 0U;
        analyze_advance_full_stage();
    }

    return 1U;
}

void analyze_start(uint32_t center_hz)
{
    /* 正在分析时拒绝重复启动，避免外部误调用把当前状态清零。 */
    if ((g_analyze.active != 0U) && (g_analyze.done == 0U))
    {
        return;
    }

#if (ANALYZE_START_LOG_ENABLE != 0U)
    char log_buf[128];
    int n = snprintf(log_buf, sizeof(log_buf), "analyze: start, receive center_hz=%luHz\r\n", (unsigned long)center_hz);
    if ((n > 0) && ((size_t)n < sizeof(log_buf)))
    {
        analyze_log_send(log_buf);
    }
#endif

    memset(&g_analyze, 0, sizeof(g_analyze));
    memset(&g_analyze_log, 0, sizeof(g_analyze_log));
    g_analyze.active = 1U;
    g_analyze.done = 0U;
    g_analyze.center_hz = center_hz;
    g_analyze.mode = ANALYZE_MODE_UNKNOWN;
    g_analyze.settle_left = ANALYZE_SETTLE_BLOCKS;
}

uint8_t analyze_process_block(const uint16_t *i_buf,
                              const uint16_t *q_buf,
                              uint32_t sample_cnt)
{
    /* 未启动或已经完成时，不消费 ADC 块 */
    if (g_analyze.active == 0U)
    {
        return g_analyze.done;
    }
    if (g_analyze.settle_left > 0U)
    {
#if (ANALYZE_SETTLE_LOG_ENABLE != 0U)
        char log_buf[96];
        int n = snprintf(log_buf,
                         sizeof(log_buf),
                         "analyze: Wait for stability=%u\r\n",
                         (unsigned int)g_analyze.settle_left);

        if ((n > 0) && ((size_t)n < sizeof(log_buf)))
        {
            analyze_log_send(log_buf);
        }
#endif

    g_analyze.settle_left--;
    return 0U;
    }

        if ((i_buf == NULL) || (q_buf == NULL) || (sample_cnt < ANALYZE_FFT_N))
    {
        return 0U;
    }

    memcpy(g_analyze_i_raw, i_buf, ANALYZE_FFT_N * sizeof(uint16_t));   /* 复制处理块 */
    memcpy(g_analyze_q_raw, q_buf, ANALYZE_FFT_N * sizeof(uint16_t));

    g_analyze.depth_pm = analyze_fill_env_buffer(g_analyze_i_raw,g_analyze_q_raw,ANALYZE_FFT_N);
    analyze_process_spectra();

#if (ANALYZE_DEPTH_LOG_ENABLE != 0U)
    {
        char log_buf[96];
        int n = snprintf(log_buf,
                         sizeof(log_buf),
                         "analyze: depth=%lupm\r\n",
                         (unsigned long)g_analyze.depth_pm);

        if ((n > 0) && ((size_t)n < sizeof(log_buf)))
        {
            analyze_log_send(log_buf);
        }
    }
#endif

    g_analyze.depth_sum_pm += g_analyze.depth_pm;
    g_analyze.vote_count++;

    if (g_analyze.vote_count >= ANALYZE_DEPTH_BLOCKS)
    {
        g_analyze.depth_pm = g_analyze.depth_sum_pm / ANALYZE_DEPTH_BLOCKS;
        g_analyze.done = 1U;
        g_analyze.active = 0U;
        analyze_log_prepare_after_done();
}

return g_analyze.done;
}

uint8_t analyze_is_done(void)
{
    return g_analyze.done;
}

uint8_t analyze_is_active(void)
{
    return g_analyze.active;
}

uint8_t analyze_log_is_busy(void)
{
#if (ANALYZE_LOG_ENABLE != 0U)
    if ((g_analyze_log.result_pending != 0U) ||
        (g_analyze_log.depth_avg_pending != 0U) ||
        (g_analyze_log.summary_stage != 0U) ||
        (g_analyze_log.full_stage != 0U) ||
        (g_analyze_log.format_pending != 0U))
    {
        return 1U;
    }
#endif

    return 0U;
}

void analyze_log_flush_step(uint8_t max_lines)
{
    uint8_t sent = 0U;

#if (ANALYZE_LOG_ENABLE == 0U)
    (void)max_lines;
    return;
#endif

    while (sent < max_lines)
    {
        if (g_analyze_log.result_pending != 0U)
        {
            g_analyze_log.result_pending = 0U;
            analyze_log_result();
            sent++;
            continue;
        }

        if (g_analyze_log.depth_avg_pending != 0U)
        {
            g_analyze_log.depth_avg_pending = 0U;
            analyze_log_depth_avg();
            sent++;
            continue;
        }

        if (analyze_log_emit_summary_once() != 0U)
        {
            sent++;
            continue;
        }

        if (analyze_log_emit_full_once() != 0U)
        {
            sent++;
            continue;
        }

        if (g_analyze_log.format_pending != 0U)
        {
            g_analyze_log.format_pending = 0U;
            analyze_log_spectrum_format_line();
            sent++;
            continue;
        }

        break;
    }
}
