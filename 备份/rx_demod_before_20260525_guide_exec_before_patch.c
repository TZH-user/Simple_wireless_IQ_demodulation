#include "rx_demod.h"
#include "arm_math.h"
#include "dsp/fast_math_functions.h"
#include <math.h>


#define RX_ADC_MAX       4095U
#define RX_DAC_MIN       0U
#define RX_DAC_MAX       4095U
#define RX_DAC_CENTER    2048U
#define RX_DAC_VREF_MV   3300U
#define RX_ENV_DC_SHIFT  13U
#define RX_AUDIO_GAIN_Q8 256
#define RX_AUDIO_SMOOTH_SHIFT 1U

/*
 * 解调输出峰峰值微调入口，单位 mV，作用于 DAC1_OUT2(PA5)。
 * AM/FM 是在原有解调波形基础上做比例微调；FSK/PSK 直接决定高低电平间距。
 */
/* 负载与等效源阻抗用于把“负载端目标 Vpp”换算成 DAC 开路 Vpp。 */
#define RX_DEMOD_LOAD_OHM        50U
#define RX_DEMOD_SOURCE_OHM      50U
#define RX_DEMOD_AM_OUT_VPP_MV   100U
#define RX_DEMOD_FM_OUT_VPP_MV   100U
#define RX_DEMOD_ASK_OUT_VPP_MV  100U
#define RX_DEMOD_FSK_OUT_VPP_MV  100U
#define RX_DEMOD_PSK_OUT_VPP_MV  100U
#define RX_DEMOD_ANALOG_REF_VPP_MV 1000U

#define RX_DAC_OPEN_VPP_FROM_LOAD_VPP_MV(vpp_mv) \
    ((((uint32_t)(vpp_mv)) * (RX_DEMOD_SOURCE_OHM + RX_DEMOD_LOAD_OHM) + (RX_DEMOD_LOAD_OHM / 2U)) / RX_DEMOD_LOAD_OHM)
#define RX_DAC_CODE_FROM_VPP_MV(vpp_mv) \
    (((uint32_t)(vpp_mv) * RX_DAC_MAX + (RX_DAC_VREF_MV / 2U)) / RX_DAC_VREF_MV)
#define RX_DAC_HALF_SPAN_FROM_VPP_MV(vpp_mv) \
    (RX_DAC_CODE_FROM_VPP_MV(vpp_mv) / 2U)
#define RX_DAC_LOW_FROM_VPP_MV(vpp_mv) \
    ((RX_DAC_HALF_SPAN_FROM_VPP_MV(vpp_mv) >= RX_DAC_CENTER) ? RX_DAC_MIN : (RX_DAC_CENTER - RX_DAC_HALF_SPAN_FROM_VPP_MV(vpp_mv)))
#define RX_DAC_HIGH_FROM_VPP_MV(vpp_mv) \
    (((RX_DAC_CENTER + RX_DAC_HALF_SPAN_FROM_VPP_MV(vpp_mv)) > RX_DAC_MAX) ? RX_DAC_MAX : (RX_DAC_CENTER + RX_DAC_HALF_SPAN_FROM_VPP_MV(vpp_mv)))

#define RX_DEMOD_AM_DAC_VPP_MV   RX_DAC_OPEN_VPP_FROM_LOAD_VPP_MV(RX_DEMOD_AM_OUT_VPP_MV)
#define RX_DEMOD_FM_DAC_VPP_MV   RX_DAC_OPEN_VPP_FROM_LOAD_VPP_MV(RX_DEMOD_FM_OUT_VPP_MV)
#define RX_DEMOD_ASK_DAC_VPP_MV  RX_DAC_OPEN_VPP_FROM_LOAD_VPP_MV(RX_DEMOD_ASK_OUT_VPP_MV)
#define RX_DEMOD_FSK_DAC_VPP_MV  RX_DAC_OPEN_VPP_FROM_LOAD_VPP_MV(RX_DEMOD_FSK_OUT_VPP_MV)
#define RX_DEMOD_PSK_DAC_VPP_MV  RX_DAC_OPEN_VPP_FROM_LOAD_VPP_MV(RX_DEMOD_PSK_OUT_VPP_MV)

#define RX_DEFAULT_SYMBOL_RATE_HZ 10000U
#define RX_SYMBOL_RATE_MIN_HZ     1000U

#define RX_FM_DC_SHIFT          10U
#define RX_FM_SMOOTH_SHIFT      1U
#define RX_FM_DAC_GAIN_SHIFT    0U
#define RX_FM_ATAN_PI          3.14159265358979323846f
#define RX_FM_ATAN_TWO_PI      6.28318530717958647692f
#define RX_FM_ATAN_DC_ALPHA    0.001f
#define RX_FM_ATAN_LPF_ALPHA   0.25f
#define RX_FM_ATAN_DAC_GAIN    1000.0f

#define RX_FSK_IQ_DC_SHIFT      1U
#define RX_FSK_FREQ_DC_SHIFT    10U
#define RX_FSK_SMOOTH_SHIFT     0U
#define RX_FSK_DAC_LOW          RX_DAC_LOW_FROM_VPP_MV(RX_DEMOD_FSK_DAC_VPP_MV)
#define RX_FSK_DAC_HIGH         RX_DAC_HIGH_FROM_VPP_MV(RX_DEMOD_FSK_DAC_VPP_MV)
#define RX_FSK_CLUSTER_SHIFT    3U
#define RX_FSK_MIN_SPREAD_Q12   24
#define RX_SYMBOL_PHASE_ONE_Q16 65536U

/* ASK 判决改为符号级包络双簇；最小差值调大更抗噪，调小更容易输出。 */
#define RX_ASK_ENV_DC_SHIFT     6U
#define RX_ASK_CLUSTER_SHIFT    3U
#define RX_ASK_MIN_SPREAD       48
#define RX_ASK_DAC_LOW          RX_DAC_LOW_FROM_VPP_MV(RX_DEMOD_ASK_DAC_VPP_MV)
#define RX_ASK_DAC_HIGH         RX_DAC_HIGH_FROM_VPP_MV(RX_DEMOD_ASK_DAC_VPP_MV)

