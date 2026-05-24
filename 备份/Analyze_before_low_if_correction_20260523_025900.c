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

#define ANALYZE_IQ_SPECTRUM_ENABLE 1U /* 打开复数 IQ 双边谱，用于区分 CW、FSK、PSK 这类只看包络容易误判的信号。 */
#define ANALYZE_ENV_SPECTRUM_ENABLE 1U /* 幅度类调制使用包络谱提取调制频率。 */
#define ANALYZE_FREQ_SPECTRUM_ENABLE 1U /* 频率类调制使用展开相位差分谱提取调制频率。 */
#define ANALYZE_PHASE_SPECTRUM_ENABLE ANALYZE_FREQ_SPECTRUM_ENABLE
#define ANALYZE_LOG_ENABLE 1U /* 算法日志总控；置 0 后本文件不向打印队列投递日志。 */
#define ANALYZE_SPECTRUM_SUMMARY_LOG_ENABLE 1U /* 是否输出每类谱的主峰摘要。 */
#define ANALYZE_IQ_SPECTRUM_FULL_LOG_ENABLE 1U /* 是否逐 bin 输出 IQ 复数谱。 */
#define ANALYZE_ENV_SPECTRUM_FULL_LOG_ENABLE 1U /* 是否逐 bin 输出包络谱。 */
#define ANALYZE_PHASE_SPECTRUM_FULL_LOG_ENABLE 1U /* 是否逐 bin 输出频率类特征谱。 */
#define ANALYZE_START_LOG_ENABLE 0U /* 是否输出分析启动日志。 */
#define ANALYZE_DEPTH_LOG_ENABLE 0U /* 是否输出每块包络深度与平均深度日志。 */
#define ANALYZE_RESULT_LOG_ENABLE 1U /* 是否输出一次性分析完成日志。 */
#define ANALYZE_SETTLE_BLOCKS 2U
#define ANALYZE_SETTLE_LOG_ENABLE 0U

#define ANALYZE_LOW_IF_EST_ENABLE 1U /* 低中频偏置估计开关，用于观察混频后距离 DC 的残余频偏。 */
#define ANALYZE_MOD_MIN_FREQ_HZ 500U /* 忽略直流和 1 个 bin 以下的慢漂移，4096 点 FFT 下频率分辨率约 500Hz。 */
#define ANALYZE_MOD_MAX_FREQ_HZ 200000U /* 先只判断 200kHz 内的低频调制特征，避免高频杂散误判。 */
#define ANALYZE_AM_DEPTH_MIN_PM 50U /* 包络深度超过 5% 时才认为幅度类特征有效。 */
#define ANALYZE_ENV_SCORE_MIN_PM 3000U /* 包络谱主峰至少约为谱均值 3 倍。 */
#define ANALYZE_FREQ_SCORE_MIN_PM 3000U /* 频率类谱主峰至少约为谱均值 3 倍。 */
#define ANALYZE_MODE_SCORE_DOMINANCE_PM 1500U /* 两类特征同时存在时，1.5 倍以上才判为单一类型。 */
#define ANALYZE_SCORE_PM_MAX 999999U

#define ANALYZE_IQ_TOP_PEAK_COUNT 8U /* 复数 IQ 谱保留的强峰个数；增大可看更多杂散，但会增加判别复杂度。 */
#define ANALYZE_IQ_PEAK_MIN_SEP_HZ 1200U /* 复数 IQ 谱两个峰至少隔这么远才算不同峰，避免同一宽峰被重复计数。 */
#define ANALYZE_IQ_STRONG_PEAK_REL_PM 200U /* 复数 IQ 谱强峰门限：达到主峰 20% 以上才参与强峰数量统计。 */
#define ANALYZE_IQ_OCC_REL_PM 120U /* 粗略占用带宽门限：达到主峰 12% 以上的频点参与带宽估计。 */
#define ANALYZE_CW_DEPTH_MAX_PM 1200U /* 包络起伏低于约 12% 时，才允许判为 CW。 */
#define ANALYZE_CW_OCC_MAX_HZ 12000U /* 复数 IQ 谱占用带宽低于该值时更像单载波 CW。 */
#define ANALYZE_ENV_STRONG_PEAK_REL_PM 200U /* 包络/频率谱强峰计数门限：达到主峰 20% 以上才算强峰。 */
#define ANALYZE_ENV_TONE_BINS 2U /* 包络主峰左右各取几个 bin 估计窄带占比；越大越容易把宽带 ASK 看成 AM。 */
#define ANALYZE_AM_ENV_TONE_MIN_PM 420U /* 包络主峰附近能量占比超过该值时，才认为像 AM 单音包络。 */
#define ANALYZE_AM_ENV_PEAK_COUNT_MAX 3U /* AM 的包络谱强峰不应太多；过多更像 ASK/数字包络。 */
#define ANALYZE_FM_FREQ_ENV_DOMINANCE_PM 1800U /* 频率谱分数至少为包络谱 1.8 倍时，才按 FM 优先判定。 */
#define ANALYZE_FSK_PEAK_BALANCE_MIN_PM 450U /* FSK 双主峰较接近；第二峰至少达到第一峰 45%。 */
#define ANALYZE_FSK_THIRD_PEAK_MAX_PM 700U /* 第三峰相对第二峰不应太强，否则不像干净 2FSK。 */
#define ANALYZE_FSK_MIN_SEP_HZ 5000U /* 两个 FSK 主峰频差下限，低于该值当前 4096 点分辨率下不稳定。 */
#define ANALYZE_FSK_MAX_SEP_HZ 160000U /* 两个 FSK 主峰频差上限，超过后更可能是杂散或多信号。 */
#define ANALYZE_DIGITAL_OCC_MIN_HZ 15000U /* 复数 IQ 谱宽于该值时，才考虑 PSK/弱 FSK 等数字宽带信号。 */
#define ANALYZE_DIGITAL_PEAK_COUNT_MIN 3U /* 宽带数字调制通常有多个强峰；低于该值不轻易判 PSK。 */
#define ANALYZE_ASK_DEPTH_MIN_PM 2000U /* ASK 应有明显包络起伏；低于该值的宽带数字信号优先考虑 PSK。 */
#define ANALYZE_PSK_DEPTH_MAX_PM 2500U /* PSK 包络理论上较平，低于该值且 IQ 谱较宽时优先判 PSK。 */
#define ANALYZE_MODE_COUNT 8U

