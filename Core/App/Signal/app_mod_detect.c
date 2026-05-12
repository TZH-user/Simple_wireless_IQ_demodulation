#include "app_mod_detect.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

#include "arm_math.h"
#include "arm_const_structs.h"
#include "RtosTypes.h"

/* 条件编译判断：如果工程没有显式打开 CMSIS FFT，则调制识别不调用未链接的 CFFT 库。 */
#ifndef APP_MODDET_USE_CMSIS_FFT
/* 宏定义说明：APP_MODDET_USE_CMSIS_FFT = 0 表示只使用时域统计特征，避免新增 DSP 链接依赖。 */
#define APP_MODDET_USE_CMSIS_FFT 0U
#endif

#define APP_MODDET_DPHI_HIST_BIN_COUNT 5U
#define APP_MODDET_PHASE_STATE_BIN_COUNT     32U
#define APP_MODDET_FREQ_STATE_BIN_COUNT      32U
#define APP_MODDET_HARMONIC_SEARCH_BINS      6U
#define APP_MODDET_ENVELOPE_FAMILY_GATE_PM   180U
#define APP_MODDET_CW_PHASE_RMS_GATE_PM      12U

typedef struct
{
  app_mod_detect_config_t config;
  app_mod_detect_status_t status;
  uint8_t mode_vote[APP_MODDET_MODE_PSK + 1U];

  uint32_t fft_len;
  const arm_cfft_instance_f32 *cfft;
} app_mod_detect_ctx_t;

typedef struct
{
  float env_buf[APP_MODDET_MAX_BLOCK_SAMPLES];
  float env_smooth_buf[APP_MODDET_MAX_BLOCK_SAMPLES];
  float env_ac_buf[APP_MODDET_MAX_BLOCK_SAMPLES];

  float phase_buf[APP_MODDET_MAX_BLOCK_SAMPLES];
  float phase_base_buf[APP_MODDET_MAX_BLOCK_SAMPLES];
  float dphi_buf[APP_MODDET_MAX_BLOCK_SAMPLES];
  float dphi_ac_buf[APP_MODDET_MAX_BLOCK_SAMPLES];

  float fft_work_buf[APP_MODDET_FFT_LEN * 2U];
  float fft_mag_buf[APP_MODDET_FFT_LEN / 2U];

  float trim_work_buf[APP_MODDET_MAX_BLOCK_SAMPLES];
} app_moddet_workbuf_t;
/* 工作缓冲区较大，单独放到 .moddetect_buffer，并按 32 字节对齐以兼容 Cache/DMA 习惯。 */
__attribute__((section(".moddetect_buffer"))) __attribute__((aligned(32)))
static app_moddet_workbuf_t g_moddet_work;


static app_mod_detect_ctx_t g_moddet;

/* 将 int64 绝对值限制到 uint32_t 范围，避免统计量溢出到 UI 字段。 */
static uint32_t app_moddet_abs_i64_to_u32(int64_t value)
{
  if (value < 0)
  {
    value = -value;
  }

  if (value > 0xFFFFFFFFLL)
  {
    return 0xFFFFFFFFUL;
  }

  return (uint32_t)value;
}

/* 计算两个无符号 64 位量的差值，并限制到 uint32_t。 */
static uint32_t app_moddet_abs_diff_u64(uint64_t a, uint64_t b)
{
  uint64_t diff = (a > b) ? (a - b) : (b - a);

  if (diff > 0xFFFFFFFFULL)
  {
    return 0xFFFFFFFFUL;
  }

  return (uint32_t)diff;
}

/* 计算百分比，分母为 0 时返回 0。 */
static uint32_t app_moddet_percent_u32(uint32_t numerator, uint32_t denominator)
{
  if (denominator == 0U)
  {
    return 0U;
  }

  return (uint32_t)(((uint64_t)numerator * 100ULL) / denominator);
}

/* 计算千分比，分母为 0 时返回 0。 */
static uint32_t app_moddet_permille_u64(uint64_t numerator, uint64_t denominator)
{
  if (denominator == 0ULL)
  {
    return 0U;
  }

  return (uint32_t)((numerator * 1000ULL) / denominator);
}

/* 64 位除法并四舍五入，用于速率和频偏换算。 */
static uint32_t app_moddet_div_round_u64(uint64_t numerator, uint64_t denominator)
{
  if (denominator == 0ULL)
  {
    return 0U;
  }

  return (uint32_t)((numerator + (denominator / 2ULL)) / denominator);
}

/* 整数平方根，用于把包络功率范围换算为近似幅度范围。 */
static uint32_t app_moddet_isqrt_u64(uint64_t value)
{
  uint64_t bit = 1ULL << 62;
  uint64_t root = 0ULL;

  while (bit > value)
  {
    bit >>= 2;
  }

  while (bit != 0ULL)
  {
    if (value >= (root + bit))
    {
      value -= root + bit;
      root = (root >> 1) + bit;
    }
    else
    {
      root >>= 1;
    }
    bit >>= 2;
  }

  if (root > 0xFFFFFFFFULL)
  {
    return 0xFFFFFFFFUL;
  }

  return (uint32_t)root;
}

