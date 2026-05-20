#include "app_sweep.h"

#include <string.h>

#include <stdio.h>
#include "AppDebugConfig.h"
#include "RtosTypes.h"

#include "app_dds_ctrl.h"

#define APP_SWEEP_MAX_STEPS 512U
#define APP_SWEEP_INVALID_STEP 0xFFFFU
#define APP_SWEEP_DDS_CHANNEL 0U
#define APP_SWEEP_DWELL_BLOCKS_PER_STEP 2U
#define APP_SWEEP_MIN_RISE_RAW 80UL
#define APP_SWEEP_THRESHOLD_SHIFT 2U
#define APP_SWEEP_SETTLE_BLOCKS 2U

#define APP_SWEEP_FINAL_LO_OFFSET_HZ 10000UL /* 最终频点微调时的 LO 偏移，单位 Hz */
#define SWEEP_POINT_LOG_ENABLE 1U

typedef enum
{
    APP_SWEEP_STAGE_COARSE = 0,
    APP_SWEEP_STAGE_FINE,
    APP_SWEEP_STAGE_DONE
} app_sweep_stage_t;

typedef struct
{
    uint8_t active;
    app_sweep_stage_t stage;
    uint32_t request_start_hz;
    uint32_t request_stop_hz;
    uint32_t request_step_hz;
    uint32_t stage_start_hz;
    uint32_t stage_stop_hz;
    uint32_t stage_step_hz;
    uint32_t center_hz;
    uint32_t coarse_center_hz;
    uint32_t peak_vpp;
    uint32_t valley_vpp;
    uint16_t peak_step;
    uint16_t valley_step;
    uint16_t step_index;
    uint16_t step_count;
    uint16_t dwell_count;
    uint64_t dwell_vpp_sum;
    uint32_t vpp_table[APP_SWEEP_MAX_STEPS];
    uint32_t target_freq_hz;
    uint8_t settle_blocks;
} app_sweep_ctx_t;

static app_sweep_ctx_t g_sweep;

static uint16_t sweep_step_count(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz);
static uint32_t sweep_freq_at(uint32_t start_hz, uint32_t step_hz, uint16_t step_index);
static uint32_t sweep_clip_to_request(uint32_t hz);
static uint32_t sweep_mid_hz(uint32_t left_hz, uint32_t right_hz);
static void sweep_set_dds(uint32_t freq_hz);
static void sweep_start_stage(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz, app_sweep_stage_t stage);
static void sweep_start_coarse(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz);
static void sweep_start_fine(uint32_t center_hz);
static uint32_t sweep_measure_vpp(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt);
static uint32_t sweep_eval_stage(uint8_t *found_out);
static void sweep_save_current_step(uint32_t avg_vpp);
static uint8_t sweep_request_changed(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz);
#if (SWEEP_POINT_LOG_ENABLE != 0U)
static void sweep_log_point(uint32_t freq_hz, uint16_t step_index, uint32_t avg_vpp);
#endif
static uint8_t sweep_dds_ready_for_target(void);

static uint16_t sweep_step_count(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz)
{
    uint32_t span_hz;
    uint32_t count;

    if ((step_hz == 0UL) || (stop_hz < start_hz))
    {
        return 1U;
    }

    span_hz = stop_hz - start_hz;
    count = (span_hz / step_hz) + 1UL;
    if (count > APP_SWEEP_MAX_STEPS)
    {
        count = APP_SWEEP_MAX_STEPS;
    }

    return (uint16_t)count;
}

static uint32_t sweep_freq_at(uint32_t start_hz, uint32_t step_hz, uint16_t step_index)
{
    return start_hz + ((uint32_t)step_index * step_hz);
}

static uint32_t sweep_clip_to_request(uint32_t hz)
{
    if (hz < g_sweep.request_start_hz)
    {
        return g_sweep.request_start_hz;
    }

    if (hz > g_sweep.request_stop_hz)
    {
        return g_sweep.request_stop_hz;
    }

    return hz;
}

static uint32_t sweep_mid_hz(uint32_t left_hz, uint32_t right_hz)
{
    return (left_hz / 2UL) + (right_hz / 2UL) + ((left_hz & 1UL) & (right_hz & 1UL));
}