static uint16_t g_analyze_i_raw[ANALYZE_FFT_N];
static uint16_t g_analyze_q_raw[ANALYZE_FFT_N];

typedef struct
{
    uint32_t bin;
    uint32_t freq_hz;
    float mag;
    float phase;
    float mean_mag;
    uint32_t score_pm;
} analyze_spectrum_peak_t;

typedef struct
{
    uint32_t bin;
    int32_t freq_hz;
    float mag;
} analyze_iq_peak_t;

typedef struct
{
    analyze_iq_peak_t peaks[ANALYZE_IQ_TOP_PEAK_COUNT];
    uint8_t peak_count;
    uint8_t strong_peak_count;
    uint32_t occ_hz;
    uint32_t score_pm;
    uint32_t fsk_sep_hz;
    uint32_t fsk_balance_pm;
    uint32_t third_to_second_pm;
} analyze_iq_features_t;

typedef struct
{
    analyze_mode_t mode;
    uint32_t mod_hz;
    uint32_t confidence_pm;
    uint8_t reason;
} analyze_block_decision_t;

typedef enum
{
    ANALYZE_REASON_NONE = 0,
    ANALYZE_REASON_CW,
    ANALYZE_REASON_FM_PHASE,
    ANALYZE_REASON_AM_TONE,
    ANALYZE_REASON_FSK_STRONG,
    ANALYZE_REASON_FSK_WEAK,
    ANALYZE_REASON_ASK_ENV,
    ANALYZE_REASON_PSK_WIDE,
    ANALYZE_REASON_MIXED
} analyze_reason_t;


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
    uint8_t amp_vote_count;
    uint8_t freq_vote_count;
    uint8_t quiet_vote_count;
    uint8_t mode_vote_count[ANALYZE_MODE_COUNT];
    uint32_t mode_confidence_sum[ANALYZE_MODE_COUNT];
    uint32_t amp_mod_sum_hz;
    uint32_t freq_mod_sum_hz;
    uint32_t env_score_pm;
    uint32_t freq_score_pm;
    uint32_t env_peak_hz;
    uint32_t freq_peak_hz;
    uint32_t iq_occ_hz;
    uint32_t iq_score_pm;
    uint32_t fsk_sep_hz;
    uint8_t result_reason;
    int32_t low_if_hz;
    int32_t low_if_sum_hz;
} analyze_state_t;

typedef struct
{
    uint8_t result_pending;    /* 分析完成结果日志等待发送。 */
    uint8_t depth_avg_pending; /* 平均包络深度日志等待发送。 */
    uint8_t summary_stage;     /* 频谱摘要慢速发送阶段：0空闲，1 IQ，2 包络，3 频率类。 */
    uint8_t full_stage;        /* 全量频谱慢速发送阶段：0空闲，1 IQ，2 包络，3 频率类。 */
    uint8_t format_pending;    /* 频谱输出完成后的格式说明日志等待发送。 */
    uint32_t full_bin;         /* 当前全量频谱发送到的 bin。 */
} analyze_log_state_t;

static analyze_state_t g_analyze;
static analyze_log_state_t g_analyze_log;
static uint8_t g_fft_ready;
static analyze_iq_features_t g_iq_features;
static analyze_spectrum_peak_t g_iq_peak;
static analyze_spectrum_peak_t g_env_peak;
static analyze_spectrum_peak_t g_phase_peak;
static uint8_t g_env_peak_count;
static uint8_t g_phase_peak_count;
static uint32_t g_env_tone_fraction_pm;

static arm_rfft_fast_instance_f32 g_env_fft_inst;
static arm_rfft_fast_instance_f32 g_phase_fft_inst;

__attribute__((section(".ram_d1_buffer")))
static float g_i_buf[ANALYZE_FFT_N];
__attribute__((section(".ram_d1_buffer")))
static float g_q_buf[ANALYZE_FFT_N];
__attribute__((section(".ram_d1_buffer")))
static float g_env_buf[ANALYZE_FFT_N];
__attribute__((section(".ram_d1_buffer")))
static float g_freq_dev_buf[ANALYZE_FFT_N];
__attribute__((section(".ram_d1_buffer")))
static float g_phase_unwrap_buf[ANALYZE_FFT_N];
#if (ANALYZE_IQ_SPECTRUM_ENABLE != 0U)
__attribute__((section(".ram_d1_buffer")))
static float g_iq_cfft_buf[ANALYZE_FFT_N * 2U];
#endif
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