static int32_t g_fsk_i_dc = 0;
static int32_t g_fsk_q_dc = 0;
static uint8_t g_fsk_iq_dc_valid = 0U;

static int32_t g_fsk_freq_dc = 0;
static int32_t g_fsk_smooth = 0;
static int32_t g_fsk_prev_i = 0;
static int32_t g_fsk_prev_q = 0;
static uint8_t g_fsk_prev_valid = 0U;
static int64_t g_fsk_sym_acc = 0;
static uint32_t g_fsk_sym_count = 0U;
static uint32_t g_fsk_symbol_phase_q16 = 0U;
static uint32_t g_fsk_symbol_step_q16 = 1U;
static int32_t g_fsk_low_est = 0;
static int32_t g_fsk_high_est = 0;
static int32_t g_fsk_threshold = 0;
static uint8_t g_fsk_cluster_valid = 0U;
static uint16_t g_fsk_last_dac = RX_FSK_DAC_LOW;

static int32_t g_ask_env_dc = 0;
static uint8_t g_ask_env_dc_valid = 0U;
static int64_t g_ask_sym_acc = 0;
static uint32_t g_ask_sym_count = 0U;
static uint32_t g_ask_symbol_phase_q16 = 0U;
static uint32_t g_ask_symbol_step_q16 = 1U;
static int32_t g_ask_low_est = 0;
static int32_t g_ask_high_est = 0;
static int32_t g_ask_threshold = 0;
static uint8_t g_ask_cluster_valid = 0U;
static uint16_t g_ask_last_dac = RX_ASK_DAC_LOW;

static uint32_t g_sample_rate_hz = 480000U;
static RxMode g_rx_mode = RX_MODE_AM;
static uint32_t g_rx_symbol_rate_hz = RX_DEFAULT_SYMBOL_RATE_HZ;
static int32_t g_rx_low_if_hz = 0;
static uint32_t g_rx_fsk_separation_hz = 0U;
static int32_t g_env_dc_q8 = 0;
static uint8_t g_env_dc_valid = 0U;

static int32_t g_fm_dc = 0;
static int32_t g_fm_smooth = 0;
static int32_t g_fm_prev_i = 0;
static int32_t g_fm_prev_q = 0;
static uint8_t g_fm_prev_valid = 0U;

#define RX_FM_CMSIS_BLOCK_MAX      RX_DEMOD_MAX_BLOCK_SAMPLES
#define RX_FM_CMSIS_PI             3.14159265358979323846f
#define RX_FM_CMSIS_TWO_PI         6.28318530717958647692f
#define RX_FM_CMSIS_DAC_GAIN       1000.0f
#define RX_FM_CMSIS_LPF_ALPHA      0.5f
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static float32_t g_fm_i_f32[RX_FM_CMSIS_BLOCK_MAX];
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static float32_t g_fm_q_f32[RX_FM_CMSIS_BLOCK_MAX];
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static float32_t g_fm_dphi_f32[RX_FM_CMSIS_BLOCK_MAX];

static float32_t g_fm_prev_phase = 0.0f;
static float32_t g_fm_lpf = 0.0f;
static uint8_t g_fm_phase_valid = 0U;




#define RX_PSK_TWO_PI           6.28318530717958647692f
#define RX_PSK_IQ_DC_SHIFT      10U
#define RX_PSK_DAC_LOW          RX_DAC_LOW_FROM_VPP_MV(RX_DEMOD_PSK_DAC_VPP_MV)
#define RX_PSK_DAC_HIGH         RX_DAC_HIGH_FROM_VPP_MV(RX_DEMOD_PSK_DAC_VPP_MV)
#define RX_PSK_RESIDUAL_LOOP_GAIN 0.02f
#define RX_PSK_AXIS_LOOP_GAIN     0.05f

static int32_t g_psk_i_dc = 0;
static int32_t g_psk_q_dc = 0;
static uint8_t g_psk_iq_dc_valid = 0U;

static float g_psk_nco_phase = 0.0f;
static float g_psk_nco_step = 0.0f;
static uint8_t g_psk_nco_valid = 0U;
static float g_psk_residual_phase = 0.0f;
static float g_psk_residual_step = 0.0f;
static float g_psk_prev_z2_i = 0.0f;
static float g_psk_prev_z2_q = 0.0f;
static uint8_t g_psk_z2_valid = 0U;

static int64_t g_psk_sym_i_acc = 0;
static int64_t g_psk_sym_q_acc = 0;
static uint32_t g_psk_sym_count = 0U;
static uint32_t g_psk_symbol_phase_q16 = 0U;
static uint32_t g_psk_symbol_step_q16 = 1U;
static float g_psk_axis_m20 = 1.0f;
static float g_psk_axis_m11 = 0.0f;
static uint8_t g_psk_axis_valid = 0U;

static uint16_t g_psk_last_dac = RX_PSK_DAC_LOW;




static int32_t rx_iq_fm_discriminator(int32_t i_now, int32_t q_now)
{
    int32_t di;
    int32_t dq;
    int64_t num;
    int64_t den;
    int64_t fm_q;

    if (g_fm_prev_valid == 0U)
    {
        g_fm_prev_i = i_now;
        g_fm_prev_q = q_now;
        g_fm_prev_valid = 1U;
        return 0;
    }

    di = i_now - g_fm_prev_i;
    dq = q_now - g_fm_prev_q;

    num = ((int64_t)i_now * (int64_t)dq) - ((int64_t)q_now * (int64_t)di);
    den = ((int64_t)i_now * (int64_t)i_now) + ((int64_t)q_now * (int64_t)q_now);

    g_fm_prev_i = i_now;
    g_fm_prev_q = q_now;

    if (den < 64)
    {
        return 0;
    }

    /*
     * 放大后再除，保留动态范围。
     * 这里输出大约是 Q12 格式的相位差。
     */
    fm_q = (num << 12) / den;

    if (fm_q > 2147483647LL)
    {
        return 2147483647;
    }

    if (fm_q < -2147483647LL)
    {
        return -2147483647;
    }

    return (int32_t)fm_q;
}

