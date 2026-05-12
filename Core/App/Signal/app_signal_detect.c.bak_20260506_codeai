#include "app_signal_detect.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "cmsis_os2.h"
#include "app_dds_ctrl.h"
#include "RtosTypes.h"

#define APP_SIGDET_MAX_STEPS 512U
#define APP_SIGDET_INVALID_STEP 0xFFFFU

typedef struct
{
  uint32_t metric_vpp;
  uint32_t i_vpp;
  uint32_t q_vpp;
} app_signal_detect_vpp_t;

typedef struct
{
  uint8_t carrier_present;
  uint8_t locked;
  uint32_t estimated_hz;
} app_signal_detect_sweep_result_t;

typedef struct
{
  app_signal_detect_status_t status;
  uint32_t vpp_table[APP_SIGDET_MAX_STEPS];
  uint64_t step_vpp_sum;
  uint64_t step_i_vpp_sum;
  uint64_t step_q_vpp_sum;
  uint32_t coarse_estimate_hz;
  uint16_t step_dwell_count;
  uint16_t scan_dwell_blocks;
  uint32_t scan_start_hz;
  uint32_t scan_stop_hz;
  uint32_t scan_step_hz;
  uint8_t inited;
} app_signal_detect_ctx_t;

static app_signal_detect_ctx_t g_sigdet;

static uint16_t app_signal_detect_calc_step_count(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz);
static uint32_t app_signal_detect_step_freq_hz(uint32_t start_hz, uint32_t step_hz, uint16_t step_index);
static uint32_t app_signal_detect_clamp_to_valid_hz(uint32_t hz);
static void app_signal_detect_set_lo(uint32_t lo_hz);
static app_signal_detect_vpp_t app_signal_detect_block_vpp(const uint16_t *i_buf,
                                                           const uint16_t *q_buf,
                                                           uint32_t sample_cnt);

static void app_signal_detect_begin_scan(void);
static void app_signal_detect_begin_sweep(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz, uint8_t stage);
static void app_signal_detect_begin_fine_scan(uint32_t center_hz);
static void app_signal_detect_finish_step(uint32_t avg_vpp, uint32_t avg_i_vpp, uint32_t avg_q_vpp);
static app_signal_detect_sweep_result_t app_signal_detect_eval_current_sweep(void);
static void app_signal_detect_finish_current_sweep(void);
static void app_signal_detect_finish_locked(uint8_t carrier_present, uint8_t locked, uint32_t estimate_hz);
#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
static void app_signal_detect_trace_line(const char *text);
static void app_signal_detect_trace_point(uint32_t lo_hz, uint32_t vpp, uint32_t i_vpp, uint32_t q_vpp);
static void app_signal_detect_trace_sweep_done(const char *name);
#endif

#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
static void app_signal_detect_trace_line(const char *text)
{
  if (text != 0)
  {
    print_queue_send_log(text);
  }
}

static void app_signal_detect_trace_point(uint32_t lo_hz, uint32_t vpp, uint32_t i_vpp, uint32_t q_vpp)
{
  char line[96];

  (void)snprintf(line,
                 sizeof(line),
                 "vpp,%lu,%lu,%lu,%lu\r\n",
                 (unsigned long)lo_hz,
                 (unsigned long)vpp,
                 (unsigned long)i_vpp,
                 (unsigned long)q_vpp);
  app_signal_detect_trace_line(line);
}

static void app_signal_detect_trace_sweep_done(const char *name)
{
  char line[144];

  (void)snprintf(line,
                 sizeof(line),
                 "vpp_%s_done,est=%lu,peak=%lu,valley=%lu,th=%lu,left=%lu,right=%lu\r\n",
                 name,
                 (unsigned long)g_sigdet.status.estimated_carrier_hz,
                 (unsigned long)g_sigdet.status.peak_vpp_raw,
                 (unsigned long)g_sigdet.status.valley_vpp_raw,
                 (unsigned long)g_sigdet.status.threshold_vpp_raw,
                 (unsigned long)g_sigdet.status.left_edge_hz,
                 (unsigned long)g_sigdet.status.right_edge_hz);
  app_signal_detect_trace_line(line);
}
#endif