static void sweep_set_dds(uint32_t freq_hz)
{
    AppDdsCmd cmd = AppDDS_MakeSetChFreqApplyCmd((uint8_t)APP_SWEEP_DDS_CHANNEL, freq_hz);

    g_sweep.target_freq_hz = freq_hz;
    g_sweep.settle_blocks = APP_SWEEP_SETTLE_BLOCKS;

    (void)AppDDS_DispatchCmd(&cmd);
}

static uint8_t sweep_dds_ready_for_target(void)
{
    const AppDdsStatus *st = AppDDS_GetStatus();
    uint8_t ch_mask = (uint8_t)(1U << APP_SWEEP_DDS_CHANNEL);

    if (st->hw_ready == 0U)
    {
        return 0U;
    }

    if (st->freq_hz[APP_SWEEP_DDS_CHANNEL] != g_sweep.target_freq_hz)
    {
        return 0U;
    }

    if ((st->dirty_mask & ch_mask) != 0U)
    {
        return 0U;
    }

    if (st->last_err != 0)
    {
        return 0U;
    }

    return 1U;
}

static void sweep_start_stage(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz, app_sweep_stage_t stage)
{
    memset(g_sweep.vpp_table, 0, sizeof(g_sweep.vpp_table));

    g_sweep.stage = stage;
    g_sweep.stage_start_hz = start_hz;
    g_sweep.stage_stop_hz = stop_hz;
    g_sweep.stage_step_hz = (step_hz == 0UL) ? 1UL : step_hz;
    g_sweep.step_count = sweep_step_count(g_sweep.stage_start_hz,
                                           g_sweep.stage_stop_hz,
                                           g_sweep.stage_step_hz);
    g_sweep.step_index = 0U;
    g_sweep.dwell_count = 0U;
    g_sweep.dwell_vpp_sum = 0ULL;
    g_sweep.peak_vpp = 0U;
    g_sweep.valley_vpp = 0xFFFFFFFFUL;
    g_sweep.peak_step = 0U;
    g_sweep.valley_step = APP_SWEEP_INVALID_STEP;
    g_sweep.active = 1U;

    sweep_set_dds(g_sweep.stage_start_hz);
}

static void sweep_start_coarse(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz)
{
    memset(&g_sweep, 0, sizeof(g_sweep));
    g_sweep.request_start_hz = start_hz;
    g_sweep.request_stop_hz = stop_hz;
    g_sweep.request_step_hz = (step_hz == 0UL) ? 1UL : step_hz;
    sweep_start_stage(g_sweep.request_start_hz,
                      g_sweep.request_stop_hz,
                      g_sweep.request_step_hz,
                      APP_SWEEP_STAGE_COARSE);
}

static void sweep_start_fine(uint32_t center_hz)
{
    uint32_t fine_span_hz = g_sweep.request_step_hz * 2UL;
    uint32_t fine_step_hz = g_sweep.request_step_hz / 6UL;
    uint32_t fine_start_hz;
    uint32_t fine_stop_hz;

    if (fine_step_hz == 0UL)
    {
        fine_step_hz = 1UL;
    }

    fine_start_hz = (center_hz > fine_span_hz) ? (center_hz - fine_span_hz) : g_sweep.request_start_hz;
    fine_stop_hz = center_hz + fine_span_hz;

    fine_start_hz = sweep_clip_to_request(fine_start_hz);
    fine_stop_hz = sweep_clip_to_request(fine_stop_hz);
    if (fine_stop_hz < fine_start_hz)
    {
        fine_stop_hz = fine_start_hz;
    }

    sweep_start_stage(fine_start_hz, fine_stop_hz, fine_step_hz, APP_SWEEP_STAGE_FINE);
}