static int32_t rx_shift_round_s32(int32_t value, uint32_t shift)
{
    int32_t half;

    if (shift == 0U)
    {
        return value;
    }

    half = (int32_t)(1UL << (shift - 1U));
    if (value >= 0)
    {
        return (value + half) >> shift;
    }

    return -(((-value) + half) >> shift);
}

static uint32_t rx_demod_resolve_symbol_rate(uint32_t symbol_rate_hz)
{
    uint32_t max_symbol_rate_hz = g_sample_rate_hz / 2U;

    if (max_symbol_rate_hz < RX_SYMBOL_RATE_MIN_HZ)
    {
        max_symbol_rate_hz = RX_SYMBOL_RATE_MIN_HZ;
    }

    if ((symbol_rate_hz < RX_SYMBOL_RATE_MIN_HZ) ||
        (symbol_rate_hz > max_symbol_rate_hz))
    {
        return RX_DEFAULT_SYMBOL_RATE_HZ;
    }

    return symbol_rate_hz;
}

static uint32_t rx_demod_symbol_step_q16(void)
{
    uint32_t symbol_rate_hz = rx_demod_resolve_symbol_rate(g_rx_symbol_rate_hz);
    uint64_t step_q16;

    if (g_sample_rate_hz == 0U)
    {
        return 1U;
    }

    step_q16 = ((uint64_t)symbol_rate_hz << 16) / (uint64_t)g_sample_rate_hz;
    if (step_q16 == 0ULL)
    {
        step_q16 = 1ULL;
    }

    if (step_q16 > RX_SYMBOL_PHASE_ONE_Q16)
    {
        step_q16 = RX_SYMBOL_PHASE_ONE_Q16;
    }

    return (uint32_t)step_q16;
}

static int32_t rx_fsk_min_spread_q12(void)
{
    int32_t expected_spread_q12;

    if ((g_rx_fsk_separation_hz == 0U) || (g_sample_rate_hz == 0U))
    {
        return RX_FSK_MIN_SPREAD_Q12;
    }

    /*
     * 鉴频输出近似为 Q12 的相邻采样相位差：
     * spread ~= 2*pi*f_sep/Fs*4096，25736 约等于 2*pi*4096。
     * 这里只取期望频差的 1/5 作为“已看到双簇”的下限，避免噪声抖动当成 FSK 双态。
     */
    expected_spread_q12 = (int32_t)(((uint64_t)g_rx_fsk_separation_hz * 25736ULL) /
                                    (uint64_t)g_sample_rate_hz);
    expected_spread_q12 /= 5;

    if (expected_spread_q12 < RX_FSK_MIN_SPREAD_Q12)
    {
        expected_spread_q12 = RX_FSK_MIN_SPREAD_Q12;
    }

    return expected_spread_q12;
}

static int32_t rx_fsk_discriminator(int32_t i_now, int32_t q_now)
{
    int32_t di;
    int32_t dq;
    int64_t num;
    int64_t den;
    int64_t freq_q12;

    if (g_fsk_prev_valid == 0U)
    {
        g_fsk_prev_i = i_now;
        g_fsk_prev_q = q_now;
        g_fsk_prev_valid = 1U;
        return 0;
    }

    di = i_now - g_fsk_prev_i;
    dq = q_now - g_fsk_prev_q;

    num = ((int64_t)i_now * (int64_t)dq) - ((int64_t)q_now * (int64_t)di);
    den = ((int64_t)i_now * (int64_t)i_now) + ((int64_t)q_now * (int64_t)q_now);

    g_fsk_prev_i = i_now;
    g_fsk_prev_q = q_now;

    if (den < 64)
    {
        return 0;
    }

    freq_q12 = (num << 12) / den;

    if (freq_q12 > 2147483647LL)
    {
        return 2147483647;
    }

    if (freq_q12 < -2147483647LL)
    {
        return -2147483647;
    }

    return (int32_t)freq_q12;
}

static void rx_fsk_update_clusters(int32_t sym_mean)
{
    if (g_fsk_cluster_valid == 0U)
    {
        g_fsk_low_est = sym_mean;
        g_fsk_high_est = sym_mean;
        g_fsk_threshold = sym_mean;
        g_fsk_cluster_valid = 1U;
        return;
    }

    if (sym_mean < g_fsk_low_est)
    {
        g_fsk_low_est = sym_mean;
    }
    else if (sym_mean < g_fsk_threshold)
    {
        g_fsk_low_est += rx_shift_round_s32(sym_mean - g_fsk_low_est, RX_FSK_CLUSTER_SHIFT);
    }

    if (sym_mean > g_fsk_high_est)
    {
        g_fsk_high_est = sym_mean;
    }
    else if (sym_mean >= g_fsk_threshold)
    {
        g_fsk_high_est += rx_shift_round_s32(sym_mean - g_fsk_high_est, RX_FSK_CLUSTER_SHIFT);
    }

    g_fsk_threshold = (int32_t)(((int64_t)g_fsk_low_est + (int64_t)g_fsk_high_est) / 2LL);
}

static void rx_ask_update_clusters(int32_t sym_mean)
{
    if (g_ask_cluster_valid == 0U)
    {
        g_ask_low_est = sym_mean;
        g_ask_high_est = sym_mean;
        g_ask_threshold = sym_mean;
        g_ask_cluster_valid = 1U;
        return;
    }

    if (sym_mean < g_ask_low_est)
    {
        g_ask_low_est = sym_mean;
    }
    else if (sym_mean < g_ask_threshold)
    {
        g_ask_low_est += rx_shift_round_s32(sym_mean - g_ask_low_est, RX_ASK_CLUSTER_SHIFT);
    }

    if (sym_mean > g_ask_high_est)
    {
        g_ask_high_est = sym_mean;
    }
    else if (sym_mean >= g_ask_threshold)
    {
        g_ask_high_est += rx_shift_round_s32(sym_mean - g_ask_high_est, RX_ASK_CLUSTER_SHIFT);
    }

    g_ask_threshold = (int32_t)(((int64_t)g_ask_low_est + (int64_t)g_ask_high_est) / 2LL);
}