static uint32_t app_moddet_max_u32(uint32_t a, uint32_t b)
{
  return (a > b) ? a : b;
}

static uint32_t app_moddet_count_bits_u32(uint32_t value)
{
  uint32_t count = 0U;

  while (value != 0U)
  {
    count += (value & 1U);
    value >>= 1U;
  }

  return count;
}

/* 统计粗略 dphi 直方图中的有效峰数，用于区分 PSK/FSK/FM 特征。 */
static uint16_t app_moddet_count_hist_peaks(const uint32_t *hist, uint32_t hist_len, uint32_t sample_cnt)
{
  uint16_t peak_count = 0U;
  uint32_t idx;
  uint32_t min_peak_count;

  if ((hist == 0) || (hist_len == 0U))
  {
    return 0U;
  }

  min_peak_count = (sample_cnt * APP_MODDET_HIST_PEAK_MIN_PERCENT) / 100U;
  if (min_peak_count == 0U)
  {
    min_peak_count = 1U;
  }

  for (idx = 0U; idx < hist_len; idx++)
  {
    uint32_t left = (idx == 0U) ? 0U : hist[idx - 1U];
    uint32_t right = (idx + 1U >= hist_len) ? 0U : hist[idx + 1U];

    if ((hist[idx] >= min_peak_count) &&
        (hist[idx] >= left) &&
        (hist[idx] >= right))
    {
      peak_count++;
    }
  }

  return peak_count;
}

static void app_moddet_set_default_config(app_mod_detect_config_t *config)
{
  config->sample_rate_hz = APP_MODDET_DEFAULT_SAMPLE_RATE_HZ;
  config->min_block_samples = 128U;
  config->reserved = 0U;
}

/* 根据 FFT 长度选择 CMSIS-DSP 的 CFFT 描述符。 */
static const arm_cfft_instance_f32 *app_moddet_select_cfft(uint32_t fft_len)
{
#if (APP_MODDET_USE_CMSIS_FFT != 0U)
  switch (fft_len)
  {
    case 16U:
      return &arm_cfft_sR_f32_len16;
    case 32U:
      return &arm_cfft_sR_f32_len32;
    case 64U:
      return &arm_cfft_sR_f32_len64;
    case 128U:
      return &arm_cfft_sR_f32_len128;
    case 256U:
      return &arm_cfft_sR_f32_len256;
    case 512U:
      return &arm_cfft_sR_f32_len512;
    case 1024U:
      return &arm_cfft_sR_f32_len1024;
    case 2048U:
      return &arm_cfft_sR_f32_len2048;
    case 4096U:
      return &arm_cfft_sR_f32_len4096;
    default:
      return 0;
  }
#else
  (void)fft_len;
  return 0;
#endif
}


static uint32_t app_moddet_scale_u32(uint32_t value, uint32_t numerator, uint32_t denominator)
{
  if (denominator == 0U)
  {
    return 0U;
  }

  return app_moddet_div_round_u64((uint64_t)value * (uint64_t)numerator,
                                  (uint64_t)denominator);
}

static uint32_t app_moddet_cap_pm(uint32_t value)
{
  return (value > 1000U) ? 1000U : value;
}
static uint32_t app_moddet_rate_from_edges(uint32_t edge_count, uint32_t sample_cnt, uint32_t sample_rate_hz)
{
  if ((edge_count == 0U) || (sample_cnt == 0U) || (sample_rate_hz == 0U))
  {
    return 0U;
  }

  return app_moddet_div_round_u64((uint64_t)edge_count * (uint64_t)sample_rate_hz,
                                  (uint64_t)sample_cnt * 2ULL);
}

static uint32_t app_moddet_am_depth_pm(uint64_t env_min, uint64_t env_max)
{
  uint32_t min_amp = app_moddet_isqrt_u64(env_min);
  uint32_t max_amp = app_moddet_isqrt_u64(env_max);
  uint32_t sum_amp = min_amp + max_amp;

  if ((max_amp <= min_amp) || (sum_amp == 0U))
  {
    return 0U;
  }

  return (uint32_t)(((uint64_t)(max_amp - min_amp) * 1000ULL) / (uint64_t)sum_amp);
}