#if (ANALYZE_ENV_SPECTRUM_ENABLE != 0U)
    if (arm_rfft_fast_init_f32(&g_env_fft_inst, ANALYZE_FFT_N) != ARM_MATH_SUCCESS)
    {
        return 0U;
    }
#endif

#if (ANALYZE_FREQ_SPECTRUM_ENABLE != 0U)
    if (arm_rfft_fast_init_f32(&g_phase_fft_inst, ANALYZE_FFT_N) != ARM_MATH_SUCCESS)
    {
        return 0U;
    }
#endif

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

static uint8_t analyze_mod_bin_is_valid(uint32_t bin)
{
    uint32_t freq_hz = analyze_bin_to_hz(bin);

    return ((freq_hz >= ANALYZE_MOD_MIN_FREQ_HZ) &&
            (freq_hz <= ANALYZE_MOD_MAX_FREQ_HZ)) ? 1U : 0U;
}

static uint32_t analyze_peak_score_pm(float peak_mag, float mean_mag)
{
    float score;

    if ((peak_mag <= 0.0f) || (mean_mag <= 0.000001f))
    {
        return 0U;
    }

    score = (peak_mag * 1000.0f) / mean_mag;
    if (score >= (float)ANALYZE_SCORE_PM_MAX)
    {
        return ANALYZE_SCORE_PM_MAX;
    }

    return (uint32_t)(score + 0.5f);
}

static int32_t analyze_rad_step_to_hz(float rad_step)
{
    float freq_hz = (rad_step * (float)ANALYZE_SAMPLE_RATE_HZ) / ANALYZE_TWO_PI_F;

    if (freq_hz >= 0.0f)
    {
        return (int32_t)(freq_hz + 0.5f);
    }

    return (int32_t)(freq_hz - 0.5f);
}

static int32_t analyze_average_i32(int32_t sum, uint8_t count)
{
    if (count == 0U)
    {
        return 0;
    }

    if (sum >= 0)
    {
        return (sum + ((int32_t)count / 2)) / (int32_t)count;
    }

    return (sum - ((int32_t)count / 2)) / (int32_t)count;
}

static const char *analyze_mode_to_text(analyze_mode_t mode)
{
    switch (mode)
    {
    case ANALYZE_MODE_CW:
        return "CW";
    case ANALYZE_MODE_AM:
        return "AM";
    case ANALYZE_MODE_ASK:
        return "ASK";
    case ANALYZE_MODE_FM:
        return "FM";
    case ANALYZE_MODE_MIXED:
        return "MIXED";
    case ANALYZE_MODE_FSK:
        return "FSK";
    case ANALYZE_MODE_PSK:
        return "PSK";
    default:
        return "UNKNOWN";
    }
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
    analyze_log_send("format:ch0=freq_hz,ch1=mag,ch2=phase_rad,ch3=bin,ch4=spec_id(1=iq,2=env,3=freq)\r\n");
}

