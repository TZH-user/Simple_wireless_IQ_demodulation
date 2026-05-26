#include "rx_demod.h"
#include "arm_math.h"
#include "dsp/fast_math_functions.h"
#include "RtosTypes.h"
#include <math.h>
#include <stdio.h>


#define RX_ADC_MAX       4095U
#define RX_DAC_MIN       0U
#define RX_DAC_MAX       4095U
#define RX_DAC_CENTER    2048U
#define RX_DAC_VREF_MV   3300U
#define RX_ENV_DC_SHIFT  13U
#define RX_AUDIO_GAIN_Q8 256
#define RX_AUDIO_SMOOTH_SHIFT 1U
/* 低算力算法预留开关：默认关闭，打开前需要逐项上板验证波形、误码和 demod_perf。 */
#define RX_DEMOD_LOW_COMPUTE_ENABLE       0U
#define RX_AM_ENV_APPROX_ENABLE           0U
#define RX_ASK_ENV_APPROX_ENABLE          0U
#define RX_FSK_FAST_DISC_ENABLE           0U
#define RX_PSK_LOW_TRIG_ENABLE            0U
#define RX_PSK_DD_AXIS_ENABLE             0U

/* 专项 debug 配置：性能验收时保持 NONE，只在单独排查某一类调制时切到对应 profile。 */
#define RX_DEMOD_DEBUG_PROFILE_NONE       0U
#define RX_DEMOD_DEBUG_PROFILE_AM         1U
#define RX_DEMOD_DEBUG_PROFILE_ASK        2U
#define RX_DEMOD_DEBUG_PROFILE_FSK        3U
#define RX_DEMOD_DEBUG_PROFILE_PSK        4U
#ifndef RX_DEMOD_DEBUG_PROFILE
#define RX_DEMOD_DEBUG_PROFILE            RX_DEMOD_DEBUG_PROFILE_NONE
#endif

#define RX_AM_DEBUG_LOG_ENABLE  ((RX_DEMOD_DEBUG_PROFILE == RX_DEMOD_DEBUG_PROFILE_AM) ? 1U : 0U)
#define RX_ASK_DEBUG_LOG_ENABLE ((RX_DEMOD_DEBUG_PROFILE == RX_DEMOD_DEBUG_PROFILE_ASK) ? 1U : 0U)
#define RX_FSK_DEBUG_LOG_ENABLE ((RX_DEMOD_DEBUG_PROFILE == RX_DEMOD_DEBUG_PROFILE_FSK) ? 1U : 0U)
#define RX_PSK_DEBUG_LOG_ENABLE ((RX_DEMOD_DEBUG_PROFILE == RX_DEMOD_DEBUG_PROFILE_PSK) ? 1U : 0U)
#define RX_AM_DEBUG_LOG_FIRST_BLOCKS 8U
#define RX_AM_DEBUG_LOG_INTERVAL_BLOCKS 128U

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
#define RX_DEMOD_FSK_OUT_VPP_MV  1000U
#define RX_DEMOD_PSK_OUT_VPP_MV  100U
/* 各调制输出偏移，单位 mV；0 表示仍以 DAC 中点 1.65V 为中心。 */
#define RX_DEMOD_AM_OUT_OFFSET_MV   0
#define RX_DEMOD_FM_OUT_OFFSET_MV   0
#define RX_DEMOD_ASK_OUT_OFFSET_MV  0
#define RX_DEMOD_FSK_OUT_OFFSET_MV  0
#define RX_DEMOD_PSK_OUT_OFFSET_MV  0
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
#define RX_DAC_OFFSET_CODE_FROM_MV(offset_mv) \
    (((int32_t)(offset_mv) * (int32_t)RX_DAC_MAX) / (int32_t)RX_DAC_VREF_MV)
#define RX_DAC_CENTER_FROM_OFFSET_MV(offset_mv) \
    ((((int32_t)RX_DAC_CENTER + RX_DAC_OFFSET_CODE_FROM_MV(offset_mv)) < (int32_t)RX_DAC_MIN) ? (int32_t)RX_DAC_MIN : \
     ((((int32_t)RX_DAC_CENTER + RX_DAC_OFFSET_CODE_FROM_MV(offset_mv)) > (int32_t)RX_DAC_MAX) ? (int32_t)RX_DAC_MAX : \
      ((int32_t)RX_DAC_CENTER + RX_DAC_OFFSET_CODE_FROM_MV(offset_mv))))
#define RX_DAC_LOW_FROM_VPP_OFFSET_MV(vpp_mv, offset_mv) \
    (((RX_DAC_CENTER_FROM_OFFSET_MV(offset_mv) - (int32_t)RX_DAC_HALF_SPAN_FROM_VPP_MV(vpp_mv)) < (int32_t)RX_DAC_MIN) ? RX_DAC_MIN : \
     (uint16_t)(RX_DAC_CENTER_FROM_OFFSET_MV(offset_mv) - (int32_t)RX_DAC_HALF_SPAN_FROM_VPP_MV(vpp_mv)))