static uint16_t app_signal_detect_calc_step_count(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz)
{
  uint32_t span_hz;
  uint32_t steps_u32;

  if ((step_hz == 0UL) || (stop_hz < start_hz))
  {
    return 1U;
  }

  span_hz = stop_hz - start_hz;
  steps_u32 = (span_hz / step_hz) + 1UL;
  if (steps_u32 > APP_SIGDET_MAX_STEPS)
  {
    steps_u32 = APP_SIGDET_MAX_STEPS;
  }

  return (uint16_t)steps_u32;
}

static uint32_t app_signal_detect_step_freq_hz(uint32_t start_hz, uint32_t step_hz, uint16_t step_index)
{
  return start_hz + ((uint32_t)step_index * step_hz);
}

static uint32_t app_signal_detect_clamp_to_valid_hz(uint32_t hz)
{
  if (hz < APP_SIGDET_VALID_START_HZ)
  {
    return APP_SIGDET_VALID_START_HZ;
  }

  if (hz > APP_SIGDET_VALID_STOP_HZ)
  {
    return APP_SIGDET_VALID_STOP_HZ;
  }

  return hz;
}

static void app_signal_detect_set_lo(uint32_t lo_hz)
{
  AppDdsCmd cmd;

  cmd = AppDDS_MakeSetChFreqApplyCmd((uint8_t)APP_SIGDET_DDS_CHANNEL, lo_hz);
  (void)AppDDS_DispatchCmd(&cmd);
}

static app_signal_detect_vpp_t app_signal_detect_block_vpp(const uint16_t *i_buf,
                                                           const uint16_t *q_buf,
                                                           uint32_t sample_cnt)
{
  app_signal_detect_vpp_t out;
  uint32_t idx;
  uint16_t i_min;
  uint16_t i_max;
  uint16_t q_min;
  uint16_t q_max;

  memset(&out, 0, sizeof(out));

  if ((i_buf == 0) || (q_buf == 0) || (sample_cnt == 0U))
  {
    return out;
  }

  i_min = i_buf[0];
  i_max = i_buf[0];
  q_min = q_buf[0];
  q_max = q_buf[0];

  for (idx = 1U; idx < sample_cnt; idx++)
  {
    uint16_t i_sample = i_buf[idx];
    uint16_t q_sample = q_buf[idx];

    if (i_sample < i_min)
    {
      i_min = i_sample;
    }
    if (i_sample > i_max)
    {
      i_max = i_sample;
    }
    if (q_sample < q_min)
    {
      q_min = q_sample;
    }
    if (q_sample > q_max)
    {
      q_max = q_sample;
    }
  }

  out.i_vpp = (uint32_t)i_max - (uint32_t)i_min;
  out.q_vpp = (uint32_t)q_max - (uint32_t)q_min;
  out.metric_vpp = (out.i_vpp > out.q_vpp) ? out.i_vpp : out.q_vpp;

  return out;
}

#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
static const char *app_signal_detect_stage_name(uint8_t stage)
{
  if (stage == APP_SIGDET_STAGE_VPP_FINE_SCAN)
  {
    return "fine";
  }
  if (stage == APP_SIGDET_STAGE_VPP_LOCKED)
  {
    return "locked";
  }
  return "coarse";
}
#endif

static void app_signal_detect_begin_scan(void)
{
  memset(&g_sigdet.status, 0, sizeof(g_sigdet.status));
  g_sigdet.status.scan_start_tick_ms = osKernelGetTickCount();
  g_sigdet.coarse_estimate_hz = 0UL;

  app_signal_detect_begin_sweep(APP_SIGDET_SCAN_START_HZ,
                                APP_SIGDET_SCAN_STOP_HZ,
                                APP_SIGDET_SCAN_STEP_HZ,
                                APP_SIGDET_STAGE_VPP_COARSE_SCAN);
}