static void analyze_log_result(void)
{
    char log_buf[320];
    int n = snprintf(log_buf,
                     sizeof(log_buf),
                     "analyze: result center=%luHz low_if=%ldHz mode=%s mod=%luHz depth=%lupm env=%luHz/%lupm freq=%luHz/%lupm iq=%luHz/%lupm fsk=%luHz votes=%u/%u/%u/%u/%u/%u/%u/%u reason=%u\r\n",
                     (unsigned long)g_analyze.center_hz,
                     (long)g_analyze.low_if_hz,
                     analyze_mode_to_text(g_analyze.mode),
                     (unsigned long)g_analyze.mod_hz,
                     (unsigned long)g_analyze.depth_pm,
                     (unsigned long)g_analyze.env_peak_hz,
                     (unsigned long)g_analyze.env_score_pm,
                     (unsigned long)g_analyze.freq_peak_hz,
                     (unsigned long)g_analyze.freq_score_pm,
                     (unsigned long)g_analyze.iq_occ_hz,
                     (unsigned long)g_analyze.iq_score_pm,
                     (unsigned long)g_analyze.fsk_sep_hz,
                     (unsigned int)g_analyze.mode_vote_count[ANALYZE_MODE_CW],
                     (unsigned int)g_analyze.mode_vote_count[ANALYZE_MODE_AM],
                     (unsigned int)g_analyze.mode_vote_count[ANALYZE_MODE_ASK],
                     (unsigned int)g_analyze.mode_vote_count[ANALYZE_MODE_FM],
                     (unsigned int)g_analyze.mode_vote_count[ANALYZE_MODE_FSK],
                     (unsigned int)g_analyze.mode_vote_count[ANALYZE_MODE_PSK],
                     (unsigned int)g_analyze.mode_vote_count[ANALYZE_MODE_MIXED],
                     (unsigned int)g_analyze.mode_vote_count[ANALYZE_MODE_UNKNOWN],
                     (unsigned int)g_analyze.result_reason);

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

static void analyze_update_mod_peak(analyze_spectrum_peak_t *peak,
                                    uint32_t bin,
                                    float mag,
                                    float phase)
{
    if (analyze_mod_bin_is_valid(bin) == 0U)
    {
        return;
    }

    analyze_update_peak(peak, bin, mag, phase);
}

static int32_t analyze_iq_bin_to_signed_hz(uint32_t bin)
{
    if (bin <= ANALYZE_FFT_HALF_N)
    {
        return (int32_t)analyze_bin_to_hz(bin);
    }

    return -(int32_t)analyze_bin_to_hz(ANALYZE_FFT_N - bin);
}

static uint32_t analyze_abs_i32(int32_t value)
{
    return (value < 0) ? (uint32_t)(-value) : (uint32_t)value;
}

static uint32_t analyze_abs_diff_i32(int32_t a, int32_t b)
{
    return (a >= b) ? (uint32_t)(a - b) : (uint32_t)(b - a);
}

static uint8_t analyze_iq_peak_far_enough(const analyze_iq_features_t *features, int32_t freq_hz)
{
    if (features == NULL)
    {
        return 0U;
    }

    for (uint8_t k = 0U; k < features->peak_count; k++)
    {
        if (analyze_abs_diff_i32(freq_hz, features->peaks[k].freq_hz) < ANALYZE_IQ_PEAK_MIN_SEP_HZ)
        {
            return 0U;
        }
    }

    return 1U;
}

static void analyze_iq_insert_peak(analyze_iq_features_t *features, uint32_t bin, int32_t freq_hz, float mag)
{
    uint8_t insert_pos = ANALYZE_IQ_TOP_PEAK_COUNT;

    if ((features == NULL) || (mag <= 0.0f) || (analyze_iq_peak_far_enough(features, freq_hz) == 0U))
    {
        return;
    }

    for (uint8_t k = 0U; k < features->peak_count; k++)
    {
        if (mag > features->peaks[k].mag)
        {
            insert_pos = k;
            break;
        }
    }

    if (insert_pos == ANALYZE_IQ_TOP_PEAK_COUNT)
    {
        if (features->peak_count < ANALYZE_IQ_TOP_PEAK_COUNT)
        {
            insert_pos = features->peak_count;
        }
        else
        {
            return;
        }
    }

    if (features->peak_count < ANALYZE_IQ_TOP_PEAK_COUNT)
    {
        features->peak_count++;
    }

    for (uint8_t k = (uint8_t)(features->peak_count - 1U); k > insert_pos; k--)
    {
        features->peaks[k] = features->peaks[k - 1U];
    }

    features->peaks[insert_pos].bin = bin;
    features->peaks[insert_pos].freq_hz = freq_hz;
    features->peaks[insert_pos].mag = mag;
}

static uint32_t analyze_ratio_pm(float numerator, float denominator)
{
    float ratio;

    if ((numerator <= 0.0f) || (denominator <= 0.000001f))
    {
        return 0U;
    }

    ratio = (numerator * 1000.0f) / denominator;
    if (ratio >= (float)ANALYZE_SCORE_PM_MAX)
    {
        return ANALYZE_SCORE_PM_MAX;
    }

    return (uint32_t)(ratio + 0.5f);
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
    float prev_phase = 0.0f;
    float freq_mean = 0.0f;

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

        if (k == 0U)
        {
            g_phase_unwrap_buf[k] = phase;
            prev_phase = phase;
        }
        else
        {
            float delta = phase - prev_phase;

            if (delta > (ANALYZE_PI_F * 0.8f))
            {
                phase_offset -= ANALYZE_TWO_PI_F;
            }
            else if (delta < (-ANALYZE_PI_F * 0.8f))
            {
                phase_offset += ANALYZE_TWO_PI_F;
            }

            g_phase_unwrap_buf[k] = phase + phase_offset;
            prev_phase = phase;
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

    /* 频率类调制优先看瞬时频率变化，因此对展开相位做一阶差分并去均值。 */
    for (uint32_t k = 1U; k < ANALYZE_FFT_N; k++)
    {
        float freq_dev = g_phase_unwrap_buf[k] - g_phase_unwrap_buf[k - 1U];

        g_freq_dev_buf[k - 1U] = freq_dev;
        freq_mean += freq_dev;
    }

    g_freq_dev_buf[ANALYZE_FFT_N - 1U] = g_freq_dev_buf[ANALYZE_FFT_N - 2U];
    freq_mean += g_freq_dev_buf[ANALYZE_FFT_N - 1U];
    freq_mean /= (float)ANALYZE_FFT_N;

#if (ANALYZE_LOW_IF_EST_ENABLE != 0U)
    {
        int32_t low_if_hz = analyze_rad_step_to_hz(freq_mean);

        /* freq_mean 是本块平均相位步进，对应混频后残留低中频偏置。 */
        g_analyze.low_if_hz = low_if_hz;
        g_analyze.low_if_sum_hz += low_if_hz;
    }
#endif

    for (uint32_t k = 0; k < ANALYZE_FFT_N; k++)
    {
        /* 频率类特征只保留围绕低中频的变化量，不让 5kHz 一类固定偏置参与调制判决。 */
        g_freq_dev_buf[k] -= freq_mean;
    }

    if ((env_max + env_min) <= 1.0f)
    {
        return 0U;
    }

    return (uint32_t)(((env_max - env_min) * 1000.0f) / (env_max + env_min));
}

#if (ANALYZE_IQ_SPECTRUM_ENABLE != 0U)
static analyze_spectrum_peak_t analyze_process_iq_spectrum(void)
{
    analyze_spectrum_peak_t peak = {0U, 0U, 0.0f, 0.0f, 0.0f, 0U};
    float mag_sum = 0.0f;
    uint32_t mag_count = 0U;
    int32_t occ_min_hz = 0;
    int32_t occ_max_hz = 0;
    uint8_t occ_started = 0U;

    memset(&g_iq_features, 0, sizeof(g_iq_features));

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
        int32_t signed_hz = analyze_iq_bin_to_signed_hz(k);

        analyze_calc_mag_phase(re, im, &mag, &phase);
        g_iq_spec_mag[k] = mag;
        g_iq_spec_phase[k] = phase;
        analyze_update_peak(&peak, k, mag, phase);
        if (analyze_abs_i32(signed_hz) >= ANALYZE_MOD_MIN_FREQ_HZ)
        {
            mag_sum += mag;
            mag_count++;
            analyze_iq_insert_peak(&g_iq_features, k, signed_hz, mag);
        }

    }

    if (mag_count != 0U)
    {
        peak.mean_mag = mag_sum / (float)mag_count;
        peak.score_pm = analyze_peak_score_pm(peak.mag, peak.mean_mag);
        g_iq_features.score_pm = peak.score_pm;
    }

    if (g_iq_features.peak_count != 0U)
    {
        float occ_threshold = (g_iq_features.peaks[0].mag * (float)ANALYZE_IQ_OCC_REL_PM) / 1000.0f;
        float strong_threshold = (g_iq_features.peaks[0].mag * (float)ANALYZE_IQ_STRONG_PEAK_REL_PM) / 1000.0f;

        for (uint8_t k = 0U; k < g_iq_features.peak_count; k++)
        {
            if (g_iq_features.peaks[k].mag >= strong_threshold)
            {
                g_iq_features.strong_peak_count++;
            }
        }

        for (uint32_t k = 0; k < ANALYZE_FFT_N; k++)
        {
            if (g_iq_spec_mag[k] >= occ_threshold)
            {
                int32_t signed_hz = analyze_iq_bin_to_signed_hz(k);

                if (occ_started == 0U)
                {
                    occ_min_hz = signed_hz;
                    occ_max_hz = signed_hz;
                    occ_started = 1U;
                }
                else
                {
                    if (signed_hz < occ_min_hz) occ_min_hz = signed_hz;
                    if (signed_hz > occ_max_hz) occ_max_hz = signed_hz;
                }
            }
        }

        if (occ_started != 0U)
        {
            g_iq_features.occ_hz = analyze_abs_diff_i32(occ_max_hz, occ_min_hz);
        }

        if (g_iq_features.peak_count >= 2U)
        {
            g_iq_features.fsk_sep_hz = analyze_abs_diff_i32(g_iq_features.peaks[0].freq_hz,
                                                            g_iq_features.peaks[1].freq_hz);
            g_iq_features.fsk_balance_pm = analyze_ratio_pm(g_iq_features.peaks[1].mag,
                                                            g_iq_features.peaks[0].mag);
        }

        if (g_iq_features.peak_count >= 3U)
        {
            g_iq_features.third_to_second_pm = analyze_ratio_pm(g_iq_features.peaks[2].mag,
                                                                g_iq_features.peaks[1].mag);
        }
    }

    return peak;
}
#endif

static analyze_spectrum_peak_t analyze_process_rfft_spectrum(const char *tag,
                                                             arm_rfft_fast_instance_f32 *fft_inst,
                                                             float *input_buf,
                                                             float *fft_buf,
                                                             float *mag_buf,
                                                             float *phase_buf)
{
    analyze_spectrum_peak_t peak = {0U, 0U, 0.0f, 0.0f, 0.0f, 0U};
    float mag_sum = 0.0f;
    uint32_t mag_count = 0U;

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

        if (analyze_mod_bin_is_valid(k) != 0U)
        {
            mag_sum += mag;
            mag_count++;
            analyze_update_mod_peak(&peak, k, mag, phase);
        }

    }

    if (mag_count != 0U)
    {
        peak.mean_mag = mag_sum / (float)mag_count;
        peak.score_pm = analyze_peak_score_pm(peak.mag, peak.mean_mag);
    }

    return peak;
}