/* 用衰减投票平滑即时模式，避免 UI 在相邻 block 间频繁跳变。 */
static void app_moddet_update_stable_mode(app_mod_detect_status_t *st)
{
  uint32_t idx;
  uint8_t best_vote = 0U;
  app_mod_detect_mode_t best_mode = st->mode;
  uint32_t total_vote = 0U;

  for (idx = 0U; idx <= (uint32_t)APP_MODDET_MODE_PSK; idx++)
  {
    if (g_moddet.mode_vote[idx] > 0U)
    {
      g_moddet.mode_vote[idx]--;
    }
  }

  if ((uint32_t)st->mode <= (uint32_t)APP_MODDET_MODE_PSK)
  {
    uint32_t vote = (uint32_t)g_moddet.mode_vote[st->mode] + 6U;
    g_moddet.mode_vote[st->mode] = (vote > 60U) ? 60U : (uint8_t)vote;
  }

  for (idx = 0U; idx <= (uint32_t)APP_MODDET_MODE_PSK; idx++)
  {
    total_vote += g_moddet.mode_vote[idx];
    if (g_moddet.mode_vote[idx] > best_vote)
    {
      best_vote = g_moddet.mode_vote[idx];
      best_mode = (app_mod_detect_mode_t)idx;
    }
  }

  st->stable_mode = best_mode;
  st->stable_confidence_percent = (uint16_t)app_moddet_percent_u32(best_vote, total_vote);
}

static uint8_t app_moddet_is_single_sign_dominant(const app_mod_detect_status_t *st)
{
  uint16_t major = (st->positive_freq_percent > st->negative_freq_percent) ?
                   st->positive_freq_percent : st->negative_freq_percent;
  uint16_t minor = (st->positive_freq_percent > st->negative_freq_percent) ?
                   st->negative_freq_percent : st->positive_freq_percent;

  return ((major >= APP_MODDET_SIGN_DOMINANT_PERCENT) &&
          (minor <= APP_MODDET_SIGN_MINOR_PERCENT)) ? 1U : 0U;
}

static app_mod_detect_mode_t app_moddet_classify(const app_mod_detect_status_t *st)
{
  uint8_t single_sign;
  /* 交流功率过低时，后续调制特征不可信。 */
  if (st->carrier_present == 0U)
  {
    return APP_MODDET_MODE_NO_CARRIER;
  }
  /* 包络变化主导时优先在 ASK 和 AM 之间区分。 */
  if ((st->env_variation_pm >= APP_MODDET_AM_ENV_PM_THRESHOLD) &&
      (((st->env_edge_pm >= APP_MODDET_ASK_MIN_EDGE_PM) &&
        (st->freq_activity >= APP_MODDET_ASK_MIN_FREQ_PM)) ||
       ((st->env_variation_pm >= APP_MODDET_ASK_STRONG_ENV_PM) &&
        (st->env_edge_pm >= APP_MODDET_ASK_STRONG_EDGE_PM))))
  {
    return APP_MODDET_MODE_ASK;
  }

  if (st->env_variation_pm >= APP_MODDET_AM_ENV_PM_THRESHOLD)
  {
    return APP_MODDET_MODE_AM;
  }

  single_sign = app_moddet_is_single_sign_dominant(st);

  if ((single_sign != 0U) &&
      (st->freq_activity <= APP_MODDET_PSK_MAX_FREQ_PM) &&
      ((st->freq_activity >= APP_MODDET_PSK_MIN_FREQ_PM) ||
       (st->env_edge_pm >= APP_MODDET_PSK_MIN_EDGE_PM)) &&
      (st->psk_jump_pm >= APP_MODDET_PSK_JUMP_PM_THRESHOLD) &&
      (st->dphi_hist_peak_count <= APP_MODDET_PSK_MAX_HIST_PEAKS) &&
      (st->env_variation_pm < APP_MODDET_PSK_MAX_ENV_PM))
  {
    return APP_MODDET_MODE_PSK;
  }

  if ((single_sign != 0U) &&
      (st->freq_activity >= APP_MODDET_FSK_MIN_FREQ_PM) &&
      (st->psk_jump_pm < APP_MODDET_PSK_JUMP_PM_THRESHOLD) &&
      (st->env_variation_pm < APP_MODDET_PSK_MAX_ENV_PM))
  {
    return APP_MODDET_MODE_FSK;
  }

  if (st->freq_activity >= APP_MODDET_FREQ_ACTIVITY_THRESHOLD)
  {
    return APP_MODDET_MODE_FM;
  }

  return APP_MODDET_MODE_CW;
}

void app_mod_detect_init(const app_mod_detect_config_t *config)
{
  memset(&g_moddet, 0, sizeof(g_moddet));

  if (config != 0)
  {
    g_moddet.config = *config;
  }
  else
  {
    app_moddet_set_default_config(&g_moddet.config);
  }

  g_moddet.fft_len = APP_MODDET_FFT_LEN;
  g_moddet.cfft = app_moddet_select_cfft(g_moddet.fft_len);

  if (g_moddet.cfft == 0)
  {
    g_moddet.fft_len = 0U;
  }
}

/* 预处理和频谱特征提取用的内部数据结构。 */
typedef struct
{
  int32_t mean_i;
  int32_t mean_q;
  uint32_t ac_power;
  int32_t residual_freq_hz;
} app_moddet_preprocess_result_t;

typedef struct
{
  uint32_t main_peak_hz;
  uint32_t main_peak_bin;
  uint32_t harmonic_percent;
  float main_peak_mag;
  float total_mag;
} app_moddet_spectrum_result_t;