static void app_signal_detect_begin_sweep(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz, uint8_t stage)
{
  uint32_t overall_start_tick = g_sigdet.status.scan_start_tick_ms;

  memset(&g_sigdet.vpp_table[0], 0, sizeof(g_sigdet.vpp_table));

  g_sigdet.scan_start_hz = start_hz;
  g_sigdet.scan_stop_hz = stop_hz;
  g_sigdet.scan_step_hz = step_hz;
  if (g_sigdet.scan_step_hz == 0UL)
  {
    g_sigdet.scan_step_hz = 1UL;
  }

  g_sigdet.scan_dwell_blocks = APP_SIGDET_DWELL_BLOCKS_PER_STEP;
  if (g_sigdet.scan_dwell_blocks == 0U)
  {
    g_sigdet.scan_dwell_blocks = 1U;
  }

  g_sigdet.step_vpp_sum = 0ULL;
  g_sigdet.step_i_vpp_sum = 0ULL;
  g_sigdet.step_q_vpp_sum = 0ULL;
  g_sigdet.step_dwell_count = 0U;

  g_sigdet.status.scanning = 1U;
  g_sigdet.status.carrier_present = 0U;
  g_sigdet.status.locked = 0U;
  g_sigdet.status.stage = stage;
  g_sigdet.status.current_lo_hz = g_sigdet.scan_start_hz;
  g_sigdet.status.estimated_carrier_hz = g_sigdet.scan_start_hz;
  g_sigdet.status.current_vpp_raw = 0U;
  g_sigdet.status.current_i_vpp_raw = 0U;
  g_sigdet.status.current_q_vpp_raw = 0U;
  g_sigdet.status.peak_vpp_raw = 0U;
  g_sigdet.status.valley_vpp_raw = 0xFFFFFFFFUL;
  g_sigdet.status.threshold_vpp_raw = 0U;
  g_sigdet.status.left_edge_hz = 0U;
  g_sigdet.status.right_edge_hz = 0U;
  g_sigdet.status.step_index = 0U;
  g_sigdet.status.step_count = app_signal_detect_calc_step_count(g_sigdet.scan_start_hz,
                                                                  g_sigdet.scan_stop_hz,
                                                                  g_sigdet.scan_step_hz);
  g_sigdet.status.valley_step_index = APP_SIGDET_INVALID_STEP;
  g_sigdet.status.peak_step_index = 0U;
  g_sigdet.status.scan_start_tick_ms = overall_start_tick;
  g_sigdet.status.scan_finish_tick_ms = 0U;

  app_signal_detect_set_lo(g_sigdet.status.current_lo_hz);

#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
  {
    char line[128];

    (void)snprintf(line,
                   sizeof(line),
                   "vpp_stage,%s\r\n",
                   app_signal_detect_stage_name(stage));
    app_signal_detect_trace_line(line);

    (void)snprintf(line,
                   sizeof(line),
                   "vpp_begin,%lu,%lu,%lu,%u\r\n",
                   (unsigned long)g_sigdet.scan_start_hz,
                   (unsigned long)g_sigdet.scan_stop_hz,
                   (unsigned long)g_sigdet.scan_step_hz,
                   (unsigned)g_sigdet.scan_dwell_blocks);
    app_signal_detect_trace_line(line);
    app_signal_detect_trace_line("vpp,lo_hz,total_vpp,i_vpp,q_vpp\r\n");
  }
#endif
}