static int32_t __attribute__((unused)) rx_iq_phase_diff_freq(int32_t i_now, int32_t q_now)
{
    int32_t di;
    int32_t dq;
    int64_t num;
    int64_t den;
    int64_t freq_q12;

    if (g_fm_prev_valid == 0U)
    {
        g_fm_prev_i = i_now;
        g_fm_prev_q = q_now;
        g_fm_prev_valid = 1U;
        return 0;
    }

    di = i_now - g_fm_prev_i;
    dq = q_now - g_fm_prev_q;

    num = ((int64_t)i_now * (int64_t)dq) - ((int64_t)q_now * (int64_t)di);
    den = ((int64_t)i_now * (int64_t)i_now) + ((int64_t)q_now * (int64_t)q_now);

    g_fm_prev_i = i_now;
    g_fm_prev_q = q_now;

    if (den < 256)
    {
        return 0;
    }

    freq_q12 = (num << 12) / den;

    if (freq_q12 > 2147483647LL)
    {
        return 2147483647;
    }

    if (freq_q12 < -2147483647LL)
    {
        return -2147483647;
    }

    return (int32_t)freq_q12;
}

static uint16_t rx_clip_to_dac(int32_t v)
{
    if (v < (int32_t)RX_DAC_MIN)
    {
        return RX_DAC_MIN;
    }

    if (v > (int32_t)RX_DAC_MAX)
    {
        return RX_DAC_MAX;
    }

    return (uint16_t)v;
}

/* 按目标峰峰值对 DAC 波形做比例微调，中心点保持在 RX_DAC_CENTER。 */
static int32_t rx_scale_dac_vpp(int32_t dac_code, uint32_t target_vpp_mv, uint32_t ref_vpp_mv)
{
    int32_t centered;
    int64_t scaled;

    if ((target_vpp_mv == ref_vpp_mv) || (ref_vpp_mv == 0U))
    {
        return dac_code;
    }

    centered = dac_code - (int32_t)RX_DAC_CENTER;
    scaled = ((int64_t)centered * (int64_t)target_vpp_mv) / (int64_t)ref_vpp_mv;

    return (int32_t)RX_DAC_CENTER + (int32_t)scaled;
}

static uint32_t rx_isqrt_u32(uint32_t x)
{
    uint32_t op = x;
    uint32_t res = 0U;
    uint32_t one = 1UL << 30;

    while (one > op)
    {
        one >>= 2;
    }

    while (one != 0U)
    {
        if (op >= res + one)
        {
            op -= res + one;
            res = (res >> 1) + one;
        }
        else
        {
            res >>= 1;
        }
        one >>= 2;
    }

    return res;
}

static uint32_t rx_demod_limit_count(uint32_t n)
{
    if (n > RX_DEMOD_MAX_BLOCK_SAMPLES)
    {
        return RX_DEMOD_MAX_BLOCK_SAMPLES;
    }

    return n;
}

void RxDemod_Reset(void)
{
    g_env_dc_q8 = 0;
    g_env_dc_valid = 0U;

    g_fm_dc = 0;
    g_fm_smooth = 0;
    g_fm_prev_i = 0;
    g_fm_prev_q = 0;
    g_fm_prev_valid = 0U;
    g_fm_prev_phase = 0.0f;
    g_fm_lpf = 0.0f;
    g_fm_phase_valid = 0U;

    g_fsk_i_dc = 0;
    g_fsk_q_dc = 0;
    g_fsk_iq_dc_valid = 0U;

    g_fsk_freq_dc = 0;
    g_fsk_smooth = 0;
    g_fsk_prev_i = 0;
    g_fsk_prev_q = 0;
    g_fsk_prev_valid = 0U;
    g_fsk_sym_acc = 0;
    g_fsk_sym_count = 0U;
    g_fsk_symbol_phase_q16 = 0U;
    g_fsk_symbol_step_q16 = rx_demod_symbol_step_q16();
    g_fsk_low_est = 0;
    g_fsk_high_est = 0;
    g_fsk_threshold = 0;
    g_fsk_cluster_valid = 0U;
    g_fsk_last_dac = RX_FSK_DAC_LOW;

    g_ask_env_dc = 0;
    g_ask_env_dc_valid = 0U;
    g_ask_sym_acc = 0;
    g_ask_sym_count = 0U;
    g_ask_symbol_phase_q16 = 0U;
    g_ask_symbol_step_q16 = rx_demod_symbol_step_q16();
    g_ask_low_est = 0;
    g_ask_high_est = 0;
    g_ask_threshold = 0;
    g_ask_cluster_valid = 0U;
    g_ask_last_dac = RX_ASK_DAC_LOW;

    g_psk_i_dc = 0;
    g_psk_q_dc = 0;
    g_psk_iq_dc_valid = 0U;

    g_psk_nco_phase = 0.0f;
    g_psk_nco_step = 0.0f;
    g_psk_nco_valid = 0U;
    g_psk_residual_phase = 0.0f;
    g_psk_residual_step = 0.0f;
    g_psk_prev_z2_i = 0.0f;
    g_psk_prev_z2_q = 0.0f;
    g_psk_z2_valid = 0U;

    g_psk_sym_i_acc = 0;
    g_psk_sym_q_acc = 0;
    g_psk_sym_count = 0U;
    g_psk_symbol_phase_q16 = 0U;
    g_psk_symbol_step_q16 = rx_demod_symbol_step_q16();

    g_psk_axis_m20 = 1.0f;
    g_psk_axis_m11 = 0.0f;
    g_psk_axis_valid = 0U;

    g_psk_last_dac = RX_PSK_DAC_LOW;
}