static uint8_t analyze_count_rfft_strong_peaks(const float *mag_buf, const analyze_spectrum_peak_t *peak)
{
    uint8_t count = 0U;
    float threshold;

    if ((mag_buf == NULL) || (peak == NULL) || (peak->mag <= 0.0f))
    {
        return 0U;
    }

    threshold = (peak->mag * (float)ANALYZE_ENV_STRONG_PEAK_REL_PM) / 1000.0f;
    for (uint32_t k = 1U; k < (ANALYZE_FFT_HALF_N - 1U); k++)
    {
        if ((analyze_mod_bin_is_valid(k) != 0U) &&
            (mag_buf[k] >= threshold) &&
            (mag_buf[k] >= mag_buf[k - 1U]) &&
            (mag_buf[k] >= mag_buf[k + 1U]))
        {
            count++;
        }
    }

    return count;
}

static uint32_t analyze_rfft_tone_fraction_pm(const float *mag_buf, const analyze_spectrum_peak_t *peak)
{
    float total_power = 0.0f;
    float tone_power = 0.0f;
    uint32_t start_bin;
    uint32_t stop_bin;

    if ((mag_buf == NULL) || (peak == NULL) || (peak->bin == 0U))
    {
        return 0U;
    }

    start_bin = (peak->bin > ANALYZE_ENV_TONE_BINS) ? (peak->bin - ANALYZE_ENV_TONE_BINS) : 1U;
    stop_bin = peak->bin + ANALYZE_ENV_TONE_BINS;
    if (stop_bin >= ANALYZE_FFT_HALF_N)
    {
        stop_bin = ANALYZE_FFT_HALF_N - 1U;
    }

    for (uint32_t k = 1U; k < ANALYZE_FFT_HALF_N; k++)
    {
        float power = mag_buf[k] * mag_buf[k];

        if (analyze_mod_bin_is_valid(k) == 0U)
        {
            continue;
        }

        total_power += power;
        if ((k >= start_bin) && (k <= stop_bin))
        {
            tone_power += power;
        }
    }

    return analyze_ratio_pm(tone_power, total_power);
}