static void app_signal_detect_begin_fine_scan(uint32_t center_hz)
{
  uint32_t start_hz;
  uint32_t stop_hz;

  if (center_hz > (APP_SIGDET_SCAN_START_HZ + APP_SIGDET_FINE_SPAN_HZ))
  {
    start_hz = center_hz - APP_SIGDET_FINE_SPAN_HZ;
  }
  else
  {
    start_hz = APP_SIGDET_SCAN_START_HZ;
  }

  if (center_hz > (0xFFFFFFFFUL - APP_SIGDET_FINE_SPAN_HZ))
  {
    stop_hz = APP_SIGDET_SCAN_STOP_HZ;
  }
  else
  {
    stop_hz = center_hz + APP_SIGDET_FINE_SPAN_HZ;
    if (stop_hz > APP_SIGDET_SCAN_STOP_HZ)
    {
      stop_hz = APP_SIGDET_SCAN_STOP_HZ;
    }
  }

  if (stop_hz < start_hz)
  {
    stop_hz = start_hz;
  }

  app_signal_detect_begin_sweep(start_hz,
                                stop_hz,
                                APP_SIGDET_FINE_STEP_HZ,
                                APP_SIGDET_STAGE_VPP_FINE_SCAN);
}

static void app_signal_detect_finish_step(uint32_t avg_vpp, uint32_t avg_i_vpp, uint32_t avg_q_vpp)
{
  uint16_t idx = g_sigdet.status.step_index;

  if (idx < APP_SIGDET_MAX_STEPS)
  {
    g_sigdet.vpp_table[idx] = avg_vpp;
  }

  g_sigdet.status.current_vpp_raw = avg_vpp;
  g_sigdet.status.current_i_vpp_raw = avg_i_vpp;
  g_sigdet.status.current_q_vpp_raw = avg_q_vpp;

  if (avg_vpp > g_sigdet.status.peak_vpp_raw)
  {
    g_sigdet.status.peak_vpp_raw = avg_vpp;
    g_sigdet.status.peak_step_index = idx;
  }

  if ((g_sigdet.status.valley_step_index == APP_SIGDET_INVALID_STEP) ||
      (avg_vpp < g_sigdet.status.valley_vpp_raw))
  {
    g_sigdet.status.valley_vpp_raw = avg_vpp;
    g_sigdet.status.valley_step_index = idx;
  }

#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
  app_signal_detect_trace_point(app_signal_detect_step_freq_hz(g_sigdet.scan_start_hz,
                                                               g_sigdet.scan_step_hz,
                                                               idx),
                                avg_vpp,
                                avg_i_vpp,
                                avg_q_vpp);
#endif
}

