#include "app_mod_detect.h"

#include <string.h>

typedef struct
{
  app_mod_detect_config_t config;
  app_mod_detect_status_t status;
} app_mod_detect_ctx_t;

static app_mod_detect_ctx_t g_moddet;

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

static uint32_t app_moddet_abs_diff_u64(uint64_t a, uint64_t b)
{
  uint64_t diff = (a > b) ? (a - b) : (b - a);

  if (diff > 0xFFFFFFFFULL)
  {
    return 0xFFFFFFFFUL;
  }

  return (uint32_t)diff;
}

static uint32_t app_moddet_percent_u32(uint32_t numerator, uint32_t denominator)
{
  if (denominator == 0U)
  {
    return 0U;
  }

  return (uint32_t)(((uint64_t)numerator * 100ULL) / denominator);
}

static uint32_t app_moddet_permille_u64(uint64_t numerator, uint64_t denominator)
{
  if (denominator == 0ULL)
  {
    return 0U;
  }

  return (uint32_t)((numerator * 1000ULL) / denominator);
}

static void app_moddet_set_default_config(app_mod_detect_config_t *config)
{
  config->sample_rate_hz = 0U;
  config->min_block_samples = 128U;
  config->reserved = 0U;
}

static app_mod_detect_mode_t app_moddet_classify(const app_mod_detect_status_t *st)
{
  if (st->carrier_present == 0U)
  {
    return APP_MODDET_MODE_NO_CARRIER;
  }

  if ((st->psk_jump_pm >= APP_MODDET_PSK_JUMP_PM_THRESHOLD) &&
      (st->env_variation_pm < APP_MODDET_AM_ENV_PM_THRESHOLD))
  {
    return APP_MODDET_MODE_PSK;
  }

  if ((st->positive_freq_percent >= APP_MODDET_FSK_SIGN_MIN_PERCENT) &&
      (st->negative_freq_percent >= APP_MODDET_FSK_SIGN_MIN_PERCENT) &&
      (st->freq_activity >= APP_MODDET_FREQ_ACTIVITY_THRESHOLD))
  {
    return APP_MODDET_MODE_FSK;
  }

  if ((st->env_variation_pm >= APP_MODDET_AM_ENV_PM_THRESHOLD) &&
      (st->freq_activity < (APP_MODDET_FREQ_ACTIVITY_THRESHOLD * 2U)))
  {
    return APP_MODDET_MODE_AM;
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
}

void app_mod_detect_process_iq_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt)
{
  uint64_t sum_i = 0ULL;
  uint64_t sum_q = 0ULL;
  uint64_t power_sum = 0ULL;
  uint64_t env_dev_sum = 0ULL;
  int64_t freq_metric_sum = 0LL;
  uint64_t freq_abs_sum = 0ULL;
  uint32_t positive_freq_cnt = 0U;
  uint32_t negative_freq_cnt = 0U;
  uint32_t psk_jump_cnt = 0U;
  uint32_t pair_cnt;
  uint32_t idx;
  int32_t mean_i;
  int32_t mean_q;
  int32_t prev_i = 0;
  int32_t prev_q = 0;
  uint64_t mean_power;
  app_mod_detect_status_t st;

  if ((i_buf == 0) || (q_buf == 0) || (sample_cnt < g_moddet.config.min_block_samples))
  {
    return;
  }

  for (idx = 0U; idx < sample_cnt; idx++)
  {
    sum_i += i_buf[idx];
    sum_q += q_buf[idx];
  }

  mean_i = (int32_t)(sum_i / sample_cnt);
  mean_q = (int32_t)(sum_q / sample_cnt);

  for (idx = 0U; idx < sample_cnt; idx++)
  {
    int32_t ci = (int32_t)i_buf[idx] - mean_i;
    int32_t cq = (int32_t)q_buf[idx] - mean_q;

    power_sum += (uint64_t)(((int64_t)ci * (int64_t)ci) + ((int64_t)cq * (int64_t)cq));
  }

  mean_power = power_sum / sample_cnt;

  for (idx = 0U; idx < sample_cnt; idx++)
  {
    int32_t ci = (int32_t)i_buf[idx] - mean_i;
    int32_t cq = (int32_t)q_buf[idx] - mean_q;
    uint64_t mag2 = (uint64_t)(((int64_t)ci * (int64_t)ci) + ((int64_t)cq * (int64_t)cq));

    env_dev_sum += app_moddet_abs_diff_u64(mag2, mean_power);

    if (idx > 0U)
    {
      int64_t cross = ((int64_t)ci * (int64_t)prev_q) - ((int64_t)cq * (int64_t)prev_i);
      int64_t dot = ((int64_t)ci * (int64_t)prev_i) + ((int64_t)cq * (int64_t)prev_q);
      uint32_t abs_cross = app_moddet_abs_i64_to_u32(cross);

      freq_metric_sum += cross;
      freq_abs_sum += abs_cross;

      if (cross > 0)
      {
        positive_freq_cnt++;
      }
      else if (cross < 0)
      {
        negative_freq_cnt++;
      }

      if (dot < 0)
      {
        psk_jump_cnt++;
      }
    }

    prev_i = ci;
    prev_q = cq;
  }

  pair_cnt = sample_cnt - 1U;

  memset(&st, 0, sizeof(st));
  st.block_count = g_moddet.status.block_count + 1U;
  st.ac_power = (uint32_t)mean_power;
  st.carrier_present = (st.ac_power >= APP_MODDET_MIN_AC_POWER) ? 1U : 0U;
  st.env_variation_pm = app_moddet_permille_u64(env_dev_sum / sample_cnt, mean_power);
  st.freq_activity = app_moddet_permille_u64(freq_abs_sum / pair_cnt, mean_power);
  st.freq_bias = (int32_t)(freq_metric_sum / (int64_t)pair_cnt);
  st.positive_freq_percent = (uint16_t)app_moddet_percent_u32(positive_freq_cnt, pair_cnt);
  st.negative_freq_percent = (uint16_t)app_moddet_percent_u32(negative_freq_cnt, pair_cnt);
  st.fsk_balance_pm = app_moddet_permille_u64((uint64_t)(positive_freq_cnt + negative_freq_cnt), pair_cnt);
  st.psk_jump_pm = app_moddet_permille_u64(psk_jump_cnt, pair_cnt);
  st.mode = app_moddet_classify(&st);

  g_moddet.status = st;
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