static uint8_t analyze_peak_score_valid(const analyze_spectrum_peak_t *peak, uint32_t min_score_pm)
{
    if (peak == NULL)
    {
        return 0U;
    }

    return ((peak->bin != 0U) && (peak->score_pm >= min_score_pm)) ? 1U : 0U;
}

static uint8_t analyze_iq_fsk_strong_valid(void)
{
    if (g_iq_features.peak_count < 2U)
    {
        return 0U;
    }

    if ((g_iq_features.fsk_sep_hz < ANALYZE_FSK_MIN_SEP_HZ) ||
        (g_iq_features.fsk_sep_hz > ANALYZE_FSK_MAX_SEP_HZ))
    {
        return 0U;
    }

    if (g_iq_features.fsk_balance_pm < ANALYZE_FSK_PEAK_BALANCE_MIN_PM)
    {
        return 0U;
    }

    if ((g_iq_features.peak_count >= 3U) &&
        (g_iq_features.third_to_second_pm > ANALYZE_FSK_THIRD_PEAK_MAX_PM))
    {
        return 0U;
    }

    return 1U;
}

static analyze_block_decision_t analyze_classify_current_block(uint8_t amp_valid, uint8_t freq_valid)
{
    analyze_block_decision_t decision = {ANALYZE_MODE_UNKNOWN, 0U, 0U, ANALYZE_REASON_NONE};
    uint8_t fsk_strong = analyze_iq_fsk_strong_valid();
    uint8_t iq_wide = ((g_iq_features.occ_hz >= ANALYZE_DIGITAL_OCC_MIN_HZ) ||
                       (g_iq_features.strong_peak_count >= ANALYZE_DIGITAL_PEAK_COUNT_MIN)) ? 1U : 0U;
    uint8_t iq_narrow = ((g_iq_features.occ_hz <= ANALYZE_CW_OCC_MAX_HZ) &&
                         (g_iq_features.strong_peak_count <= 2U)) ? 1U : 0U;
    uint8_t env_tone_like = ((g_env_tone_fraction_pm >= ANALYZE_AM_ENV_TONE_MIN_PM) &&
                             (g_env_peak_count <= ANALYZE_AM_ENV_PEAK_COUNT_MAX)) ? 1U : 0U;

    if ((g_analyze.depth_pm <= ANALYZE_CW_DEPTH_MAX_PM) &&
        (iq_narrow != 0U) &&
        (fsk_strong == 0U))
    {
        decision.mode = ANALYZE_MODE_CW;
        decision.mod_hz = 0U;
        decision.confidence_pm = (g_iq_features.score_pm != 0U) ? g_iq_features.score_pm : 1000U;
        decision.reason = ANALYZE_REASON_CW;
        return decision;
    }

    if ((freq_valid != 0U) &&
        (fsk_strong == 0U) &&
        (((uint32_t)g_phase_peak.score_pm * 1000UL) >=
         ((uint32_t)g_env_peak.score_pm * ANALYZE_FM_FREQ_ENV_DOMINANCE_PM)))
    {
        decision.mode = ANALYZE_MODE_FM;
        decision.mod_hz = g_phase_peak.freq_hz;
        decision.confidence_pm = g_phase_peak.score_pm;
        decision.reason = ANALYZE_REASON_FM_PHASE;
        return decision;
    }

    if ((amp_valid != 0U) && (env_tone_like != 0U) && (fsk_strong == 0U))
    {
        decision.mode = ANALYZE_MODE_AM;
        decision.mod_hz = g_env_peak.freq_hz;
        decision.confidence_pm = g_env_peak.score_pm;
        decision.reason = ANALYZE_REASON_AM_TONE;
        return decision;
    }

    if (fsk_strong != 0U)
    {
        decision.mode = ANALYZE_MODE_FSK;
        decision.mod_hz = g_iq_features.fsk_sep_hz;
        decision.confidence_pm = (g_iq_features.fsk_balance_pm > g_iq_features.score_pm) ?
                                 g_iq_features.fsk_balance_pm : g_iq_features.score_pm;
        decision.reason = ANALYZE_REASON_FSK_STRONG;
        return decision;
    }

    if ((iq_wide != 0U) && (amp_valid == 0U) && (freq_valid == 0U))
    {
        decision.mode = ANALYZE_MODE_PSK;
        decision.mod_hz = 0U;
        decision.confidence_pm = g_iq_features.score_pm;
        decision.reason = ANALYZE_REASON_PSK_WIDE;
        return decision;
    }

    if ((iq_wide != 0U) && (amp_valid == 0U) && (freq_valid != 0U))
    {
        decision.mode = ANALYZE_MODE_FSK;
        decision.mod_hz = (g_iq_features.fsk_sep_hz != 0U) ? g_iq_features.fsk_sep_hz : g_phase_peak.freq_hz;
        decision.confidence_pm = g_phase_peak.score_pm;
        decision.reason = ANALYZE_REASON_FSK_WEAK;
        return decision;
    }

    if ((iq_wide != 0U) && (env_tone_like == 0U) && (g_analyze.depth_pm <= ANALYZE_PSK_DEPTH_MAX_PM))
    {
        decision.mode = ANALYZE_MODE_PSK;
        decision.mod_hz = 0U;
        decision.confidence_pm = (g_iq_features.score_pm > g_env_peak.score_pm) ?
                                 g_iq_features.score_pm : g_env_peak.score_pm;
        decision.reason = ANALYZE_REASON_PSK_WIDE;
        return decision;
    }

    if ((amp_valid != 0U) && (env_tone_like == 0U) && (g_analyze.depth_pm >= ANALYZE_ASK_DEPTH_MIN_PM))
    {
        decision.mode = ANALYZE_MODE_ASK;
        decision.mod_hz = g_env_peak.freq_hz;
        decision.confidence_pm = g_env_peak.score_pm;
        decision.reason = ANALYZE_REASON_ASK_ENV;
        return decision;
    }

    if ((g_analyze.depth_pm <= ANALYZE_CW_DEPTH_MAX_PM) &&
        (amp_valid == 0U) &&
        (freq_valid == 0U))
    {
        decision.mode = ANALYZE_MODE_CW;
        decision.mod_hz = 0U;
        decision.confidence_pm = (g_iq_features.score_pm != 0U) ? g_iq_features.score_pm : 1000U;
        decision.reason = ANALYZE_REASON_CW;
        return decision;
    }

    if ((amp_valid != 0U) || (freq_valid != 0U))
    {
        decision.mode = ANALYZE_MODE_MIXED;
        decision.mod_hz = (amp_valid != 0U) ? g_env_peak.freq_hz : g_phase_peak.freq_hz;
        decision.confidence_pm = (g_env_peak.score_pm > g_phase_peak.score_pm) ?
                                 g_env_peak.score_pm : g_phase_peak.score_pm;
        decision.reason = ANALYZE_REASON_MIXED;
    }

    return decision;
}