static app_signal_detect_sweep_result_t app_signal_detect_eval_current_sweep(void)
{
  app_signal_detect_sweep_result_t result;
  uint32_t rise_raw;
  uint32_t threshold_delta;
  uint16_t idx;
  uint16_t first_active = APP_SIGDET_INVALID_STEP;
  uint16_t last_active = APP_SIGDET_INVALID_STEP;
  uint16_t valley_in_active = APP_SIGDET_INVALID_STEP;
  uint32_t valley_in_active_vpp = 0xFFFFFFFFUL;

  memset(&result, 0, sizeof(result));
  result.estimated_hz = g_sigdet.scan_start_hz;

  if (g_sigdet.status.valley_step_index == APP_SIGDET_INVALID_STEP)
  {
    g_sigdet.status.carrier_present = 0U;
    g_sigdet.status.locked = 0U;
    return result;
  }

  rise_raw = g_sigdet.status.peak_vpp_raw - g_sigdet.status.valley_vpp_raw;
  threshold_delta = rise_raw >> APP_SIGDET_VPP_THRESHOLD_SHIFT;
  if (threshold_delta < APP_SIGDET_VPP_MIN_RISE_RAW)
  {
    threshold_delta = APP_SIGDET_VPP_MIN_RISE_RAW;
  }
  g_sigdet.status.threshold_vpp_raw = g_sigdet.status.valley_vpp_raw + threshold_delta;

  if (rise_raw < APP_SIGDET_VPP_MIN_RISE_RAW)
  {
    result.estimated_hz = app_signal_detect_step_freq_hz(g_sigdet.scan_start_hz,
                                                         g_sigdet.scan_step_hz,
                                                         g_sigdet.status.valley_step_index);
    g_sigdet.status.carrier_present = 0U;
    g_sigdet.status.locked = 0U;
    g_sigdet.status.estimated_carrier_hz = result.estimated_hz;
    return result;
  }

  for (idx = 0U; idx < g_sigdet.status.step_count; idx++)
  {
    if (g_sigdet.vpp_table[idx] >= g_sigdet.status.threshold_vpp_raw)
    {
      if (first_active == APP_SIGDET_INVALID_STEP)
      {
        first_active = idx;
      }
      last_active = idx;
    }
  }

  if (first_active == APP_SIGDET_INVALID_STEP)
  {
    result.estimated_hz = app_signal_detect_step_freq_hz(g_sigdet.scan_start_hz,
                                                         g_sigdet.scan_step_hz,
                                                         g_sigdet.status.valley_step_index);
    g_sigdet.status.carrier_present = 0U;
    g_sigdet.status.locked = 0U;
    g_sigdet.status.estimated_carrier_hz = result.estimated_hz;
    return result;
  }

  for (idx = first_active; idx <= last_active; idx++)
  {
    if (g_sigdet.vpp_table[idx] < valley_in_active_vpp)
    {
      valley_in_active_vpp = g_sigdet.vpp_table[idx];
      valley_in_active = idx;
    }
  }

  g_sigdet.status.left_edge_hz = app_signal_detect_step_freq_hz(g_sigdet.scan_start_hz,
                                                                 g_sigdet.scan_step_hz,
                                                                 first_active);
  g_sigdet.status.right_edge_hz = app_signal_detect_step_freq_hz(g_sigdet.scan_start_hz,
                                                                  g_sigdet.scan_step_hz,
                                                                  last_active);

  if ((valley_in_active != APP_SIGDET_INVALID_STEP) &&
      (valley_in_active != first_active) &&
      (valley_in_active != last_active))
  {
    result.estimated_hz = app_signal_detect_step_freq_hz(g_sigdet.scan_start_hz,
                                                         g_sigdet.scan_step_hz,
                                                         valley_in_active);
  }
  else
  {
    result.estimated_hz =
        (g_sigdet.status.left_edge_hz / 2UL) + (g_sigdet.status.right_edge_hz / 2UL) +
        ((g_sigdet.status.left_edge_hz & 1UL) & (g_sigdet.status.right_edge_hz & 1UL));
  }

  result.carrier_present = 1U;
  result.locked = 1U;
  g_sigdet.status.carrier_present = 1U;
  g_sigdet.status.locked = 0U;
  g_sigdet.status.estimated_carrier_hz = result.estimated_hz;

  return result;
}

static void app_signal_detect_finish_current_sweep(void)
{
  app_signal_detect_sweep_result_t result;
  uint8_t stage = g_sigdet.status.stage;

  result = app_signal_detect_eval_current_sweep();

#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
  app_signal_detect_trace_sweep_done(app_signal_detect_stage_name(stage));
#endif

  if ((stage == APP_SIGDET_STAGE_VPP_COARSE_SCAN) && (result.carrier_present != 0U))
  {
    g_sigdet.coarse_estimate_hz = result.estimated_hz;
    app_signal_detect_begin_fine_scan(g_sigdet.coarse_estimate_hz);
    return;
  }

  app_signal_detect_finish_locked(result.carrier_present, result.locked, result.estimated_hz);
}