/* 将实数序列装入复数 FFT 工作缓冲，超长输入只取前 fft_len 点。 */
static uint32_t app_moddet_prepare_fft_input(const float *src, uint32_t src_len);
/* 运行 CFFT 并生成前半谱幅度，直流 bin 保留但后续峰值搜索会跳过。 */
static void app_moddet_compute_fft_magnitude(void);
/* 提取主峰频率和有限个谐波 bin 的能量比例。 */
static void app_moddet_extract_spectrum_features(const float *src,
                                                 uint32_t src_len,
                                                 app_moddet_spectrum_result_t *out);


/* 三点中值滤波，用于抑制包络序列中的单点毛刺。 */
static float app_moddet_median3f(float a, float b, float c)
{
  if (a > b)
  {
    float t = a;
    a = b;
    b = t;
  }

  if (b > c)
  {
    float t = b;
    b = c;
    c = t;
  }

  if (a > b)
  {
    float t = a;
    a = b;
    b = t;
  }

  return b;
}

/* 将相邻相位差限制到 [-pi, pi]，避免 atan2 跨界造成跳变。 */
static float app_moddet_wrap_to_pi(float x)
{
  const float two_pi = 6.28318530718f;
  const float pi = 3.14159265359f;

  while (x > pi)
  {
    x -= two_pi;
  }

  while (x < -pi)
  {
    x += two_pi;
  }

  return x;
}

/* 对包络做三点中值滤波，首尾点保持原值。 */
static void app_moddet_smooth_envelope(uint32_t sample_cnt)
{
  uint32_t idx;

  if (sample_cnt == 0U)
  {
    return;
  }

  if (sample_cnt == 1U)
  {
    g_moddet_work.env_smooth_buf[0] = g_moddet_work.env_buf[0];
    return;
  }

  g_moddet_work.env_smooth_buf[0] = g_moddet_work.env_buf[0];
  g_moddet_work.env_smooth_buf[sample_cnt - 1U] = g_moddet_work.env_buf[sample_cnt - 1U];

  for (idx = 1U; idx + 1U < sample_cnt; idx++)
  {
    g_moddet_work.env_smooth_buf[idx] =
        app_moddet_median3f(g_moddet_work.env_buf[idx - 1U],
                            g_moddet_work.env_buf[idx],
                            g_moddet_work.env_buf[idx + 1U]);
  }
}

/*
 * 从原始 I/Q 构造包络、相位、相位差及其去直流序列。
 * 输出 residual_freq_hz 只表示该 block 内平均相位步进对应的残余频偏。
 */
static void app_moddet_build_sequences(const uint16_t *i_buf,
                                       const uint16_t *q_buf,
                                       uint32_t sample_cnt,
                                       app_moddet_preprocess_result_t *out)
{
  uint64_t sum_i = 0ULL;
  uint64_t sum_q = 0ULL;
  uint64_t power_sum = 0ULL;
  float env_mean = 0.0f;
  float dphi_mean = 0.0f;
  float phase_acc = 0.0f;
  float prev_raw_phase = 0.0f;
  uint32_t idx;
  int32_t mean_i;
  int32_t mean_q;

  memset(out, 0, sizeof(*out));

  for (idx = 0U; idx < sample_cnt; idx++)
  {
    sum_i += i_buf[idx];
    sum_q += q_buf[idx];
  }

  mean_i = (int32_t)(sum_i / sample_cnt);
  mean_q = (int32_t)(sum_q / sample_cnt);

  out->mean_i = mean_i;
  out->mean_q = mean_q;

  for (idx = 0U; idx < sample_cnt; idx++)
  {
    float ci = (float)((int32_t)i_buf[idx] - mean_i);
    float cq = (float)((int32_t)q_buf[idx] - mean_q);
    float mag2 = (ci * ci) + (cq * cq);
    float mag = sqrtf(mag2);
    float raw_phase = 0.0f;

    power_sum += (uint64_t)mag2;
    g_moddet_work.env_buf[idx] = mag;

    if ((ci != 0.0f) || (cq != 0.0f))
    {
      raw_phase = atan2f(cq, ci);
    }

    if (idx == 0U)
    {
      phase_acc = raw_phase;
      g_moddet_work.phase_buf[idx] = phase_acc;
      g_moddet_work.dphi_buf[idx] = 0.0f;
    }
    else
    {
      float dphi = app_moddet_wrap_to_pi(raw_phase - prev_raw_phase);
      phase_acc += dphi;
      g_moddet_work.phase_buf[idx] = phase_acc;
      g_moddet_work.dphi_buf[idx] = dphi;
      dphi_mean += dphi;
    }

    prev_raw_phase = raw_phase;
  }

  out->ac_power = (uint32_t)(power_sum / sample_cnt);

  app_moddet_smooth_envelope(sample_cnt);

  for (idx = 0U; idx < sample_cnt; idx++)
  {
    env_mean += g_moddet_work.env_smooth_buf[idx];
  }
  env_mean /= (float)sample_cnt;

  for (idx = 0U; idx < sample_cnt; idx++)
  {
    g_moddet_work.env_ac_buf[idx] = g_moddet_work.env_smooth_buf[idx] - env_mean;
  }

  if (sample_cnt > 1U)
  {
    const float two_pi = 6.28318530718f;
    dphi_mean /= (float)(sample_cnt - 1U);

    for (idx = 0U; idx < sample_cnt; idx++)
    {
      g_moddet_work.phase_base_buf[idx] =
          g_moddet_work.phase_buf[idx] -
          (g_moddet_work.phase_buf[0] + (dphi_mean * (float)idx));
    }

    g_moddet_work.dphi_ac_buf[0] = 0.0f;
    for (idx = 1U; idx < sample_cnt; idx++)
    {
      g_moddet_work.dphi_ac_buf[idx] = g_moddet_work.dphi_buf[idx] - dphi_mean;
    }

    out->residual_freq_hz =
        (int32_t)(dphi_mean * (float)g_moddet.config.sample_rate_hz / two_pi);
  }
  else
  {
    g_moddet_work.phase_base_buf[0] = 0.0f;
    g_moddet_work.dphi_ac_buf[0] = 0.0f;
    out->residual_freq_hz = 0;
  }
}