static void analyze_accumulate_block_result(void)
{
    uint8_t amp_valid = 0U;
    uint8_t freq_valid = 0U;
    analyze_block_decision_t decision;

#if (ANALYZE_ENV_SPECTRUM_ENABLE != 0U)
    amp_valid = ((g_analyze.depth_pm >= ANALYZE_AM_DEPTH_MIN_PM) &&
                 (analyze_peak_score_valid(&g_env_peak, ANALYZE_ENV_SCORE_MIN_PM) != 0U)) ? 1U : 0U;
    g_analyze.env_score_pm = g_env_peak.score_pm;
    g_analyze.env_peak_hz = g_env_peak.freq_hz;
#endif

#if (ANALYZE_FREQ_SPECTRUM_ENABLE != 0U)
    freq_valid = analyze_peak_score_valid(&g_phase_peak, ANALYZE_FREQ_SCORE_MIN_PM);
    g_analyze.freq_score_pm = g_phase_peak.score_pm;
    g_analyze.freq_peak_hz = g_phase_peak.freq_hz;
#endif

    /* 当前先做轻量投票：包络谱代表幅度类，频率偏移谱代表频率类。 */
    if (amp_valid != 0U)
    {
        g_analyze.amp_vote_count++;
        g_analyze.amp_mod_sum_hz += g_env_peak.freq_hz;
    }

    if (freq_valid != 0U)
    {
        g_analyze.freq_vote_count++;
        g_analyze.freq_mod_sum_hz += g_phase_peak.freq_hz;
    }

    if ((amp_valid == 0U) && (freq_valid == 0U))
    {
        g_analyze.quiet_vote_count++;
    }

    decision = analyze_classify_current_block(amp_valid, freq_valid);
    if ((uint32_t)decision.mode < ANALYZE_MODE_COUNT)
    {
        g_analyze.mode_vote_count[decision.mode]++;
        g_analyze.mode_confidence_sum[decision.mode] += decision.confidence_pm;
    }

    if (decision.mod_hz != 0U)
    {
        if (decision.mode == ANALYZE_MODE_FSK)
        {
            g_analyze.fsk_sep_hz = decision.mod_hz;
        }
    }

    if (decision.reason != ANALYZE_REASON_NONE)
    {
        g_analyze.result_reason = decision.reason;
    }
}