#define RX_DAC_HIGH_FROM_VPP_OFFSET_MV(vpp_mv, offset_mv) \
    (((RX_DAC_CENTER_FROM_OFFSET_MV(offset_mv) + (int32_t)RX_DAC_HALF_SPAN_FROM_VPP_MV(vpp_mv)) > (int32_t)RX_DAC_MAX) ? RX_DAC_MAX : \
     (uint16_t)(RX_DAC_CENTER_FROM_OFFSET_MV(offset_mv) + (int32_t)RX_DAC_HALF_SPAN_FROM_VPP_MV(vpp_mv)))

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
#define RX_FSK_DECIMATE         4U /* FSK 每 4 个原始采样点鉴频一次，DAC 仍按原采样率输出，主要用于降低实时运算量。 */
#define RX_FSK_DAC_LOW          RX_DAC_LOW_FROM_VPP_OFFSET_MV(RX_DEMOD_FSK_DAC_VPP_MV, RX_DEMOD_FSK_OUT_OFFSET_MV)
#define RX_FSK_DAC_HIGH         RX_DAC_HIGH_FROM_VPP_OFFSET_MV(RX_DEMOD_FSK_DAC_VPP_MV, RX_DEMOD_FSK_OUT_OFFSET_MV)
#define RX_FSK_CLUSTER_SHIFT    3U
#define RX_FSK_MIN_SPREAD_Q12   24
#define RX_FSK_WARMUP_SYMBOLS   24U /* FSK 初始收集若干符号均值建立双簇，避免刚启动时输出随机翻转。 */
#define RX_FSK_DEBUG_LOG_FIRST_SYMBOLS 32U /* 刚进入 FSK 时连续输出前若干个符号，方便确认 warmup 是否正常前进。 */
#define RX_FSK_DEBUG_LOG_INTERVAL_SYMBOLS 1000U /* 稳定运行后每隔多少个符号输出一次，数值越小串口日志越密。 */
#define RX_SYMBOL_PHASE_ONE_Q16 65536U

/* ASK 判决改为符号级包络双簇；最小差值调大更抗噪，调小更容易输出。 */
#define RX_ASK_ENV_DC_SHIFT     6U
#define RX_ASK_CLUSTER_SHIFT    3U
#define RX_ASK_MIN_SPREAD       48
#define RX_ASK_DEBUG_LOG_FIRST_SYMBOLS 32U
#define RX_ASK_DEBUG_LOG_INTERVAL_SYMBOLS 1000U
#define RX_ASK_DAC_LOW          RX_DAC_LOW_FROM_VPP_OFFSET_MV(RX_DEMOD_ASK_DAC_VPP_MV, RX_DEMOD_ASK_OUT_OFFSET_MV)
#define RX_ASK_DAC_HIGH         RX_DAC_HIGH_FROM_VPP_OFFSET_MV(RX_DEMOD_ASK_DAC_VPP_MV, RX_DEMOD_ASK_OUT_OFFSET_MV)

/*
 * 增强模式比较器参数。
 * DC_SHIFT 越大，自适应中心跟踪越慢，越不容易被单个边沿拖动；越小越能跟随慢漂移。
 * THRESHOLD_CODE 是相对自适应中心的触发幅度，越大越不容易被小毛刺触发。
 * HYST_CODE 是 DAC 码值滞回宽度，越大越不容易在非符号跳变区域误翻转。
 */
#define RX_ASK_ANALOG_SQUARE_DC_SHIFT   6U
#define RX_ASK_ANALOG_SQUARE_THRESHOLD_CODE 40
#define RX_ASK_ANALOG_SQUARE_HYST_CODE  12
#define RX_FSK_ANALOG_SQUARE_DC_SHIFT   8U
#define RX_FSK_ANALOG_SQUARE_THRESHOLD_CODE 0
#define RX_FSK_ANALOG_SQUARE_HYST_CODE  12

typedef struct
{
    int32_t dc_q8;
    uint8_t dc_valid;
    uint16_t last_dac;
} RxAnalogSquareState;

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
static uint32_t g_fsk_warmup_count = 0U;
static int32_t g_fsk_warmup_min = 0;
static int32_t g_fsk_warmup_max = 0;
static uint16_t g_fsk_last_dac = RX_FSK_DAC_LOW;
static uint32_t g_fsk_debug_symbol_count = 0U;
#if (RX_FSK_DEBUG_LOG_ENABLE != 0U)
static int32_t g_fsk_sym_raw_min = 0;
static int32_t g_fsk_sym_raw_max = 0;
static int32_t g_fsk_sym_smooth_min = 0;
static int32_t g_fsk_sym_smooth_max = 0;
static int32_t g_fsk_sym_i_abs_max = 0;
static int32_t g_fsk_sym_q_abs_max = 0;
static uint8_t g_fsk_sym_stats_valid = 0U;
#endif

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
static uint32_t g_ask_debug_symbol_count = 0U;

static RxAnalogSquareState g_ask_analog_square_state = {0, 0U, RX_ASK_DAC_LOW};
static RxAnalogSquareState g_fsk_analog_square_state = {0, 0U, RX_FSK_DAC_LOW};

