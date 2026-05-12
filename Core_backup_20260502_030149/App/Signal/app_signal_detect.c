#include "app_signal_detect.h"

#include <string.h>

#include "cmsis_os2.h"
#include "app_dds_ctrl.h"

#define APP_SIGDET_MAX_STEPS            128U
#define APP_SIGDET_AUTO_RESCAN_MS       3000U
#define APP_SIGDET_CARRIER_RATIO_NUM    3U
#define APP_SIGDET_CARRIER_RATIO_DEN    2U

typedef struct
{
  app_signal_detect_status_t status;
  uint32_t step_metric_sum;
  uint16_t step_dwell_count;
  uint16_t best_step_index;
  uint32_t metric_table[APP_SIGDET_MAX_STEPS];
  uint8_t inited;
} app_signal_detect_ctx_t;

static app_signal_detect_ctx_t g_sigdet;

static uint16_t app_signal_detect_calc_step_count(void);
static uint32_t app_signal_detect_step_freq_hz(uint16_t step_index);
static uint32_t app_signal_detect_block_metric(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt);
static void app_signal_detect_set_lo(uint32_t lo_hz);
static void app_signal_detect_start_scan(void);
static void app_signal_detect_finish_step(uint32_t avg_metric);
static void app_signal_detect_finish_scan(void);

static uint16_t app_signal_detect_calc_step_count(void)
{
  uint32_t span_hz;
  uint32_t steps_u32;

  if ((APP_SIGDET_SCAN_STEP_HZ == 0UL) || (APP_SIGDET_SCAN_STOP_HZ < APP_SIGDET_SCAN_START_HZ))
  {
    return 1U;
  }

  span_hz = APP_SIGDET_SCAN_STOP_HZ - APP_SIGDET_SCAN_START_HZ;
  steps_u32 = (span_hz / APP_SIGDET_SCAN_STEP_HZ) + 1UL;
  if (steps_u32 > APP_SIGDET_MAX_STEPS)
  {
    steps_u32 = APP_SIGDET_MAX_STEPS;
  }

  return (uint16_t)steps_u32;
}

static uint32_t app_signal_detect_step_freq_hz(uint16_t step_index)
{
  /* 棰戠巼璁＄畻鍏紡锛歴tep_freq = start_freq + (step_index * step_size) */
  return APP_SIGDET_SCAN_START_HZ + ((uint32_t)step_index * APP_SIGDET_SCAN_STEP_HZ);
}

/* 璁＄畻鍗曚釜鏁版嵁鍧楃殑淇″彿璐ㄩ噺搴﹂噺锛岃繑鍥炲€艰秺澶ц〃绀轰俊鍙疯秺寮恒€傝繖閲屼娇鐢?I/Q 鏍锋湰鐨勬柟宸綔涓哄害閲忔寚鏍囥€?*/
static uint32_t app_signal_detect_block_metric(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt)
{
  uint64_t sum_i = 0ULL;
  uint64_t sum_q = 0ULL;
  uint64_t var_sum = 0ULL;
  uint32_t idx;
  int32_t mean_i;
  int32_t mean_q;

  if ((i_buf == 0) || (q_buf == 0) || (sample_cnt == 0U))
  {
    return 0U;
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
    int32_t di = (int32_t)i_buf[idx] - mean_i;
    int32_t dq = (int32_t)q_buf[idx] - mean_q;
    var_sum += (uint64_t)((di * di) + (dq * dq));
  }

  return (uint32_t)(var_sum / sample_cnt);
}

static void app_signal_detect_set_lo(uint32_t lo_hz)
{
  AppDdsCmd cmd;

  cmd = AppDDS_MakeSelectChCmd((uint8_t)APP_SIGDET_DDS_CHANNEL);
  (void)AppDDS_ExecuteCmd(&cmd);

  cmd = AppDDS_MakeSetFreqCmd(lo_hz);
  (void)AppDDS_ExecuteCmd(&cmd);

  cmd = AppDDS_MakeApplyCmd();
  (void)AppDDS_ExecuteCmd(&cmd);
}

static void app_signal_detect_start_scan(void)
{
  memset(&g_sigdet.metric_table[0], 0, sizeof(g_sigdet.metric_table));
  g_sigdet.step_metric_sum = 0U;
  g_sigdet.step_dwell_count = 0U;
  g_sigdet.best_step_index = 0U;

  g_sigdet.status.scanning = 1U;
  g_sigdet.status.carrier_present = 0U;
  g_sigdet.status.current_lo_hz = APP_SIGDET_SCAN_START_HZ;
  g_sigdet.status.estimated_carrier_hz = APP_SIGDET_SCAN_START_HZ;
  g_sigdet.status.best_metric = 0U;
  g_sigdet.status.noise_metric = 0U;
  g_sigdet.status.step_index = 0U;
  g_sigdet.status.step_count = app_signal_detect_calc_step_count();
  g_sigdet.status.scan_start_tick_ms = osKernelGetTickCount();
  g_sigdet.status.scan_finish_tick_ms = 0U;

  app_signal_detect_set_lo(g_sigdet.status.current_lo_hz);
}