static uint32_t sweep_measure_vpp(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt)
{
    uint32_t idx;
    uint16_t i_min;
    uint16_t i_max;
    uint16_t q_min;
    uint16_t q_max;
    uint32_t i_vpp;
    uint32_t q_vpp;

    if ((i_buf == 0) || (q_buf == 0) || (sample_cnt == 0U))
    {
        return 0U;
    }

    i_min = i_buf[0];
    i_max = i_buf[0];
    q_min = q_buf[0];
    q_max = q_buf[0];

    for (idx = 1U; idx < sample_cnt; idx++)
    {
        uint16_t i_sample = i_buf[idx];
        uint16_t q_sample = q_buf[idx];

        if (i_sample < i_min) { i_min = i_sample; }
        if (i_sample > i_max) { i_max = i_sample; }
        if (q_sample < q_min) { q_min = q_sample; }
        if (q_sample > q_max) { q_max = q_sample; }
    }

    i_vpp = (uint32_t)i_max - (uint32_t)i_min;
    q_vpp = (uint32_t)q_max - (uint32_t)q_min;
    return (i_vpp > q_vpp) ? i_vpp : q_vpp;
}

static uint32_t sweep_eval_stage(uint8_t *found_out)
{
    uint32_t rise_vpp;
    uint32_t threshold_vpp;
    uint16_t first_active = APP_SWEEP_INVALID_STEP;
    uint16_t last_active = APP_SWEEP_INVALID_STEP;
    uint16_t lowest_step = APP_SWEEP_INVALID_STEP;
    uint32_t lowest_vpp = 0xFFFFFFFFUL;
    uint16_t idx;

    if (found_out != 0)
    {
        *found_out = 0U;
    }

    if (g_sweep.valley_step == APP_SWEEP_INVALID_STEP)
    {
        return g_sweep.stage_start_hz;
    }

    rise_vpp = g_sweep.peak_vpp - g_sweep.valley_vpp;
    if (rise_vpp < APP_SWEEP_MIN_RISE_RAW)
    {
        return sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, g_sweep.valley_step);
    }

    threshold_vpp = g_sweep.valley_vpp + (rise_vpp >> APP_SWEEP_THRESHOLD_SHIFT);
    if ((threshold_vpp - g_sweep.valley_vpp) < APP_SWEEP_MIN_RISE_RAW)
    {
        threshold_vpp = g_sweep.valley_vpp + APP_SWEEP_MIN_RISE_RAW;
    }

    for (idx = 0U; idx < g_sweep.step_count; idx++)
    {
        if (g_sweep.vpp_table[idx] >= threshold_vpp)
        {
            if (first_active == APP_SWEEP_INVALID_STEP)
            {
                first_active = idx;
            }
            last_active = idx;
        }
    }

    if (first_active == APP_SWEEP_INVALID_STEP)
    {
        return sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, g_sweep.valley_step);
    }

    for (idx = first_active; idx <= last_active; idx++)
    {
        if (g_sweep.vpp_table[idx] < lowest_vpp)
        {
            lowest_vpp = g_sweep.vpp_table[idx];
            lowest_step = idx;
        }
    }

    if (found_out != 0)
    {
        *found_out = 1U;
    }

    if ((lowest_step != APP_SWEEP_INVALID_STEP) &&
        (lowest_step != first_active) &&
        (lowest_step != last_active))
    {
        return sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, lowest_step);
    }

    return sweep_mid_hz(sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, first_active),
                        sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, last_active));
}

/* ------------扫频频点信息打印测试----------------- */
#if (SWEEP_POINT_LOG_ENABLE != 0U)
static void sweep_log_point(uint32_t freq_hz, uint16_t step_index, uint32_t avg_vpp)
{
#if (APP_PRINT_LOG_ENABLE != 0U)
    char line[126];
    int n;

    n = snprintf(line,
                 sizeof(line),
                 "%lu.%03lu,%lu,%u,%u\r\n",
                 (unsigned long)(freq_hz / 1000000UL),
                 (unsigned long)((freq_hz % 1000000UL) / 1000UL),
                 (unsigned long)avg_vpp,
                 (unsigned int)step_index,
                 (unsigned int)g_sweep.stage);

    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
        print_queue_send(line);
    }
#else
    (void)freq_hz;
    (void)step_index;
    (void)avg_vpp;
#endif
}
#endif

static void sweep_save_current_step(uint32_t avg_vpp)
{
    uint16_t idx = g_sweep.step_index;

    if (idx < APP_SWEEP_MAX_STEPS)
    {
        g_sweep.vpp_table[idx] = avg_vpp;
    }

    if (avg_vpp > g_sweep.peak_vpp)
    {
        g_sweep.peak_vpp = avg_vpp;
        g_sweep.peak_step = idx;
    }

    if ((g_sweep.valley_step == APP_SWEEP_INVALID_STEP) || (avg_vpp < g_sweep.valley_vpp))
    {
        g_sweep.valley_vpp = avg_vpp;
        g_sweep.valley_step = idx;
    }
}