static uint32_t g_sample_rate_hz = 480000U;
static RxMode g_rx_mode = RX_MODE_AM;
static uint32_t g_rx_symbol_rate_hz = RX_DEFAULT_SYMBOL_RATE_HZ;
static int32_t g_rx_low_if_hz = 0;
static uint32_t g_rx_fsk_separation_hz = 0U;
static int32_t g_env_dc_q8 = 0;
static uint8_t g_env_dc_valid = 0U;
static uint32_t g_am_debug_block_count = 0U;

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
#define RX_PSK_DAC_LOW          RX_DAC_LOW_FROM_VPP_OFFSET_MV(RX_DEMOD_PSK_DAC_VPP_MV, RX_DEMOD_PSK_OUT_OFFSET_MV)
#define RX_PSK_DAC_HIGH         RX_DAC_HIGH_FROM_VPP_OFFSET_MV(RX_DEMOD_PSK_DAC_VPP_MV, RX_DEMOD_PSK_OUT_OFFSET_MV)
#define RX_PSK_RESIDUAL_LOOP_GAIN 0.02f
#define RX_PSK_AXIS_LOOP_GAIN     0.05f
#define RX_PSK_Q15_SCALE          32767.0f
#define RX_PSK_DEBUG_LOG_FIRST_SYMBOLS 32U
#define RX_PSK_DEBUG_LOG_INTERVAL_SYMBOLS 1000U

static int32_t g_psk_i_dc = 0;
static int32_t g_psk_q_dc = 0;
static uint8_t g_psk_iq_dc_valid = 0U;

static float g_psk_nco_phase = 0.0f;
static float g_psk_nco_step = 0.0f;
static int32_t g_psk_nco_c_q15 = 32767;
static int32_t g_psk_nco_s_q15 = 0;
static int32_t g_psk_step_c_q15 = 32767;
static int32_t g_psk_step_s_q15 = 0;
static uint8_t g_psk_nco_valid = 0U;
static float g_psk_residual_step = 0.0f;
static float g_psk_prev_z2_i = 0.0f;
static float g_psk_prev_z2_q = 0.0f;
static uint8_t g_psk_z2_valid = 0U;
static float g_psk_z2_dot_sum = 0.0f;
static float g_psk_z2_cross_sum = 0.0f;
static uint32_t g_psk_z2_count = 0U;

static int64_t g_psk_sym_i_acc = 0;
static int64_t g_psk_sym_q_acc = 0;
static uint32_t g_psk_sym_count = 0U;
static uint32_t g_psk_symbol_phase_q16 = 0U;
static uint32_t g_psk_symbol_step_q16 = 1U;
static float g_psk_axis_m20 = 1.0f;
static float g_psk_axis_m11 = 0.0f;
static uint8_t g_psk_axis_valid = 0U;

static uint16_t g_psk_last_dac = RX_PSK_DAC_LOW;
static uint32_t g_psk_debug_symbol_count = 0U;




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

#if (RX_FSK_DEBUG_LOG_ENABLE != 0U)
static int32_t rx_abs_s32(int32_t value)
{
    if (value < 0)
    {
        return -value;
    }

    return value;
}
#endif

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