static void app_signal_detect_finish_locked(uint8_t carrier_present, uint8_t locked, uint32_t estimate_hz)
{
  uint32_t final_hz = app_signal_detect_clamp_to_valid_hz(estimate_hz);

  if (final_hz != APP_SIGDET_VALID_STOP_HZ)
  {
    uint32_t half_fine_step_hz = APP_SIGDET_FINE_STEP_HZ / 2UL;

    if (final_hz > half_fine_step_hz)
    {
      final_hz -= half_fine_step_hz;
    }
    else
    {
      final_hz = 0UL;
    }
    final_hz = app_signal_detect_clamp_to_valid_hz(final_hz);
  }

  g_sigdet.status.estimated_carrier_hz = final_hz;
  g_sigdet.status.current_lo_hz = final_hz;
  g_sigdet.status.carrier_present = carrier_present;
  g_sigdet.status.locked = (carrier_present != 0U) ? locked : 0U;
  g_sigdet.status.scanning = 0U;
  g_sigdet.status.stage = APP_SIGDET_STAGE_VPP_LOCKED;
  g_sigdet.status.scan_finish_tick_ms = osKernelGetTickCount();

  app_signal_detect_set_lo(final_hz);

#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
  {
    char line[144];

    (void)snprintf(line,
                   sizeof(line),
                   "vpp_done,est=%lu,peak=%lu,valley=%lu,th=%lu,left=%lu,right=%lu\r\n",
                   (unsigned long)g_sigdet.status.estimated_carrier_hz,
                   (unsigned long)g_sigdet.status.peak_vpp_raw,
                   (unsigned long)g_sigdet.status.valley_vpp_raw,
                   (unsigned long)g_sigdet.status.threshold_vpp_raw,
                   (unsigned long)g_sigdet.status.left_edge_hz,
                   (unsigned long)g_sigdet.status.right_edge_hz);
    app_signal_detect_trace_line(line);
  }
#endif
}

void app_signal_detect_init(void)
{
  memset(&g_sigdet, 0, sizeof(g_sigdet));
  app_signal_detect_begin_scan();
  g_sigdet.inited = 1U;
}

void app_signal_detect_request_rescan(void)
{
  if (g_sigdet.inited == 0U)
  {
    return;
  }

  app_signal_detect_begin_scan();
}

/* ADC block processing entry.
 * Fixed external mapping:
 *   i_buf = I path samples from PC4 / ADC1_INP4
 *   q_buf = Q path samples from PB1 / ADC2_INP5
 */
void app_signal_detect_process_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt)
{
  app_signal_detect_vpp_t vpp;
  uint32_t avg_vpp;
  uint32_t avg_i_vpp;
  uint32_t avg_q_vpp;

  if ((g_sigdet.inited == 0U) || (g_sigdet.status.scanning == 0U))
  {
    return;
  }

  vpp = app_signal_detect_block_vpp(i_buf, q_buf, sample_cnt);
  g_sigdet.step_vpp_sum += vpp.metric_vpp;
  g_sigdet.step_i_vpp_sum += vpp.i_vpp;
  g_sigdet.step_q_vpp_sum += vpp.q_vpp;
  g_sigdet.step_dwell_count++;

  if (g_sigdet.step_dwell_count < g_sigdet.scan_dwell_blocks)
  {
    return;
  }

  avg_vpp = (uint32_t)(g_sigdet.step_vpp_sum / g_sigdet.step_dwell_count);
  avg_i_vpp = (uint32_t)(g_sigdet.step_i_vpp_sum / g_sigdet.step_dwell_count);
  avg_q_vpp = (uint32_t)(g_sigdet.step_q_vpp_sum / g_sigdet.step_dwell_count);
  app_signal_detect_finish_step(avg_vpp, avg_i_vpp, avg_q_vpp);

  g_sigdet.step_vpp_sum = 0ULL;
  g_sigdet.step_i_vpp_sum = 0ULL;
  g_sigdet.step_q_vpp_sum = 0ULL;
  g_sigdet.step_dwell_count = 0U;

  if ((g_sigdet.status.step_index + 1U) < g_sigdet.status.step_count)
  {
    g_sigdet.status.step_index++;
    g_sigdet.status.current_lo_hz = app_signal_detect_step_freq_hz(g_sigdet.scan_start_hz,
                                                                    g_sigdet.scan_step_hz,
                                                                    g_sigdet.status.step_index);
    app_signal_detect_set_lo(g_sigdet.status.current_lo_hz);
    return;
  }

  app_signal_detect_finish_current_sweep();
}

void app_signal_detect_get_status(app_signal_detect_status_t *status_out)
{
  if (status_out == 0)
  {
    return;
  }

  *status_out = g_sigdet.status;
}