static uint8_t sweep_request_changed(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz)
{
    uint32_t fixed_step_hz = (step_hz == 0UL) ? 1UL : step_hz;

    return ((g_sweep.request_start_hz != start_hz) ||
            (g_sweep.request_stop_hz != stop_hz) ||
            (g_sweep.request_step_hz != fixed_step_hz)) ? 1U : 0U;
}

uint32_t app_sweep_find_center_hz(uint32_t start_hz,
                                  uint32_t stop_hz,
                                  uint32_t step_hz,
                                  const uint16_t *i_buf,
                                  const uint16_t *q_buf,
                                  uint32_t sample_cnt)
{
    uint32_t block_vpp;
    uint32_t avg_vpp;
    uint8_t found;
    uint32_t estimate_hz;

    if (stop_hz < start_hz)
    {
        return 0U;
    }

    if ((g_sweep.active == 0U) && (g_sweep.stage != APP_SWEEP_STAGE_DONE))
    {
        if (AppDDS_GetStatus()->hw_ready == 0U)
        {
            return 0U;
        }

        sweep_start_coarse(start_hz, stop_hz, step_hz);
        return 0U;
    }

    /* 如果当 请求参数未改变 且 未复位扫频状态 就退出 */
    if ((g_sweep.stage == APP_SWEEP_STAGE_DONE) && (sweep_request_changed(start_hz, stop_hz, step_hz) == 0U))
    {
        return g_sweep.center_hz;
    }

    if (sweep_request_changed(start_hz, stop_hz, step_hz) != 0U)
    {
        sweep_start_coarse(start_hz, stop_hz, step_hz);
        return 0U;
    }

    if (sweep_dds_ready_for_target() == 0U)
    {
        return 0U;
    }

    if (g_sweep.settle_blocks > 0U)
    {
        g_sweep.settle_blocks--;
        return 0U;
    }

    block_vpp = sweep_measure_vpp(i_buf, q_buf, sample_cnt);
    g_sweep.dwell_vpp_sum += block_vpp;
    g_sweep.dwell_count++;

    if (g_sweep.dwell_count < APP_SWEEP_DWELL_BLOCKS_PER_STEP)
    {
        return 0U;
    }

    avg_vpp = (uint32_t)(g_sweep.dwell_vpp_sum / (uint64_t)g_sweep.dwell_count);
    sweep_save_current_step(avg_vpp);
#if (SWEEP_POINT_LOG_ENABLE != 0U)
    sweep_log_point(sweep_freq_at(g_sweep.stage_start_hz,
                              g_sweep.stage_step_hz,
                              g_sweep.step_index),
                g_sweep.step_index,
                avg_vpp);
#endif
    g_sweep.dwell_vpp_sum = 0ULL;
    g_sweep.dwell_count = 0U;

    if ((g_sweep.step_index + 1U) < g_sweep.step_count)
    {
        g_sweep.step_index++;
        sweep_set_dds(sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, g_sweep.step_index));
        return 0U;
    }

    estimate_hz = sweep_eval_stage(&found);
    if ((g_sweep.stage == APP_SWEEP_STAGE_COARSE) && (found != 0U))
    {
        g_sweep.coarse_center_hz = sweep_clip_to_request(estimate_hz);
        sweep_start_fine(g_sweep.coarse_center_hz);
        return 0U;
    }

    if (found == 0U)
    {
        sweep_start_coarse(start_hz, stop_hz, step_hz);
        return 0U;
    }

    g_sweep.center_hz = sweep_clip_to_request(estimate_hz);
    g_sweep.stage = APP_SWEEP_STAGE_DONE;
    g_sweep.active = 0U;
    sweep_set_dds(g_sweep.center_hz + APP_SWEEP_FINAL_LO_OFFSET_HZ);
    return g_sweep.center_hz;
}

void app_sweep_reset(void)
{
    memset(&g_sweep, 0, sizeof(g_sweep));
}
