#include "rx_demod.h"
#include "arm_math.h"
#include "dsp/fast_math_functions.h"
#include <math.h>


#define RX_ADC_MAX       4095U
#define RX_DAC_MIN       0U
#define RX_DAC_MAX       4095U
#define RX_DAC_CENTER    2048U
#define RX_ENV_DC_SHIFT  13U
#define RX_AUDIO_GAIN_Q8 256
#define RX_AUDIO_SMOOTH_SHIFT 1U

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
#define RX_FSK_DAC_GAIN_SHIFT   0U

static int32_t g_fsk_i_dc = 0;
static int32_t g_fsk_q_dc = 0;
static uint8_t g_fsk_iq_dc_valid = 0U;

static int32_t g_fsk_freq_dc = 0;
static int32_t g_fsk_smooth = 0;

static uint32_t g_sample_rate_hz = 480000U;
static RxMode g_rx_mode = RX_MODE_AM;
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




#define RX_PSK_SYMBOL_RATE_HZ   10000U
#define RX_PSK_IF_HZ            0.0f   /* 改成你的实际低中频 */
#define RX_PSK_TWO_PI           6.28318530717958647692f
#define RX_PSK_IQ_DC_SHIFT      10U
#define RX_PSK_DAC_LOW          1200U
#define RX_PSK_DAC_HIGH         2800U

static int32_t g_psk_i_dc = 0;
static int32_t g_psk_q_dc = 0;
static uint8_t g_psk_iq_dc_valid = 0U;

static float g_psk_nco_phase = 0.0f;
static float g_psk_nco_step = 0.0f;
static uint8_t g_psk_nco_valid = 0U;

static int64_t g_psk_sym_i_acc = 0;
static int64_t g_psk_sym_q_acc = 0;
static uint32_t g_psk_sym_count = 0U;
static uint32_t g_psk_samples_per_symbol = 48U;

static int64_t g_psk_prev_sym_i = 0;
static int64_t g_psk_prev_sym_q = 0;
static uint8_t g_psk_prev_valid = 0U;

static uint8_t g_psk_bit_state = 0U;
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

    g_psk_i_dc = 0;
    g_psk_q_dc = 0;
    g_psk_iq_dc_valid = 0U;

    g_psk_nco_phase = 0.0f;
    g_psk_nco_step = 0.0f;
    g_psk_nco_valid = 0U;

    g_psk_sym_i_acc = 0;
    g_psk_sym_q_acc = 0;
    g_psk_sym_count = 0U;
    g_psk_samples_per_symbol = g_sample_rate_hz / RX_PSK_SYMBOL_RATE_HZ;
    if (g_psk_samples_per_symbol == 0U)
    {
        g_psk_samples_per_symbol = 1U;
    }

    g_psk_prev_sym_i = 0;
    g_psk_prev_sym_q = 0;
    g_psk_prev_valid = 0U;

    g_psk_bit_state = 0U;
    g_psk_last_dac = RX_PSK_DAC_LOW;
}

void RxDemod_Init(uint32_t sample_rate_hz)
{
    g_sample_rate_hz = sample_rate_hz;
    g_rx_mode = RX_MODE_AM;
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

        dac_out[i] = rx_clip_to_dac(y);
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

        /*
         * 输出到 12bit DAC。
         * 波形太小：RX_FM_DAC_GAIN_SHIFT 改小，比如 4 或 3。
         * 波形削顶：RX_FM_DAC_GAIN_SHIFT 改大，比如 6 或 7。
         */
        y = (int32_t)RX_DAC_CENTER + (g_fm_smooth >> RX_FM_DAC_GAIN_SHIFT);

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

    for (i = 0U; i < n; ++i)
    {
        int32_t i_now = (int32_t)i_adc[i] - dc_i;
        int32_t q_now = (int32_t)q_adc[i] - dc_q;
        int32_t fm_raw;
        int32_t fm_ac;
        int32_t y;

        /* 完全复用 FM 的相位差分鉴频前端 */
        fm_raw = rx_iq_fm_discriminator(i_now, q_now);

        /* 完全复用 FM 的去直流和平滑状态 */
        g_fm_dc += (fm_raw - g_fm_dc) >> RX_FM_DC_SHIFT;
        fm_ac = fm_raw - g_fm_dc;

        g_fm_smooth += (fm_ac - g_fm_smooth) >> RX_FM_SMOOTH_SHIFT;

        /*
         * 只在这里改变 FSK 的显示增益。
         * 数值越小，输出越大。
         */
        y = (int32_t)RX_DAC_CENTER + (g_fm_smooth >> RX_FSK_DAC_GAIN_SHIFT);
        
         dac_out[i] = rx_clip_to_dac(y);
        //if(y>2300)
        //dac_out[i] = 3600;
        //else
        //dac_out[i] = 1800;
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

    g_psk_samples_per_symbol = g_sample_rate_hz / RX_PSK_SYMBOL_RATE_HZ;
    if (g_psk_samples_per_symbol == 0U)
    {
        g_psk_samples_per_symbol = 1U;
    }

    if (g_psk_nco_valid == 0U)
    {
        g_psk_nco_step = RX_PSK_TWO_PI * RX_PSK_IF_HZ / (float)g_sample_rate_hz;
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
        g_psk_i_dc += (blk_mean_i - g_psk_i_dc) >> RX_PSK_IQ_DC_SHIFT;
        g_psk_q_dc += (blk_mean_q - g_psk_q_dc) >> RX_PSK_IQ_DC_SHIFT;
    }

    for (i = 0U; i < n; ++i)
    {
        int32_t i_now = (int32_t)i_adc[i] - g_psk_i_dc;
        int32_t q_now = (int32_t)q_adc[i] - g_psk_q_dc;

        float c = cosf(g_psk_nco_phase);
        float s = sinf(g_psk_nco_phase);

        int32_t bb_i;
        int32_t bb_q;

        /*
         * 下变频：rx * exp(-j*w*n)
         */
        bb_i = (int32_t)(((float)i_now * c) + ((float)q_now * s));
        bb_q = (int32_t)(((float)q_now * c) - ((float)i_now * s));

        g_psk_sym_i_acc += bb_i;
        g_psk_sym_q_acc += bb_q;
        g_psk_sym_count++;

        g_psk_nco_phase += g_psk_nco_step;
        if (g_psk_nco_phase >= RX_PSK_TWO_PI)
        {
            g_psk_nco_phase -= RX_PSK_TWO_PI;
        }

        if (g_psk_sym_count >= g_psk_samples_per_symbol)
        {
            int64_t sym_i = g_psk_sym_i_acc;
            int64_t sym_q = g_psk_sym_q_acc;

            if (g_psk_prev_valid != 0U)
            {
                int64_t dot = (sym_i * g_psk_prev_sym_i) + (sym_q * g_psk_prev_sym_q);

                /*
                 * dot < 0 表示相邻符号相位约 180 度跳变：
                 * 有跳变 -> bit 状态翻转；无跳变 -> 保持。
                 */
                if (dot < 0)
                {
                    g_psk_bit_state ^= 1U;
                }

                g_psk_last_dac = (g_psk_bit_state != 0U) ? RX_PSK_DAC_HIGH : RX_PSK_DAC_LOW;
            }

            g_psk_prev_sym_i = sym_i;
            g_psk_prev_sym_q = sym_q;
            g_psk_prev_valid = 1U;

            g_psk_sym_i_acc = 0;
            g_psk_sym_q_acc = 0;
            g_psk_sym_count = 0U;
        }

        dac_out[i] = g_psk_last_dac;
    }
}