static uint32_t app_moddet_prepare_fft_input(const float *src, uint32_t src_len)
{
  uint32_t fft_len = g_moddet.fft_len;
  uint32_t copy_len;
  uint32_t idx;

  if ((src == 0) || (fft_len == 0U))
  {
    return 0U;
  }

  copy_len = (src_len < fft_len) ? src_len : fft_len;

  memset(g_moddet_work.fft_work_buf, 0, sizeof(g_moddet_work.fft_work_buf));

  for (idx = 0U; idx < copy_len; idx++)
  {
    g_moddet_work.fft_work_buf[2U * idx] = src[idx];
    g_moddet_work.fft_work_buf[(2U * idx) + 1U] = 0.0f;
  }

  return copy_len;
}


/* 主处理入口：从 I/Q block 提取包络、相位、频率活动等特征并完成模式判定。 */
void app_mod_detect_process_iq_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt)
{
  uint64_t sum_i = 0ULL;
  uint64_t sum_q = 0ULL;
  uint64_t power_sum = 0ULL;
  uint64_t env_dev_sum = 0ULL;
  uint64_t env_prev_mag2 = 0ULL;
  int64_t freq_metric_sum = 0LL;
  uint64_t freq_abs_sum = 0ULL;
  uint32_t positive_freq_cnt = 0U;
  uint32_t negative_freq_cnt = 0U;
  uint32_t psk_jump_cnt = 0U;
  uint32_t env_edge_cnt = 0U;
  uint32_t env_transition_cnt = 0U;
  uint32_t freq_sign_transition_cnt = 0U;
  uint32_t env_level_mask = 0U;
  uint32_t dphi_hist[APP_MODDET_DPHI_HIST_BIN_COUNT];
  uint32_t pair_cnt;
  uint32_t idx;
  int32_t mean_i;
  int32_t mean_q;
  int32_t prev_i = 0;
  int32_t prev_q = 0;
  uint64_t mean_power;
  uint64_t env_min = 0ULL;
  uint64_t env_max = 0ULL;
  uint64_t env_span;
  uint64_t env_mid;
  uint64_t env_hyst;
  uint32_t env_edge_threshold;
  uint8_t env_state = 0U;
  uint8_t env_state_valid = 0U;
  int8_t prev_freq_sign = 0;
  app_mod_detect_status_t st;
  app_moddet_preprocess_result_t prep;
  app_moddet_spectrum_result_t env_spec;
  app_moddet_spectrum_result_t phase_spec;

  if ((i_buf == 0) || (q_buf == 0) || (sample_cnt < g_moddet.config.min_block_samples))
  {
    return;
  }

  app_moddet_build_sequences(i_buf, q_buf, sample_cnt, &prep);
  app_moddet_extract_spectrum_features(g_moddet_work.env_ac_buf, sample_cnt, &env_spec);
  app_moddet_extract_spectrum_features(g_moddet_work.dphi_ac_buf, sample_cnt, &phase_spec);


  for (idx = 0U; idx < sample_cnt; idx++)
  {
    sum_i += i_buf[idx];
    sum_q += q_buf[idx];
  }

  mean_i = (int32_t)(sum_i / sample_cnt);
  mean_q = (int32_t)(sum_q / sample_cnt);

  memset(&dphi_hist[0], 0, sizeof(dphi_hist));

  for (idx = 0U; idx < sample_cnt; idx++)
  {
    int32_t ci = (int32_t)i_buf[idx] - mean_i;
    int32_t cq = (int32_t)q_buf[idx] - mean_q;
    uint64_t mag2 = (uint64_t)(((int64_t)ci * (int64_t)ci) + ((int64_t)cq * (int64_t)cq));

    power_sum += mag2;  /* 交流功率累加：sum(|I + jQ|^2)。 */
    if (idx == 0U)
    {
      env_min = mag2;
      env_max = mag2;
    }
    else
    {
      if (mag2 < env_min)
      {
        env_min = mag2;
      }
      if (mag2 > env_max)
      {
        env_max = mag2;
      }
    }
  }

  mean_power = power_sum / sample_cnt;
  env_span = env_max - env_min;
  env_mid = env_min + (env_span / 2ULL);
  env_hyst = env_span / 16ULL;  /* 包络电平滞回，减少噪声导致的高低状态抖动。 */
  env_edge_threshold = app_moddet_max_u32((uint32_t)(env_span / 8ULL),
                                          app_moddet_max_u32((uint32_t)(mean_power / 64ULL), 1U));

  for (idx = 0U; idx < sample_cnt; idx++)
  {
    int32_t ci = (int32_t)i_buf[idx] - mean_i;
    int32_t cq = (int32_t)q_buf[idx] - mean_q;
    uint64_t mag2 = (uint64_t)(((int64_t)ci * (int64_t)ci) + ((int64_t)cq * (int64_t)cq));
    uint32_t env_bin;

    env_dev_sum += app_moddet_abs_diff_u64(mag2, mean_power);

    if (env_span == 0ULL)
    {
      env_bin = 0U;
    }
    else
    {
      env_bin = (uint32_t)(((mag2 - env_min) * APP_MODDET_ENV_LEVEL_BINS) / (env_span + 1ULL));
      if (env_bin >= APP_MODDET_ENV_LEVEL_BINS)
      {
        env_bin = APP_MODDET_ENV_LEVEL_BINS - 1U;
      }
    }
    env_level_mask |= (1UL << env_bin);

    if (idx > 0U)
    {
      /* cross 表示相邻 IQ 向量的相位变化方向和强度；dot < 0 用于统计接近 180° 的相位翻转。 */
      int64_t cross = ((int64_t)ci * (int64_t)prev_q) - ((int64_t)cq * (int64_t)prev_i);
      int64_t dot = ((int64_t)ci * (int64_t)prev_i) + ((int64_t)cq * (int64_t)prev_q);
      uint32_t abs_cross = app_moddet_abs_i64_to_u32(cross);
      uint32_t cross_pm = app_moddet_permille_u64((uint64_t)abs_cross, mean_power);
      uint32_t hist_bin = 2U;
      int8_t freq_sign = 0;

      if (app_moddet_abs_diff_u64(mag2, env_prev_mag2) >= env_edge_threshold)
      {
        env_edge_cnt++;
      }

      freq_metric_sum += cross;
      freq_abs_sum += abs_cross;

      if (cross > 0)
      {
        positive_freq_cnt++;
        freq_sign = 1;
      }
      else if (cross < 0)
      {
        negative_freq_cnt++;
        freq_sign = -1;
      }

      if (freq_sign != 0)
      {
        if ((prev_freq_sign != 0) && (freq_sign != prev_freq_sign))
        {
          freq_sign_transition_cnt++;
        }
        prev_freq_sign = freq_sign;
      }

      if (dot < 0)
      {
        psk_jump_cnt++;
      }

      if (cross_pm <= APP_MODDET_DPHI_ZERO_PM_THRESHOLD)
      {
        hist_bin = 2U;
      }
      else if (cross < 0)
      {
        hist_bin = (cross_pm >= APP_MODDET_DPHI_STRONG_PM_THRESHOLD) ? 0U : 1U;
      }
      else
      {
        hist_bin = (cross_pm >= APP_MODDET_DPHI_STRONG_PM_THRESHOLD) ? 4U : 3U;
      }
      dphi_hist[hist_bin]++;
    }

    if (env_span != 0ULL)
    {
      uint8_t next_env_state = env_state;

      if (mag2 > (env_mid + env_hyst))
      {
        next_env_state = 1U;
      }
      else if (mag2 < ((env_mid > env_hyst) ? (env_mid - env_hyst) : 0ULL))
      {
        next_env_state = 0U;
      }

      if (env_state_valid == 0U)
      {
        env_state = next_env_state;
        env_state_valid = 1U;
      }
      else if (next_env_state != env_state)
      {
        env_transition_cnt++;
        env_state = next_env_state;
      }
    }

    prev_i = ci;
    prev_q = cq;
    env_prev_mag2 = mag2;
  }

  pair_cnt = sample_cnt - 1U;

  memset(&st, 0, sizeof(st));
  st.block_count = g_moddet.status.block_count + 1U;
  st.ac_power = (uint32_t)mean_power;
  st.residual_freq_hz = prep.residual_freq_hz;
  app_moddet_extract_spectrum_features(g_moddet_work.env_ac_buf, sample_cnt, &env_spec);
  app_moddet_extract_spectrum_features(g_moddet_work.dphi_ac_buf, sample_cnt, &phase_spec);


  st.carrier_present = (st.ac_power >= APP_MODDET_MIN_AC_POWER) ? 1U : 0U;
  st.env_variation_pm = app_moddet_permille_u64(env_dev_sum / sample_cnt, mean_power);
  st.env_edge_pm = app_moddet_permille_u64(env_edge_cnt, pair_cnt);
  st.freq_activity = app_moddet_permille_u64(freq_abs_sum / pair_cnt, mean_power);
  st.freq_bias = (int32_t)(freq_metric_sum / (int64_t)pair_cnt);
  st.fsk_balance_pm = app_moddet_permille_u64((uint64_t)(positive_freq_cnt + negative_freq_cnt), pair_cnt);
  st.psk_jump_pm = app_moddet_permille_u64(psk_jump_cnt, pair_cnt);
  st.modulation_rate_hz = app_moddet_rate_from_edges(env_transition_cnt, sample_cnt, g_moddet.config.sample_rate_hz);
  st.am_depth_pm = app_moddet_am_depth_pm(env_min, env_max);
  st.fm_deviation_hz = app_moddet_div_round_u64((uint64_t)st.freq_activity *
                                                (uint64_t)g_moddet.config.sample_rate_hz,
                                                6283ULL);
  st.fsk_shift_hz = st.fm_deviation_hz;
  st.env_level_count = (uint16_t)app_moddet_count_bits_u32(env_level_mask);
  st.dphi_hist_peak_count = app_moddet_count_hist_peaks(&dphi_hist[0],
                                                        APP_MODDET_DPHI_HIST_BIN_COUNT,
                                                        pair_cnt);
  st.positive_freq_percent = (uint16_t)app_moddet_percent_u32(positive_freq_cnt, pair_cnt);
  st.negative_freq_percent = (uint16_t)app_moddet_percent_u32(negative_freq_cnt, pair_cnt);
  st.mode = app_moddet_classify(&st);
  {
    uint32_t raw_env_rate_hz = st.modulation_rate_hz;
    uint32_t raw_freq_rate_hz = app_moddet_rate_from_edges(freq_sign_transition_cnt,
                                                           sample_cnt,
                                                           g_moddet.config.sample_rate_hz);
    uint32_t raw_psk_rate_hz = app_moddet_div_round_u64((uint64_t)psk_jump_cnt *
                                                        (uint64_t)g_moddet.config.sample_rate_hz,
                                                        (uint64_t)pair_cnt);
    uint32_t raw_depth_pm = st.am_depth_pm;
    uint32_t raw_dev_hz = st.fm_deviation_hz;

    st.modulation_rate_hz = 0U;
    st.am_depth_pm = 0U;
    st.fm_deviation_hz = 0U;
    st.fsk_shift_hz = 0U;

    if (st.mode == APP_MODDET_MODE_AM)
    {
      st.modulation_rate_hz = app_moddet_scale_u32(raw_env_rate_hz, 2U, 5U);
      st.am_depth_pm = app_moddet_cap_pm(app_moddet_scale_u32(raw_depth_pm, 7U, 10U));
    }
    else if (st.mode == APP_MODDET_MODE_ASK)
    {
      st.modulation_rate_hz = app_moddet_scale_u32(raw_env_rate_hz, 1U, 3U);
      st.am_depth_pm = app_moddet_cap_pm(raw_depth_pm);
    }
    else if (st.mode == APP_MODDET_MODE_FM)
    {
      st.modulation_rate_hz = app_moddet_scale_u32(raw_freq_rate_hz, 1U, 5U);
      st.fm_deviation_hz = app_moddet_scale_u32(raw_dev_hz, 4U, 5U);
    }
    else if (st.mode == APP_MODDET_MODE_FSK)
    {
      st.modulation_rate_hz = app_moddet_scale_u32(raw_freq_rate_hz, 1U, 17U);
      st.fsk_shift_hz = app_moddet_scale_u32(raw_dev_hz, 4U, 5U);
      st.fm_deviation_hz = st.fsk_shift_hz;
    }
    else if (st.mode == APP_MODDET_MODE_PSK)
    {
      st.modulation_rate_hz = app_moddet_scale_u32(raw_psk_rate_hz, 2U, 11U);
    }
  }
  app_moddet_update_stable_mode(&st);

  g_moddet.status = st;

#if (APP_MODDET_UART_TRACE_ENABLE != 0U)
  if ((g_uart_mode == UART_MODE_LOG) &&
      ((APP_MODDET_TRACE_DECIMATION_BLOCKS == 0U) ||
       ((st.block_count % APP_MODDET_TRACE_DECIMATION_BLOCKS) == 0U)))
  {
    char line[125];
    int n = snprintf(line,
                     sizeof(line),
                     "mod,mode=%s,stable=%s,conf=%u,rate=%lu,depth=%lu,dev=%lu,shift=%lu\r\n",
                     app_mod_detect_mode_name(st.mode),
                     app_mod_detect_mode_name(st.stable_mode),
                     (unsigned)st.stable_confidence_percent,
                     (unsigned long)st.modulation_rate_hz,
                     (unsigned long)st.am_depth_pm,
                     (unsigned long)st.fm_deviation_hz,
                     (unsigned long)st.fsk_shift_hz);
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
      print_queue_send_log(line);
    }

    n = snprintf(line,
                 sizeof(line),
                 "ui,mode=%s,inst=%s,conf=%u,rate=%lu,depth=%lu,dev=%lu,shift=%lu\r\n",
                 app_mod_detect_mode_name(st.stable_mode),
                 app_mod_detect_mode_name(st.mode),
                 (unsigned)st.stable_confidence_percent,
                 (unsigned long)st.modulation_rate_hz,
                 (unsigned long)st.am_depth_pm,
                 (unsigned long)st.fm_deviation_hz,
                 (unsigned long)st.fsk_shift_hz);
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
      print_queue_send_log(line);
    }
  }