static uint32_t analyze_average_mod_hz(uint32_t sum_hz, uint8_t count)
{
    if (count == 0U)
    {
        return 0U;
    }

    return (sum_hz + ((uint32_t)count / 2U)) / (uint32_t)count;
}

static void analyze_finalize_result(void)
{
    analyze_mode_t best_mode = ANALYZE_MODE_UNKNOWN;
    uint8_t best_votes = 0U;
    uint8_t second_votes = 0U;
    uint32_t best_confidence = 0U;

#if (ANALYZE_LOW_IF_EST_ENABLE != 0U)
    g_analyze.low_if_hz = analyze_average_i32(g_analyze.low_if_sum_hz, g_analyze.vote_count);
#endif

    for (uint8_t mode = 0U; mode < ANALYZE_MODE_COUNT; mode++)
    {
        uint8_t votes = g_analyze.mode_vote_count[mode];
        uint32_t confidence = g_analyze.mode_confidence_sum[mode];

        if ((votes > best_votes) || ((votes == best_votes) && (confidence > best_confidence)))
        {
            second_votes = best_votes;
            best_votes = votes;
            best_confidence = confidence;
            best_mode = (analyze_mode_t)mode;
        }
        else if (votes > second_votes)
        {
            second_votes = votes;
        }
    }

    if (best_votes == 0U)
    {
        g_analyze.mode = ANALYZE_MODE_UNKNOWN;
        g_analyze.mod_hz = 0U;
        return;
    }

    if ((best_votes == second_votes) && (best_votes != 0U) &&
        (best_mode != ANALYZE_MODE_CW) && (best_mode != ANALYZE_MODE_UNKNOWN))
    {
        g_analyze.mode = ANALYZE_MODE_MIXED;
        g_analyze.mod_hz = (g_analyze.amp_vote_count >= g_analyze.freq_vote_count) ?
                           analyze_average_mod_hz(g_analyze.amp_mod_sum_hz, g_analyze.amp_vote_count) :
                           analyze_average_mod_hz(g_analyze.freq_mod_sum_hz, g_analyze.freq_vote_count);
        return;
    }

    g_analyze.mode = best_mode;
    switch (best_mode)
    {
    case ANALYZE_MODE_AM:
    case ANALYZE_MODE_ASK:
        g_analyze.mod_hz = analyze_average_mod_hz(g_analyze.amp_mod_sum_hz, g_analyze.amp_vote_count);
        break;
    case ANALYZE_MODE_FM:
        g_analyze.mod_hz = analyze_average_mod_hz(g_analyze.freq_mod_sum_hz, g_analyze.freq_vote_count);
        break;
    case ANALYZE_MODE_FSK:
        g_analyze.mod_hz = (g_analyze.fsk_sep_hz != 0U) ? g_analyze.fsk_sep_hz :
                           analyze_average_mod_hz(g_analyze.freq_mod_sum_hz, g_analyze.freq_vote_count);
        break;
    default:
        g_analyze.mod_hz = 0U;
        break;
    }
}

static void analyze_process_spectra(void)
{
    memset(&g_iq_features, 0, sizeof(g_iq_features));
    g_env_peak = (analyze_spectrum_peak_t){0U, 0U, 0.0f, 0.0f, 0.0f, 0U};
    g_phase_peak = (analyze_spectrum_peak_t){0U, 0U, 0.0f, 0.0f, 0.0f, 0U};
    g_env_peak_count = 0U;
    g_phase_peak_count = 0U;
    g_env_tone_fraction_pm = 0U;

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
        g_env_peak_count = analyze_count_rfft_strong_peaks(g_env_spec_mag, &g_env_peak);
        g_env_tone_fraction_pm = analyze_rfft_tone_fraction_pm(g_env_spec_mag, &g_env_peak);
    }
#endif

#if (ANALYZE_FREQ_SPECTRUM_ENABLE != 0U)
    {
        g_phase_peak = analyze_process_rfft_spectrum("freq",
                                                     &g_phase_fft_inst,
                                                     g_freq_dev_buf,
                                                     g_phase_rfft_buf,
                                                     g_phase_spec_mag,
                                                     g_phase_spec_phase);
        g_phase_peak_count = analyze_count_rfft_strong_peaks(g_phase_spec_mag, &g_phase_peak);
    }
#endif

    g_analyze.iq_occ_hz = g_iq_features.occ_hz;
    g_analyze.iq_score_pm = g_iq_features.score_pm;
    if (g_iq_features.fsk_sep_hz != 0U)
    {
        g_analyze.fsk_sep_hz = g_iq_features.fsk_sep_hz;
    }
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
    analyze_accumulate_block_result();

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
        analyze_finalize_result();
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

void analyze_get_result(analyze_result_t *result_out)
{
    if (result_out == NULL)
    {
        return;
    }

    result_out->mode = g_analyze.mode;
    result_out->center_hz = g_analyze.center_hz;
    result_out->mod_hz = g_analyze.mod_hz;
    result_out->depth_pm = g_analyze.depth_pm;
    result_out->done = g_analyze.done;
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