void RxDemod_Init(uint32_t sample_rate_hz)
{
    g_sample_rate_hz = sample_rate_hz;
    g_rx_mode = RX_MODE_AM;
    g_rx_symbol_rate_hz = RX_DEFAULT_SYMBOL_RATE_HZ;
    g_rx_low_if_hz = 0;
    g_rx_fsk_separation_hz = 0U;
    RxDemod_Reset();
}

void RxDemod_SetMode(RxMode mode)
{
    if (g_rx_mode != mode)
    {
        RxDemod_Reset();
    }

    g_rx_mode = mode;
}

void RxDemod_ConfigureSignal(uint32_t symbol_rate_hz,
                             int32_t low_if_hz,
                             uint32_t fsk_separation_hz)
{
    g_rx_symbol_rate_hz = rx_demod_resolve_symbol_rate(symbol_rate_hz);
    g_rx_low_if_hz = low_if_hz;
    g_rx_fsk_separation_hz = fsk_separation_hz;
    RxDemod_Reset();
}

void RxDemod_AM_ProcessBlock(const uint16_t *i_adc, const uint16_t *q_adc, uint32_t n, uint16_t *dac_out)
{
    uint32_t i;
    uint32_t sum_i = 0U;
    uint32_t sum_q = 0U;
    int32_t dc_i;
    int32_t dc_q;

    (void)g_sample_rate_hz;

    if ((i_adc == 0U) || (q_adc == 0U) || (dac_out == 0U) || (n == 0U))
    {
        return;
    }

    n = rx_demod_limit_count(n);

    if (g_rx_mode == RX_MODE_LOOPBACK)
    {
        for (i = 0U; i < n; ++i)
        {
            dac_out[i] = (i_adc[i] > RX_ADC_MAX) ? RX_DAC_MAX : i_adc[i];
        }
        return;
    }

    for (i = 0U; i < n; ++i)
    {
        sum_i += i_adc[i];
        sum_q += q_adc[i];
    }

    dc_i = (int32_t)(sum_i / n);
    dc_q = (int32_t)(sum_q / n);

    for (i = 0U; i < n; ++i)
    {
        int32_t i_val = (int32_t)i_adc[i] - dc_i;
        int32_t q_val = (int32_t)q_adc[i] - dc_q;
        uint32_t mag2 = (uint32_t)((i_val * i_val) + (q_val * q_val));
        int32_t env = (int32_t)rx_isqrt_u32(mag2);
        int32_t env_q8 = env << 8;
        int32_t audio_q8;
        int32_t y;

        if (g_env_dc_valid == 0U)
        {
            g_env_dc_q8 = env_q8;
            g_env_dc_valid = 1U;
        }
        else
        {
            g_env_dc_q8 += (env_q8 - g_env_dc_q8) >> RX_ENV_DC_SHIFT;
        }

        audio_q8 = env_q8 - g_env_dc_q8;

        y = (int32_t)RX_DAC_CENTER + ((audio_q8 * RX_AUDIO_GAIN_Q8) >> 16);
        y = rx_scale_dac_vpp(y, RX_DEMOD_AM_DAC_VPP_MV, RX_DEMOD_ANALOG_REF_VPP_MV);

        dac_out[i] = rx_clip_to_dac(y);
    }
}

void RxDemod_ASK_ProcessBlock(const uint16_t *i_adc,
                              const uint16_t *q_adc,
                              uint32_t n,
                              uint16_t *dac_out)
{
    uint32_t i;
    uint32_t sum_i = 0U;
    uint32_t sum_q = 0U;
    int32_t dc_i;
    int32_t dc_q;

    if ((i_adc == 0U) || (q_adc == 0U) || (dac_out == 0U) || (n == 0U))
    {
        return;
    }

    n = rx_demod_limit_count(n);

    if (g_ask_symbol_step_q16 == 0U)
    {
        g_ask_symbol_step_q16 = rx_demod_symbol_step_q16();
    }

    for (i = 0U; i < n; ++i)
    {
        sum_i += i_adc[i];
        sum_q += q_adc[i];
    }

    dc_i = (int32_t)(sum_i / n);
    dc_q = (int32_t)(sum_q / n);

    for (i = 0U; i < n; ++i)
    {
        int32_t i_val = (int32_t)i_adc[i] - dc_i;
        int32_t q_val = (int32_t)q_adc[i] - dc_q;
        uint32_t mag2 = (uint32_t)((i_val * i_val) + (q_val * q_val));
        int32_t env = (int32_t)rx_isqrt_u32(mag2);
        int32_t env_ac;

        if (g_ask_env_dc_valid == 0U)
        {
            g_ask_env_dc = env;
            g_ask_env_dc_valid = 1U;
        }
        else
        {
            g_ask_env_dc += rx_shift_round_s32(env - g_ask_env_dc, RX_ASK_ENV_DC_SHIFT);
        }

        env_ac = env - g_ask_env_dc;
        g_ask_sym_acc += env_ac;
        g_ask_sym_count++;
        g_ask_symbol_phase_q16 += g_ask_symbol_step_q16;

        if (g_ask_symbol_phase_q16 >= RX_SYMBOL_PHASE_ONE_Q16)
        {
            int32_t sym_mean = (int32_t)(g_ask_sym_acc / (int64_t)g_ask_sym_count);
            int32_t spread;

            g_ask_symbol_phase_q16 -= RX_SYMBOL_PHASE_ONE_Q16;
            rx_ask_update_clusters(sym_mean);

            spread = g_ask_high_est - g_ask_low_est;
            if (spread < 0)
            {
                spread = -spread;
            }

            if (spread >= RX_ASK_MIN_SPREAD)
            {
                g_ask_last_dac = (sym_mean >= g_ask_threshold) ? RX_ASK_DAC_HIGH : RX_ASK_DAC_LOW;
            }

            g_ask_sym_acc = 0;
            g_ask_sym_count = 0U;
        }

        dac_out[i] = g_ask_last_dac;
    }
}