#endif
}

void app_mod_detect_get_status(app_mod_detect_status_t *status_out)
{
  if (status_out == 0)
  {
    return;
  }

  *status_out = g_moddet.status;
}

const char *app_mod_detect_mode_name(app_mod_detect_mode_t mode)
{
  switch (mode)
  {
    case APP_MODDET_MODE_NO_CARRIER:
      return "NO_CARRIER";
    case APP_MODDET_MODE_CW:
      return "CW";
    case APP_MODDET_MODE_AM:
      return "AM";
    case APP_MODDET_MODE_ASK:
      return "ASK";
    case APP_MODDET_MODE_FM:
      return "FM";
    case APP_MODDET_MODE_FSK:
      return "FSK";
    case APP_MODDET_MODE_PSK:
      return "PSK";
    case APP_MODDET_MODE_UNKNOWN:
    default:
      return "UNKNOWN";
  }
}

static void app_moddet_compute_fft_magnitude(void)
{
#if (APP_MODDET_USE_CMSIS_FFT != 0U)
  uint32_t idx;
  uint32_t fft_len = g_moddet.fft_len;

  if ((g_moddet.cfft == 0) || (fft_len == 0U))
  {
    return;
  }

  arm_cfft_f32(g_moddet.cfft, g_moddet_work.fft_work_buf, 0U, 1U);

  for (idx = 0U; idx < (fft_len / 2U); idx++)
  {
    float re = g_moddet_work.fft_work_buf[2U * idx];
    float im = g_moddet_work.fft_work_buf[(2U * idx) + 1U];
    g_moddet_work.fft_mag_buf[idx] = sqrtf((re * re) + (im * im));
  }
#else
  /* FFT 支持未启用时清空频谱缓存，调制识别退回时域统计特征。 */
  memset(g_moddet_work.fft_mag_buf, 0, sizeof(g_moddet_work.fft_mag_buf));
#endif
}