static uint32_t rx_demod_symbol_step_q16_by_rate(uint32_t proc_sample_rate_hz)
{
    uint32_t symbol_rate_hz = rx_demod_resolve_symbol_rate(g_rx_symbol_rate_hz);
    uint64_t step_q16;

    if (proc_sample_rate_hz == 0U)
    {
        return 1U;
    }

    step_q16 = ((uint64_t)symbol_rate_hz << 16) / (uint64_t)proc_sample_rate_hz;
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

static uint32_t rx_fsk_proc_sample_rate_hz(void)
{
    uint32_t decimate = (RX_FSK_DECIMATE == 0U) ? 1U : RX_FSK_DECIMATE;
    uint32_t proc_rate_hz = g_sample_rate_hz / decimate;

    return (proc_rate_hz == 0U) ? 1U : proc_rate_hz;
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

#if (RX_AM_DEBUG_LOG_ENABLE != 0U)
static void rx_am_debug_log_block(int32_t dc_i,
                                  int32_t dc_q,
                                  int32_t env_min,
                                  int32_t env_max,
                                  int32_t env_dc,
                                  int32_t audio_min,
                                  int32_t audio_max,
                                  uint16_t dac_min,
                                  uint16_t dac_max,
                                  uint32_t clip_count)
{
    char log_buf[128];
    int n;

    g_am_debug_block_count++;
    if ((g_am_debug_block_count > RX_AM_DEBUG_LOG_FIRST_BLOCKS) &&
        ((g_am_debug_block_count % RX_AM_DEBUG_LOG_INTERVAL_BLOCKS) != 0U))
    {
        return;
    }

    n = snprintf(log_buf,
                 sizeof(log_buf),
                 "am_dbg:b=%lu,dc=%ld/%ld,e=%ld/%ld,edc=%ld,a=%ld/%ld,d=%u/%u,clip=%lu\r\n",
                 (unsigned long)g_am_debug_block_count,
                 (long)dc_i,
                 (long)dc_q,
                 (long)env_min,
                 (long)env_max,
                 (long)env_dc,
                 (long)audio_min,
                 (long)audio_max,
                 (unsigned int)dac_min,
                 (unsigned int)dac_max,
                 (unsigned long)clip_count);
    if ((n > 0) && ((size_t)n < sizeof(log_buf)))
    {
        print_queue_send(log_buf);
    }
}
#endif

#if (RX_ASK_DEBUG_LOG_ENABLE != 0U)
static void rx_ask_debug_log_symbol(int32_t sym_mean, uint32_t sym_count, int32_t spread)
{
    char log_buf[128];
    const char *dac_text;
    int n;

    g_ask_debug_symbol_count++;
    if ((g_ask_debug_symbol_count > RX_ASK_DEBUG_LOG_FIRST_SYMBOLS) &&
        ((g_ask_debug_symbol_count % RX_ASK_DEBUG_LOG_INTERVAL_SYMBOLS) != 0U))
    {
        return;
    }

    dac_text = (g_ask_last_dac == RX_ASK_DAC_HIGH) ? "high" : "low";
    n = snprintf(log_buf,
                 sizeof(log_buf),
                 "ask_dbg:sr=%lu,st=%lu,c=%lu,m=%ld,lo=%ld,hi=%ld,th=%ld,sp=%ld,d=%s\r\n",
                 (unsigned long)g_rx_symbol_rate_hz,
                 (unsigned long)g_ask_symbol_step_q16,
                 (unsigned long)sym_count,
                 (long)sym_mean,
                 (long)g_ask_low_est,
                 (long)g_ask_high_est,
                 (long)g_ask_threshold,
                 (long)spread,
                 dac_text);
    if ((n > 0) && ((size_t)n < sizeof(log_buf)))
    {
        print_queue_send(log_buf);
    }
}
#endif

#if (RX_FSK_DEBUG_LOG_ENABLE != 0U)
static void rx_fsk_debug_reset_symbol_stats(void)
{
    g_fsk_sym_raw_min = 0;
    g_fsk_sym_raw_max = 0;
    g_fsk_sym_smooth_min = 0;
    g_fsk_sym_smooth_max = 0;
    g_fsk_sym_i_abs_max = 0;
    g_fsk_sym_q_abs_max = 0;
    g_fsk_sym_stats_valid = 0U;
}

static void rx_fsk_debug_accum_sample(int32_t freq_raw,
                                      int32_t freq_smooth,
                                      int32_t i_now,
                                      int32_t q_now)
{
    int32_t i_abs = rx_abs_s32(i_now);
    int32_t q_abs = rx_abs_s32(q_now);

    if (g_fsk_sym_stats_valid == 0U)
    {
        g_fsk_sym_raw_min = freq_raw;
        g_fsk_sym_raw_max = freq_raw;
        g_fsk_sym_smooth_min = freq_smooth;
        g_fsk_sym_smooth_max = freq_smooth;
        g_fsk_sym_i_abs_max = i_abs;
        g_fsk_sym_q_abs_max = q_abs;
        g_fsk_sym_stats_valid = 1U;
        return;
    }

    if (freq_raw < g_fsk_sym_raw_min)
    {
        g_fsk_sym_raw_min = freq_raw;
    }
    if (freq_raw > g_fsk_sym_raw_max)
    {
        g_fsk_sym_raw_max = freq_raw;
    }
    if (freq_smooth < g_fsk_sym_smooth_min)
    {
        g_fsk_sym_smooth_min = freq_smooth;
    }
    if (freq_smooth > g_fsk_sym_smooth_max)
    {
        g_fsk_sym_smooth_max = freq_smooth;
    }
    if (i_abs > g_fsk_sym_i_abs_max)
    {
        g_fsk_sym_i_abs_max = i_abs;
    }
    if (q_abs > g_fsk_sym_q_abs_max)
    {
        g_fsk_sym_q_abs_max = q_abs;
    }
}

static void rx_fsk_debug_log_symbol(int32_t sym_mean,
                                    uint32_t sym_count,
                                    int32_t spread,
                                    int32_t min_spread)
{
    char log_buf[128];
    const char *dac_text;
    int n;

    g_fsk_debug_symbol_count++;
    if ((g_fsk_debug_symbol_count > RX_FSK_DEBUG_LOG_FIRST_SYMBOLS) &&
        ((g_fsk_debug_symbol_count % RX_FSK_DEBUG_LOG_INTERVAL_SYMBOLS) != 0U))
    {
        return;
    }

    dac_text = (g_fsk_last_dac == RX_FSK_DAC_HIGH) ? "high" : "low";
    n = snprintf(log_buf,
                 sizeof(log_buf),
                 "fsk_dbg:sr=%lu,st=%lu,w=%lu,c=%lu,m=%ld,lo=%ld,hi=%ld,th=%ld,sp=%ld,mi=%ld,d=%s\r\n",
                 (unsigned long)g_rx_symbol_rate_hz,
                 (unsigned long)g_fsk_symbol_step_q16,
                 (unsigned long)g_fsk_warmup_count,
                 (unsigned long)sym_count,
                 (long)sym_mean,
                 (long)g_fsk_low_est,
                 (long)g_fsk_high_est,
                 (long)g_fsk_threshold,
                 (long)spread,
                 (long)min_spread,
                 dac_text);
    if ((n > 0) && ((size_t)n < sizeof(log_buf)))
    {
        print_queue_send(log_buf);
    }

    n = snprintf(log_buf,
                 sizeof(log_buf),
                 "fsk_rng:raw=%ld/%ld,sm=%ld/%ld,iq=%ld/%ld,dc=%ld/%ld\r\n",
                 (long)g_fsk_sym_raw_min,
                 (long)g_fsk_sym_raw_max,
                 (long)g_fsk_sym_smooth_min,
                 (long)g_fsk_sym_smooth_max,
                 (long)g_fsk_sym_i_abs_max,
                 (long)g_fsk_sym_q_abs_max,
                 (long)g_fsk_i_dc,
                 (long)g_fsk_q_dc);
    if ((n > 0) && ((size_t)n < sizeof(log_buf)))
    {
        print_queue_send(log_buf);
    }
}
#endif

#if (RX_PSK_DEBUG_LOG_ENABLE != 0U)
static void rx_psk_debug_log_symbol(float sym_i,
                                    float sym_q,
                                    float projection,
                                    float axis_angle,
                                    uint32_t sym_count)
{
    char log_buf[128];
    const char *dac_text;
    int32_t axis_mrad;
    int32_t residual_urad;
    int n;

    g_psk_debug_symbol_count++;
    if ((g_psk_debug_symbol_count > RX_PSK_DEBUG_LOG_FIRST_SYMBOLS) &&
        ((g_psk_debug_symbol_count % RX_PSK_DEBUG_LOG_INTERVAL_SYMBOLS) != 0U))
    {
        return;
    }

    axis_mrad = (int32_t)(axis_angle * 1000.0f);
    residual_urad = (int32_t)(g_psk_residual_step * 1000000.0f);
    dac_text = (g_psk_last_dac == RX_PSK_DAC_HIGH) ? "high" : "low";

    n = snprintf(log_buf,
                 sizeof(log_buf),
                 "psk_dbg:sr=%lu,st=%lu,c=%lu,si=%ld,sq=%ld,pr=%ld,ax=%ld,rs=%ld,d=%s\r\n",
                 (unsigned long)g_rx_symbol_rate_hz,
                 (unsigned long)g_psk_symbol_step_q16,
                 (unsigned long)sym_count,
                 (long)((int32_t)sym_i),
                 (long)((int32_t)sym_q),
                 (long)((int32_t)projection),
                 (long)axis_mrad,
                 (long)residual_urad,
                 dac_text);
    if ((n > 0) && ((size_t)n < sizeof(log_buf)))
    {
        print_queue_send(log_buf);
    }
}
#endif

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

static void rx_fsk_accept_symbol(int32_t sym_mean)
{
    if (g_fsk_warmup_count < RX_FSK_WARMUP_SYMBOLS)
    {
        if (g_fsk_warmup_count == 0U)
        {
            g_fsk_warmup_min = sym_mean;
            g_fsk_warmup_max = sym_mean;
        }
        else
        {
            if (sym_mean < g_fsk_warmup_min)
            {
                g_fsk_warmup_min = sym_mean;
            }
            if (sym_mean > g_fsk_warmup_max)
            {
                g_fsk_warmup_max = sym_mean;
            }
        }

        g_fsk_warmup_count++;
        if (g_fsk_warmup_count >= RX_FSK_WARMUP_SYMBOLS)
        {
            g_fsk_low_est = g_fsk_warmup_min;
            g_fsk_high_est = g_fsk_warmup_max;
            g_fsk_threshold = (int32_t)(((int64_t)g_fsk_low_est + (int64_t)g_fsk_high_est) / 2LL);
            g_fsk_cluster_valid = 1U;
        }
        return;
    }

    rx_fsk_update_clusters(sym_mean);
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

static int32_t rx_float_to_q15(float value)
{
    if (value >= 0.999969f)
    {
        return 32767;
    }
    if (value <= -1.0f)
    {
        return -32768;
    }

    return (int32_t)(value * RX_PSK_Q15_SCALE);
}

static void rx_psk_update_nco_step(void)
{
    g_psk_step_c_q15 = rx_float_to_q15(cosf(g_psk_nco_step));
    g_psk_step_s_q15 = rx_float_to_q15(sinf(g_psk_nco_step));
}

static void rx_psk_rotate_nco(void)
{
    int32_t c = g_psk_nco_c_q15;
    int32_t s = g_psk_nco_s_q15;
    int32_t next_c = (int32_t)((((int64_t)c * g_psk_step_c_q15) -
                                ((int64_t)s * g_psk_step_s_q15)) >> 15);
    int32_t next_s = (int32_t)((((int64_t)s * g_psk_step_c_q15) +
                                ((int64_t)c * g_psk_step_s_q15)) >> 15);

    g_psk_nco_c_q15 = next_c;
    g_psk_nco_s_q15 = next_s;
}

/* 按目标峰峰值对 DAC 波形做比例微调，中心点保持在 RX_DAC_CENTER。 */
static int32_t rx_scale_dac_vpp_offset(int32_t dac_code,
                                       uint32_t target_vpp_mv,
                                       uint32_t ref_vpp_mv,
                                       int32_t offset_mv)
{
    int32_t centered;
    int32_t target_center;
    int64_t scaled;

    target_center = RX_DAC_CENTER_FROM_OFFSET_MV(offset_mv);
    if (ref_vpp_mv == 0U)
    {
        return target_center;
    }

    centered = dac_code - (int32_t)RX_DAC_CENTER;
    scaled = ((int64_t)centered * (int64_t)target_vpp_mv) / (int64_t)ref_vpp_mv;

    return target_center + (int32_t)scaled;
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

/* 对模拟解调输出做自适应中心比较，给数字增强模式输出稳定高低电平。 */
static void rx_analog_square_from_dac(uint16_t *dac_out,
                                      uint32_t n,
                                      uint16_t low_code,
                                      uint16_t high_code,
                                      RxAnalogSquareState *state,
                                      uint8_t dc_shift,
                                      int32_t threshold_code,
                                      int32_t hyst_code)
{
    uint32_t i;

    if ((dac_out == NULL) || (state == NULL) || (n == 0U))
    {
        return;
    }

    for (i = 0U; i < n; ++i)
    {
        int32_t sample = (int32_t)dac_out[i];
        int32_t sample_q8 = sample << 8;
        int32_t center;
        int32_t high_th;
        int32_t low_th;

        if (state->dc_valid == 0U)
        {
            state->dc_q8 = sample_q8;
            state->dc_valid = 1U;
            state->last_dac = low_code;
        }
        else
        {
            state->dc_q8 += rx_shift_round_s32(sample_q8 - state->dc_q8, dc_shift);
        }

        center = (state->dc_q8 >> 8);
        high_th = center + threshold_code + (hyst_code / 2);
        low_th = center + threshold_code - (hyst_code / 2);

        if (state->last_dac == high_code)
        {
            if (sample < low_th)
            {
                state->last_dac = low_code;
            }
        }
        else
        {
            if (sample > high_th)
            {
                state->last_dac = high_code;
            }
        }

        dac_out[i] = state->last_dac;
    }
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
    g_fsk_symbol_step_q16 = rx_demod_symbol_step_q16_by_rate(rx_fsk_proc_sample_rate_hz());
    g_fsk_low_est = 0;
    g_fsk_high_est = 0;
    g_fsk_threshold = 0;
    g_fsk_cluster_valid = 0U;
    g_fsk_warmup_count = 0U;
    g_fsk_warmup_min = 0;
    g_fsk_warmup_max = 0;
    g_fsk_last_dac = RX_FSK_DAC_LOW;
    g_fsk_debug_symbol_count = 0U;
#if (RX_FSK_DEBUG_LOG_ENABLE != 0U)
    rx_fsk_debug_reset_symbol_stats();
#endif

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
    g_ask_debug_symbol_count = 0U;

    g_ask_analog_square_state.dc_q8 = 0;
    g_ask_analog_square_state.dc_valid = 0U;
    g_ask_analog_square_state.last_dac = RX_ASK_DAC_LOW;
    g_fsk_analog_square_state.dc_q8 = 0;
    g_fsk_analog_square_state.dc_valid = 0U;
    g_fsk_analog_square_state.last_dac = RX_FSK_DAC_LOW;

    g_psk_i_dc = 0;
    g_psk_q_dc = 0;
    g_psk_iq_dc_valid = 0U;

    g_psk_nco_phase = 0.0f;
    g_psk_nco_step = 0.0f;
    g_psk_nco_c_q15 = 32767;
    g_psk_nco_s_q15 = 0;
    g_psk_step_c_q15 = 32767;
    g_psk_step_s_q15 = 0;
    g_psk_nco_valid = 0U;
    g_psk_residual_step = 0.0f;
    g_psk_prev_z2_i = 0.0f;
    g_psk_prev_z2_q = 0.0f;
    g_psk_z2_valid = 0U;
    g_psk_z2_dot_sum = 0.0f;
    g_psk_z2_cross_sum = 0.0f;
    g_psk_z2_count = 0U;

    g_psk_sym_i_acc = 0;
    g_psk_sym_q_acc = 0;
    g_psk_sym_count = 0U;
    g_psk_symbol_phase_q16 = 0U;
    g_psk_symbol_step_q16 = rx_demod_symbol_step_q16();

    g_psk_axis_m20 = 1.0f;
    g_psk_axis_m11 = 0.0f;
    g_psk_axis_valid = 0U;

    g_psk_last_dac = RX_PSK_DAC_LOW;
    g_psk_debug_symbol_count = 0U;

    g_am_debug_block_count = 0U;
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
#if (RX_AM_DEBUG_LOG_ENABLE != 0U)
    int32_t env_min = 0;
    int32_t env_max = 0;
    int32_t audio_min = 0;
    int32_t audio_max = 0;
    uint16_t dac_min = RX_DAC_MAX;
    uint16_t dac_max = RX_DAC_MIN;
    uint32_t clip_count = 0U;
    uint8_t debug_valid = 0U;
#endif

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
        uint16_t dac_value;

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

#if (RX_AM_DEBUG_LOG_ENABLE != 0U)
        if (debug_valid == 0U)
        {
            env_min = env;
            env_max = env;
            audio_min = audio_q8 >> 8;
            audio_max = audio_q8 >> 8;
            debug_valid = 1U;
        }
        else
        {
            int32_t audio_now = audio_q8 >> 8;

            if (env < env_min)
            {
                env_min = env;
            }
            if (env > env_max)
            {
                env_max = env;
            }
            if (audio_now < audio_min)
            {
                audio_min = audio_now;
            }
            if (audio_now > audio_max)
            {
                audio_max = audio_now;
            }
        }
#endif

        y = (int32_t)RX_DAC_CENTER + ((audio_q8 * RX_AUDIO_GAIN_Q8) >> 16);
        y = rx_scale_dac_vpp_offset(y,
                                    RX_DEMOD_AM_DAC_VPP_MV,
                                    RX_DEMOD_ANALOG_REF_VPP_MV,
                                    RX_DEMOD_AM_OUT_OFFSET_MV);

#if (RX_AM_DEBUG_LOG_ENABLE != 0U)
        if ((y < (int32_t)RX_DAC_MIN) || (y > (int32_t)RX_DAC_MAX))
        {
            clip_count++;
        }
#endif
        dac_value = rx_clip_to_dac(y);
        dac_out[i] = dac_value;

#if (RX_AM_DEBUG_LOG_ENABLE != 0U)
        if (dac_value < dac_min)
        {
            dac_min = dac_value;
        }
        if (dac_value > dac_max)
        {
            dac_max = dac_value;
        }
#endif
    }

#if (RX_AM_DEBUG_LOG_ENABLE != 0U)
    rx_am_debug_log_block(dc_i,
                          dc_q,
                          env_min,
                          env_max,
                          g_env_dc_q8 >> 8,
                          audio_min,
                          audio_max,
                          dac_min,
                          dac_max,
                          clip_count);
#endif
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

#if (RX_ASK_DEBUG_LOG_ENABLE != 0U)
            rx_ask_debug_log_symbol(sym_mean, g_ask_sym_count, spread);
#endif

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
        y = rx_scale_dac_vpp_offset(y,
                                    RX_DEMOD_FM_DAC_VPP_MV,
                                    RX_DEMOD_ANALOG_REF_VPP_MV,
                                    RX_DEMOD_FM_OUT_OFFSET_MV);

        dac_out[i] = rx_clip_to_dac(y);
    }
}

void RxDemod_ASK_AnalogSquare_ProcessBlock(const uint16_t *i_adc,
                                           const uint16_t *q_adc,
                                           uint32_t n,
                                           uint16_t *dac_out)
{
    RxDemod_AM_ProcessBlock(i_adc, q_adc, n, dac_out);
    rx_analog_square_from_dac(dac_out,
                              rx_demod_limit_count(n),
                              RX_ASK_DAC_LOW,
                              RX_ASK_DAC_HIGH,
                              &g_ask_analog_square_state,
                              RX_ASK_ANALOG_SQUARE_DC_SHIFT,
                              RX_ASK_ANALOG_SQUARE_THRESHOLD_CODE,
                              RX_ASK_ANALOG_SQUARE_HYST_CODE);
}

void RxDemod_FSK_AnalogSquare_ProcessBlock(const uint16_t *i_adc,
                                           const uint16_t *q_adc,
                                           uint32_t n,
                                           uint16_t *dac_out)
{
    RxDemod_FM_ProcessBlock(i_adc, q_adc, n, dac_out);
    rx_analog_square_from_dac(dac_out,
                              rx_demod_limit_count(n),
                              RX_FSK_DAC_LOW,
                              RX_FSK_DAC_HIGH,
                              &g_fsk_analog_square_state,
                              RX_FSK_ANALOG_SQUARE_DC_SHIFT,
                              RX_FSK_ANALOG_SQUARE_THRESHOLD_CODE,
                              RX_FSK_ANALOG_SQUARE_HYST_CODE);
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
        y = rx_scale_dac_vpp_offset(y,
                                    RX_DEMOD_FM_DAC_VPP_MV,
                                    RX_DEMOD_ANALOG_REF_VPP_MV,
                                    RX_DEMOD_FM_OUT_OFFSET_MV);

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
        g_fsk_symbol_step_q16 = rx_demod_symbol_step_q16_by_rate(rx_fsk_proc_sample_rate_hz());
    }

    for (i = 0U; i < n; ++i)
    {
        if ((i % RX_FSK_DECIMATE) == 0U)
        {
            int32_t i_now = (int32_t)i_adc[i] - g_fsk_i_dc;
            int32_t q_now = (int32_t)q_adc[i] - g_fsk_q_dc;
            int32_t freq_raw;

            freq_raw = rx_fsk_discriminator(i_now, q_now);
            g_fsk_smooth += rx_shift_round_s32(freq_raw - g_fsk_smooth, RX_FSK_SMOOTH_SHIFT);

#if (RX_FSK_DEBUG_LOG_ENABLE != 0U)
            rx_fsk_debug_accum_sample(freq_raw, g_fsk_smooth, i_now, q_now);
#endif

            g_fsk_sym_acc += g_fsk_smooth;
            g_fsk_sym_count++;
            g_fsk_symbol_phase_q16 += g_fsk_symbol_step_q16;

            if (g_fsk_symbol_phase_q16 >= RX_SYMBOL_PHASE_ONE_Q16)
            {
                int32_t sym_mean = (int32_t)(g_fsk_sym_acc / (int64_t)g_fsk_sym_count);
                int32_t spread;

                g_fsk_symbol_phase_q16 -= RX_SYMBOL_PHASE_ONE_Q16;

                rx_fsk_accept_symbol(sym_mean);
                spread = g_fsk_high_est - g_fsk_low_est;
                if (spread < 0)
                {
                    spread = -spread;
                }

                /*
                 * 无码头时，数字输出整体反向是允许的；这里只保证输出跟随两个频率簇，
                 * 不强行规定“高频=1”还是“低频=1”。
                 */
                {
                    int32_t min_spread = rx_fsk_min_spread_q12();

                    if ((g_fsk_cluster_valid != 0U) && (spread >= min_spread))
                    {
                        g_fsk_last_dac = (sym_mean >= g_fsk_threshold) ? RX_FSK_DAC_HIGH : RX_FSK_DAC_LOW;
                    }

#if (RX_FSK_DEBUG_LOG_ENABLE != 0U)
                    rx_fsk_debug_log_symbol(sym_mean, g_fsk_sym_count, spread, min_spread);
#endif
                }

                g_fsk_sym_acc = 0;
                g_fsk_sym_count = 0U;
#if (RX_FSK_DEBUG_LOG_ENABLE != 0U)
                rx_fsk_debug_reset_symbol_stats();
#endif
            }
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
        g_psk_nco_c_q15 = 32767;
        g_psk_nco_s_q15 = 0;
        rx_psk_update_nco_step();
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

        int32_t c_q15 = g_psk_nco_c_q15;
        int32_t s_q15 = g_psk_nco_s_q15;
        int32_t bb_i;
        int32_t bb_q;
        float z2_i;
        float z2_q;

        /* 下变频：rx * exp(-j*w*n)。NCO 使用定点旋转，避免采样点循环内调用 sinf/cosf。 */
        bb_i = (int32_t)((((int64_t)i_now * c_q15) + ((int64_t)q_now * s_q15)) >> 15);
        bb_q = (int32_t)((((int64_t)q_now * c_q15) - ((int64_t)i_now * s_q15)) >> 15);

        /*
         * BPSK 平方环路只累计 cross/dot，残余频偏估计放到符号边界执行，
         * 避免每个采样点调用 atan2f。
         */
        z2_i = ((float)bb_i * (float)bb_i) - ((float)bb_q * (float)bb_q);
        z2_q = 2.0f * (float)bb_i * (float)bb_q;
        if (g_psk_z2_valid != 0U)
        {
            float dot = (z2_i * g_psk_prev_z2_i) + (z2_q * g_psk_prev_z2_q);
            float cross = (z2_q * g_psk_prev_z2_i) - (z2_i * g_psk_prev_z2_q);
            g_psk_z2_dot_sum += dot;
            g_psk_z2_cross_sum += cross;
            g_psk_z2_count++;
        }
        g_psk_prev_z2_i = z2_i;
        g_psk_prev_z2_q = z2_q;
        g_psk_z2_valid = 1U;

        g_psk_sym_i_acc += bb_i;
        g_psk_sym_q_acc += bb_q;
        g_psk_sym_count++;

        rx_psk_rotate_nco();

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

            if (g_psk_z2_count != 0U)
            {
                float residual_step_est = 0.5f * atan2f(g_psk_z2_cross_sum, g_psk_z2_dot_sum);

                g_psk_residual_step += RX_PSK_RESIDUAL_LOOP_GAIN * (residual_step_est - g_psk_residual_step);
                g_psk_nco_step += g_psk_residual_step;
                rx_psk_update_nco_step();
                g_psk_z2_dot_sum = 0.0f;
                g_psk_z2_cross_sum = 0.0f;
                g_psk_z2_count = 0U;
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

#if (RX_PSK_DEBUG_LOG_ENABLE != 0U)
            rx_psk_debug_log_symbol(sym_i, sym_q, projection, axis_angle, g_psk_sym_count);
#endif

            g_psk_sym_i_acc = 0;
            g_psk_sym_q_acc = 0;
            g_psk_sym_count = 0U;
        }

        dac_out[i] = g_psk_last_dac;
    }
}