void RxDemod_FM_ProcessBlock(const uint16_t *i_adc,
                             const uint16_t *q_adc,
                             uint32_t n,
                             uint16_t *dac_out)
{
    uint32_t i;
    uint32_t sum_i = 0U;
    uint32_t sum_q = 0U;
    int32_t dc_i;
    int32_t dc_q;

    if ((i_adc == 0U) || (q_adc == 0U) || (dac_out == 0U) || (n == 0U))
    {
        return;
    }

    n = rx_demod_limit_count(n);

    for (i = 0U; i < n; ++i)
    {
        sum_i += i_adc[i];
        sum_q += q_adc[i];
    }

    dc_i = (int32_t)(sum_i / n);
    dc_q = (int32_t)(sum_q / n);

    for (i = 0U; i < n; ++i)
    {
        int32_t i_now = (int32_t)i_adc[i] - dc_i;
        int32_t q_now = (int32_t)q_adc[i] - dc_q;
        int32_t fm_raw;
        int32_t fm_ac;
        int32_t y;

        fm_raw = rx_iq_fm_discriminator(i_now, q_now);

        /* 去掉鉴频后的慢变化直流，避免 DAC 偏移漂移 */
        g_fm_dc += (fm_raw - g_fm_dc) >> RX_FM_DC_SHIFT;
        fm_ac = fm_raw - g_fm_dc;

        /* 很轻的平滑，保留 10kHz 调制波 */
        g_fm_smooth += (fm_ac - g_fm_smooth) >> RX_FM_SMOOTH_SHIFT;

        y = (int32_t)RX_DAC_CENTER + (g_fm_smooth >> RX_FM_DAC_GAIN_SHIFT);
        y = rx_scale_dac_vpp(y, RX_DEMOD_FM_DAC_VPP_MV, RX_DEMOD_ANALOG_REF_VPP_MV);

        dac_out[i] = rx_clip_to_dac(y);
    }
}

void RxDemod_FM_CMSIS_ProcessBlock(const uint16_t *i_adc,
                                   const uint16_t *q_adc,
                                   uint32_t n,
                                   uint16_t *dac_out)
{
    uint32_t i;
    float32_t mean_i;
    float32_t mean_q;
    float32_t mean_dphi;

    if ((i_adc == 0U) || (q_adc == 0U) || (dac_out == 0U) || (n == 0U))
    {
        return;
    }

    n = rx_demod_limit_count(n);

    for (i = 0U; i < n; ++i)
    {
        g_fm_i_f32[i] = (float32_t)i_adc[i];
        g_fm_q_f32[i] = (float32_t)q_adc[i];
    }

    arm_mean_f32(g_fm_i_f32, n, &mean_i);
    arm_mean_f32(g_fm_q_f32, n, &mean_q);

    arm_offset_f32(g_fm_i_f32, -mean_i, g_fm_i_f32, n);
    arm_offset_f32(g_fm_q_f32, -mean_q, g_fm_q_f32, n);

    for (i = 0U; i < n; ++i)
    {
        float32_t phase;
        float32_t dphi;

        phase = atan2f(g_fm_q_f32[i], g_fm_i_f32[i]);

        if (g_fm_phase_valid == 0U)
        {
            g_fm_prev_phase = phase;
            g_fm_phase_valid = 1U;
            g_fm_dphi_f32[i] = 0.0f;
            continue;
        }

        dphi = phase - g_fm_prev_phase;
        g_fm_prev_phase = phase;

        if (dphi > RX_FM_CMSIS_PI*0.8f)
        {
            dphi -= RX_FM_CMSIS_TWO_PI;
        }
        else if (dphi < -RX_FM_CMSIS_PI*0.8f)
        {
            dphi += RX_FM_CMSIS_TWO_PI;
        }

        g_fm_dphi_f32[i] = dphi;
    }

    arm_mean_f32(g_fm_dphi_f32, n, &mean_dphi);
    arm_offset_f32(g_fm_dphi_f32, -mean_dphi, g_fm_dphi_f32, n);

    for (i = 0U; i < n; ++i)
    {
        int32_t y;

        g_fm_lpf += RX_FM_CMSIS_LPF_ALPHA * (g_fm_dphi_f32[i] - g_fm_lpf);

        y = (int32_t)((float32_t)RX_DAC_CENTER +
                      (g_fm_lpf * RX_FM_CMSIS_DAC_GAIN));
        y = rx_scale_dac_vpp(y, RX_DEMOD_FM_DAC_VPP_MV, RX_DEMOD_ANALOG_REF_VPP_MV);

        dac_out[i] = rx_clip_to_dac(y);
    }
}