static void app_moddet_extract_spectrum_features(const float *src,
                                                 uint32_t src_len,
                                                 app_moddet_spectrum_result_t *out)
{
  uint32_t fft_len = g_moddet.fft_len;
  uint32_t nyquist_bins;
  uint32_t idx;
  uint32_t peak_bin = 0U;
  float peak_mag = 0.0f;
  float total_mag = 0.0f;
  float harmonic_mag = 0.0f;
  uint32_t valid_len;

  memset(out, 0, sizeof(*out));

  if ((src == 0) || (out == 0) || (g_moddet.cfft == 0) || (fft_len == 0U))
  {
    return;
  }

  valid_len = app_moddet_prepare_fft_input(src, src_len);
  if (valid_len < 8U)
  {
    return;
  }

  app_moddet_compute_fft_magnitude();

  nyquist_bins = fft_len / 2U;

  for (idx = 1U; idx < nyquist_bins; idx++)
  {
    float mag = g_moddet_work.fft_mag_buf[idx];
    total_mag += mag;

    if (mag > peak_mag)
    {
      peak_mag = mag;
      peak_bin = idx;
    }
  }

  out->main_peak_bin = peak_bin;
  out->main_peak_mag = peak_mag;
  out->total_mag = total_mag;

  if ((peak_bin != 0U) && (peak_mag > 0.0f))
  {
    uint32_t harm_idx;
    for (harm_idx = 2U; harm_idx <= APP_MODDET_HARMONIC_SEARCH_BINS; harm_idx++)
    {
      uint32_t bin = peak_bin * harm_idx;
      if (bin >= nyquist_bins)
      {
        break;
      }
      harmonic_mag += g_moddet_work.fft_mag_buf[bin];
    }

    out->main_peak_hz =
        (uint32_t)(((uint64_t)peak_bin * (uint64_t)g_moddet.config.sample_rate_hz) /
                   (uint64_t)fft_len);

    out->harmonic_percent =
        (uint16_t)((peak_mag > 0.0f) ? (uint32_t)((harmonic_mag * 100.0f) / peak_mag) : 0U);
  }
}