static void app_signal_detect_finish_step(uint32_t avg_metric)
{
  uint16_t idx = g_sigdet.status.step_index;

  if (idx < APP_SIGDET_MAX_STEPS)
  {
    g_sigdet.metric_table[idx] = avg_metric;
  }

  if (avg_metric > g_sigdet.status.best_metric)
  {
    g_sigdet.status.best_metric = avg_metric;
    g_sigdet.best_step_index = idx;
  }
}

static void app_signal_detect_finish_scan(void)
{
  uint32_t noise_sum = 0U;
  uint32_t noise_cnt = 0U;
  uint16_t idx;

  for (idx = 0U; idx < g_sigdet.status.step_count; idx++)
  {
    if (idx == g_sigdet.best_step_index)
    {
      continue;
    }

    noise_sum += g_sigdet.metric_table[idx];
    noise_cnt++;
  }

  if (noise_cnt == 0U)
  {
    g_sigdet.status.noise_metric = 0U;
  }
  else
  {
    g_sigdet.status.noise_metric = noise_sum / noise_cnt;
  }

  if ((g_sigdet.status.noise_metric == 0U) && (g_sigdet.status.best_metric > 0U))
  {
    g_sigdet.status.carrier_present = 1U;
  }
  else if ((g_sigdet.status.best_metric * APP_SIGDET_CARRIER_RATIO_DEN) >
           (g_sigdet.status.noise_metric * APP_SIGDET_CARRIER_RATIO_NUM))
  {
    g_sigdet.status.carrier_present = 1U;
  }
  else
  {
    g_sigdet.status.carrier_present = 0U;
  }

  /* 鏃犺鏄惁妫€娴嬪埌杞芥尝锛岄兘灏?LO 璁句负鏈€浣虫瀵瑰簲鐨勯鐜囷紝鏂逛究鍚庣画浜哄伐璋冩暣鍜岃瀵熴€?*/
  g_sigdet.status.estimated_carrier_hz = app_signal_detect_step_freq_hz(g_sigdet.best_step_index);
  g_sigdet.status.current_lo_hz = g_sigdet.status.estimated_carrier_hz;
  g_sigdet.status.scanning = 0U;
  g_sigdet.status.scan_finish_tick_ms = osKernelGetTickCount();
}

void app_signal_detect_init(void)
{
  memset(&g_sigdet, 0, sizeof(g_sigdet));
  (void)AppDDS_Init();
  app_signal_detect_start_scan();
  g_sigdet.inited = 1U;
}

void app_signal_detect_request_rescan(void)
{
  if (g_sigdet.inited == 0U)
  {
    return;
  }

  app_signal_detect_start_scan();
}

/* ADC 鏁版嵁鍧楀鐞嗗叆鍙ｏ紝i_buf 鍜?q_buf 閮芥槸 uint16_t 绫诲瀷鐨勫師濮嬮噰鏍锋暟鎹紝sample_cnt 鏄瘡涓紦鍐插尯鐨勯噰鏍风偣鏁般€?*/
/* ADC block processing entry.
 * Fixed external mapping:
 *   i_buf = I path samples from PC4 / ADC1_INP4
 *   q_buf = Q path samples from PB1 / ADC2_INP5
 */
void app_signal_detect_process_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt)
{
  uint32_t metric;
  uint32_t now_tick;
  uint32_t avg_metric;

  if (g_sigdet.inited == 0U)
  {
    return;
  }

  if (g_sigdet.status.scanning == 0U)
  {
    now_tick = osKernelGetTickCount();
    if ((uint32_t)(now_tick - g_sigdet.status.scan_finish_tick_ms) >= APP_SIGDET_AUTO_RESCAN_MS)
    {
      app_signal_detect_start_scan();
    }
    return;
  }

  metric = app_signal_detect_block_metric(i_buf, q_buf, sample_cnt);
  g_sigdet.step_metric_sum += metric;
  g_sigdet.step_dwell_count++;

  if (g_sigdet.step_dwell_count < APP_SIGDET_DWELL_BLOCKS_PER_STEP)
  {
    return;
  }

  avg_metric = g_sigdet.step_metric_sum / g_sigdet.step_dwell_count;
  app_signal_detect_finish_step(avg_metric);

  g_sigdet.step_metric_sum = 0U;
  g_sigdet.step_dwell_count = 0U;

  if ((g_sigdet.status.step_index + 1U) < g_sigdet.status.step_count)
  {
    g_sigdet.status.step_index++;
    g_sigdet.status.current_lo_hz = app_signal_detect_step_freq_hz(g_sigdet.status.step_index);
    app_signal_detect_set_lo(g_sigdet.status.current_lo_hz);
  }
  else
  {
    app_signal_detect_finish_scan();
    app_signal_detect_set_lo(g_sigdet.status.estimated_carrier_hz);
  }
}

void app_signal_detect_get_status(app_signal_detect_status_t *status_out)
{
  if (status_out == 0)
  {
    return;
  }

  *status_out = g_sigdet.status;
}