void RxDemod_FSK_ProcessBlock(const uint16_t *i_adc,
                              const uint16_t *q_adc,
                              uint32_t n,
                              uint16_t *dac_out)
{
    uint32_t i;
    uint32_t sum_i = 0U;
    uint32_t sum_q = 0U;
    int32_t dc_i;
    int32_t dc_q;

    if ((i_adc == 0U) || (q_adc == 0U) || (dac_out == 0U) || (n == 0U))
    {
        return;
    }

    n = rx_demod_limit_count(n);

    for (i = 0U; i < n; ++i)
    {
        sum_i += i_adc[i];
        sum_q += q_adc[i];
    }

    dc_i = (int32_t)(sum_i / n);
    dc_q = (int32_t)(sum_q / n);

    if (g_fsk_iq_dc_valid == 0U)
    {
        g_fsk_i_dc = dc_i;
        g_fsk_q_dc = dc_q;
        g_fsk_iq_dc_valid = 1U;
    }
    else
    {
        g_fsk_i_dc += rx_shift_round_s32(dc_i - g_fsk_i_dc, RX_FSK_IQ_DC_SHIFT);
        g_fsk_q_dc += rx_shift_round_s32(dc_q - g_fsk_q_dc, RX_FSK_IQ_DC_SHIFT);
    }

    if (g_fsk_symbol_step_q16 == 0U)
    {
        g_fsk_symbol_step_q16 = rx_demod_symbol_step_q16();
    }

    for (i = 0U; i < n; ++i)
    {
        int32_t i_now = (int32_t)i_adc[i] - g_fsk_i_dc;
        int32_t q_now = (int32_t)q_adc[i] - g_fsk_q_dc;
        int32_t freq_raw;

        freq_raw = rx_fsk_discriminator(i_now, q_now);
        g_fsk_smooth += rx_shift_round_s32(freq_raw - g_fsk_smooth, RX_FSK_SMOOTH_SHIFT);

        g_fsk_sym_acc += g_fsk_smooth;
        g_fsk_sym_count++;
        g_fsk_symbol_phase_q16 += g_fsk_symbol_step_q16;

        if (g_fsk_symbol_phase_q16 >= RX_SYMBOL_PHASE_ONE_Q16)
        {
            int32_t sym_mean = (int32_t)(g_fsk_sym_acc / (int64_t)g_fsk_sym_count);
            int32_t spread;

            g_fsk_symbol_phase_q16 -= RX_SYMBOL_PHASE_ONE_Q16;

            rx_fsk_update_clusters(sym_mean);
            spread = g_fsk_high_est - g_fsk_low_est;
            if (spread < 0)
            {
                spread = -spread;
            }

            /*
             * 无码头时，数字输出整体反向是允许的；这里只保证输出跟随两个频率簇，
             * 不强行规定“高频=1”还是“低频=1”。
             */
            if (spread >= rx_fsk_min_spread_q12())
            {
                g_fsk_last_dac = (sym_mean >= g_fsk_threshold) ? RX_FSK_DAC_HIGH : RX_FSK_DAC_LOW;
            }

            g_fsk_sym_acc = 0;
            g_fsk_sym_count = 0U;
        }

        dac_out[i] = g_fsk_last_dac;
    }
}


// void RxDemod_FSK_ProcessBlock(const uint16_t *i_adc,
//                               const uint16_t *q_adc,
//                               uint32_t n,
//                               uint16_t *dac_out)
// {
//     uint32_t i;
//     uint32_t sum_i = 0U;
//     uint32_t sum_q = 0U;
//     int32_t blk_mean_i;
//     int32_t blk_mean_q;

//     static int64_t sym_acc = 0;
//     static uint32_t sym_count = 0U;
//     static uint32_t samples_per_symbol = 48U;
//     static int32_t threshold = 0;
//     static uint8_t threshold_valid = 0U;
//     static uint16_t last_out = 1200U;

//     if ((i_adc == 0U) || (q_adc == 0U) || (dac_out == 0U) || (n == 0U))
//     {
//         return;
//     }

//     samples_per_symbol = g_sample_rate_hz / 10000U;
//     if (samples_per_symbol == 0U)
//     {
//         samples_per_symbol = 1U;
//     }

//     for (i = 0U; i < n; ++i)
//     {
//         sum_i += i_adc[i];
//         sum_q += q_adc[i];
//     }

//     blk_mean_i = (int32_t)(sum_i / n);
//     blk_mean_q = (int32_t)(sum_q / n);

//     if (g_fsk_iq_dc_valid == 0U)
//     {
//         g_fsk_i_dc = blk_mean_i;
//         g_fsk_q_dc = blk_mean_q;
//         g_fsk_iq_dc_valid = 1U;
//     }
//     else
//     {
//         g_fsk_i_dc += (blk_mean_i - g_fsk_i_dc) >> RX_FSK_IQ_DC_SHIFT;
//         g_fsk_q_dc += (blk_mean_q - g_fsk_q_dc) >> RX_FSK_IQ_DC_SHIFT;
//     }

//     for (i = 0U; i < n; ++i)
//     {
//         int32_t i_now = (int32_t)i_adc[i] - g_fsk_i_dc;
//         int32_t q_now = (int32_t)q_adc[i] - g_fsk_q_dc;
//         int32_t freq_raw;
//         int32_t freq_ac;

//         freq_raw = rx_iq_phase_diff_freq(i_now, q_now);

//         g_fsk_freq_dc += (freq_raw - g_fsk_freq_dc) >> RX_FSK_FREQ_DC_SHIFT;
//         freq_ac = freq_raw - g_fsk_freq_dc;

//         /*
//          * FSK 符号积分：跨 block 连续累计，不在 4096 点边界重置。
//          */
//         sym_acc += freq_ac;
//         sym_count++;

//         if (sym_count >= samples_per_symbol)
//         {
//             int32_t sym_mean = (int32_t)(sym_acc / (int64_t)sym_count);

//             /*
//              * 慢速阈值跟踪，避免中心频偏或 I/Q 偏差导致固定 0 阈值失效。
//              */
//             if (threshold_valid == 0U)
//             {
//                 threshold = sym_mean;
//                 threshold_valid = 1U;
//             }
//             else
//             {
//                 threshold += (sym_mean - threshold) >> 4;
//             }

//             if (sym_mean > threshold)
//             {
//                 last_out = 2800U;
//             }
//             else
//             {
//                 last_out = 1200U;
//             }

//             sym_acc = 0;
//             sym_count = 0U;
//         }

//         dac_out[i] = last_out;
//     }
// }


void RxDemod_PSK_ProcessBlock(const uint16_t *i_adc,
                              const uint16_t *q_adc,
                              uint32_t n,
                              uint16_t *dac_out)
{
    uint32_t i;
    uint32_t sum_i = 0U;
    uint32_t sum_q = 0U;
    int32_t blk_mean_i;
    int32_t blk_mean_q;

    if ((i_adc == 0U) || (q_adc == 0U) || (dac_out == 0U) || (n == 0U))
    {
        return;
    }

    n = rx_demod_limit_count(n);

    if (g_psk_symbol_step_q16 == 0U)
    {
        g_psk_symbol_step_q16 = rx_demod_symbol_step_q16();
    }

    if (g_psk_nco_valid == 0U)
    {
        g_psk_nco_step = RX_PSK_TWO_PI * (float)g_rx_low_if_hz / (float)g_sample_rate_hz;
        g_psk_nco_phase = 0.0f;
        g_psk_nco_valid = 1U;
    }

    for (i = 0U; i < n; ++i)
    {
        sum_i += i_adc[i];
        sum_q += q_adc[i];
    }

    blk_mean_i = (int32_t)(sum_i / n);
    blk_mean_q = (int32_t)(sum_q / n);

    if (g_psk_iq_dc_valid == 0U)
    {
        g_psk_i_dc = blk_mean_i;
        g_psk_q_dc = blk_mean_q;
        g_psk_iq_dc_valid = 1U;
    }
    else
    {
        g_psk_i_dc += rx_shift_round_s32(blk_mean_i - g_psk_i_dc, RX_PSK_IQ_DC_SHIFT);
        g_psk_q_dc += rx_shift_round_s32(blk_mean_q - g_psk_q_dc, RX_PSK_IQ_DC_SHIFT);
    }

    for (i = 0U; i < n; ++i)
    {
        int32_t i_now = (int32_t)i_adc[i] - g_psk_i_dc;
        int32_t q_now = (int32_t)q_adc[i] - g_psk_q_dc;

        float c = cosf(g_psk_nco_phase);
        float s = sinf(g_psk_nco_phase);
        float bb_i_f;
        float bb_q_f;
        float z2_i;
        float z2_q;
        float rc;
        float rs;
        float comp_i;
        float comp_q;

        /* 下变频：rx * exp(-j*w*n)，low_if_hz 可以为负。 */
        bb_i_f = ((float)i_now * c) + ((float)q_now * s);
        bb_q_f = ((float)q_now * c) - ((float)i_now * s);

        /*
         * BPSK 平方环路估计残余频偏。
         * 平方后数据相位被消掉，只留下两倍载波残差；再乘 0.5 得到原始残差步进。
         */
        z2_i = (bb_i_f * bb_i_f) - (bb_q_f * bb_q_f);
        z2_q = 2.0f * bb_i_f * bb_q_f;
        if (g_psk_z2_valid != 0U)
        {
            float dot = (z2_i * g_psk_prev_z2_i) + (z2_q * g_psk_prev_z2_q);
            float cross = (z2_q * g_psk_prev_z2_i) - (z2_i * g_psk_prev_z2_q);
            float residual_step_est = 0.5f * atan2f(cross, dot);

            g_psk_residual_step += RX_PSK_RESIDUAL_LOOP_GAIN * (residual_step_est - g_psk_residual_step);
        }
        g_psk_prev_z2_i = z2_i;
        g_psk_prev_z2_q = z2_q;
        g_psk_z2_valid = 1U;

        g_psk_residual_phase += g_psk_residual_step;
        if (g_psk_residual_phase >= RX_PSK_TWO_PI)
        {
            g_psk_residual_phase -= RX_PSK_TWO_PI;
        }
        else if (g_psk_residual_phase < 0.0f)
        {
            g_psk_residual_phase += RX_PSK_TWO_PI;
        }

        rc = cosf(g_psk_residual_phase);
        rs = sinf(g_psk_residual_phase);
        comp_i = (bb_i_f * rc) + (bb_q_f * rs);
        comp_q = (bb_q_f * rc) - (bb_i_f * rs);

        g_psk_sym_i_acc += (int32_t)comp_i;
        g_psk_sym_q_acc += (int32_t)comp_q;
        g_psk_sym_count++;

        g_psk_nco_phase += g_psk_nco_step;
        if (g_psk_nco_phase >= RX_PSK_TWO_PI)
        {
            g_psk_nco_phase -= RX_PSK_TWO_PI;
        }
        else if (g_psk_nco_phase < 0.0f)
        {
            g_psk_nco_phase += RX_PSK_TWO_PI;
        }

        g_psk_symbol_phase_q16 += g_psk_symbol_step_q16;
        if (g_psk_symbol_phase_q16 >= RX_SYMBOL_PHASE_ONE_Q16)
        {
            float sym_i;
            float sym_q;
            float m20;
            float m11;
            float axis_angle;
            float projection;

            g_psk_symbol_phase_q16 -= RX_SYMBOL_PHASE_ONE_Q16;
            if (g_psk_sym_count == 0U)
            {
                dac_out[i] = g_psk_last_dac;
                continue;
            }

            sym_i = (float)g_psk_sym_i_acc / (float)g_psk_sym_count;
            sym_q = (float)g_psk_sym_q_acc / (float)g_psk_sym_count;
            m20 = (sym_i * sym_i) - (sym_q * sym_q);
            m11 = 2.0f * sym_i * sym_q;

            if (g_psk_axis_valid == 0U)
            {
                g_psk_axis_m20 = m20;
                g_psk_axis_m11 = m11;
                g_psk_axis_valid = 1U;
            }
            else
            {
                g_psk_axis_m20 += RX_PSK_AXIS_LOOP_GAIN * (m20 - g_psk_axis_m20);
                g_psk_axis_m11 += RX_PSK_AXIS_LOOP_GAIN * (m11 - g_psk_axis_m11);
            }

            axis_angle = 0.5f * atan2f(g_psk_axis_m11, g_psk_axis_m20);
            projection = (sym_i * cosf(axis_angle)) + (sym_q * sinf(axis_angle));

            /*
             * 无码头 BPSK 的整体极性天然不确定；这里只输出相干判决后的双电平，
             * 不强行规定哪个相位必须对应逻辑 1。
             */
            g_psk_last_dac = (projection < 0.0f) ? RX_PSK_DAC_HIGH : RX_PSK_DAC_LOW;

            g_psk_sym_i_acc = 0;
            g_psk_sym_q_acc = 0;
            g_psk_sym_count = 0U;
        }

        dac_out[i] = g_psk_last_dac;
    }
}
