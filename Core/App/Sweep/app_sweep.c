#include "app_sweep.h"

#include <string.h>

#include <stdio.h>
#include "AppDebugConfig.h"
#include "RtosTypes.h"

#include "app_dds_ctrl.h"
#include "app_ad9959.h"
#include "app_board_flash.h"

#define APP_SWEEP_MAX_STEPS 512U
#define APP_SWEEP_INVALID_STEP 0xFFFFU
#define APP_SWEEP_DDS_CHANNEL 0U
#define APP_SWEEP_DDS_MAX_AMP_CODE 1023U
#define APP_SWEEP_DDS_BASE_AMP_CODE APP_AD9959_STARTUP_CH0_AMP_CODE
#define APP_SWEEP_DDS_AMP_GAIN_NUM 3U
#define APP_SWEEP_DDS_AMP_GAIN_DEN 2U
#define APP_SWEEP_DDS_AMP_CODE_UNCLIPPED \
    (((uint32_t)APP_SWEEP_DDS_BASE_AMP_CODE * APP_SWEEP_DDS_AMP_GAIN_NUM) / APP_SWEEP_DDS_AMP_GAIN_DEN)
#define APP_SWEEP_DDS_AMP_CODE \
    ((APP_SWEEP_DDS_AMP_CODE_UNCLIPPED > APP_SWEEP_DDS_MAX_AMP_CODE) ? APP_SWEEP_DDS_MAX_AMP_CODE : APP_SWEEP_DDS_AMP_CODE_UNCLIPPED)
#define APP_SWEEP_DWELL_BLOCKS_PER_STEP 2U
#define APP_SWEEP_MIN_RISE_RAW 80UL
#define APP_SWEEP_THRESHOLD_SHIFT 2U
#define APP_SWEEP_SETTLE_BLOCKS 2U

#ifndef APP_SWEEP_FINAL_LO_OFFSET_HZ
#define APP_SWEEP_FINAL_LO_OFFSET_HZ 5000UL /* 最终频点微调时的 LO 偏移，单位 Hz */
#endif
#define SWEEP_POINT_LOG_ENABLE 1U
/* 候选、封锁和细扫参数保持为编译期常量，便于实机快速切换。 */
#define APP_SWEEP_COARSE_STEP_HZ APP_SWEEP_DEFAULT_STEP_HZ /* 粗扫每次跳多少 Hz；置 0 时才使用函数传入的步进。 */
#define APP_SWEEP_MAX_CANDIDATES 16U              /* 每轮最多保留和打印多少个候选段。 */
#define APP_SWEEP_BLOCK_MATCH_MIN_TOL_HZ 300000UL /* 候选频率离封锁频率这么近就丢弃，单位 Hz；现场强电台会有裙边，不能只挡中心点。 */
#define APP_SWEEP_CANDIDATE_LOG_ENABLE 1U         /* 是否打印候选汇总和候选明细，便于现场核对锁点。 */
#define APP_SWEEP_CANDIDATE_DEBUG_LOG_ENABLE 1U   /* 是否额外打印候选段、双峰和谷点，方便核对算法。 */
#define APP_SWEEP_CANDIDATE_BRIDGE_GAP_STEPS 2U   /* 候选段中间短暂掉下阈值时，最多允许跨过几个点继续合并。 */
#define APP_SWEEP_CANDIDATE_PLATEAU_DROP_RAW 150UL /* 段内最高点和最低点差值小于该值，就当平台型信号，取段中点。 */
#define APP_SWEEP_CANDIDATE_CONCAVE_DIP_RAW 800UL /* 双峰之间谷点比低肩峰至少低这么多，才当凹谷型信号取谷点。 */
#define APP_SWEEP_CANDIDATE_MIN_PEAK_GAP_STEPS 2U /* 两个肩峰至少间隔几个 step；小于该值视为同一侧凸起。 */
#define APP_SWEEP_CANDIDATE_WIDE_CONVEX_MID_STEPS 5U /* 非凹谷、非平台但候选段足够宽时，取段中点，减少宽信号锁到肩峰。 */
#define APP_SWEEP_CANDIDATE_MAX_CONVEX_WIDTH_STEPS 10U /* 非凹谷、非平台的凸峰太宽时更像环境宽干扰，超过该宽度直接丢弃。 */
#define APP_SWEEP_FINE_HOLD_COARSE_MIN_WIDTH_STEPS 5U /* 细扫宽凸峰容易被调制瞬时峰值拉偏；达到该宽度时优先沿用粗扫中心。 */
#define APP_SWEEP_FINE_FALLBACK_TO_COARSE_ENABLE 1U /* 细扫没有有效候选时是否回退到粗扫中心，避免弱目标被细扫空结果误判为未锁定。 */
#define APP_SWEEP_CANDIDATE_MIN_PEAK_VPP 3500UL    /* 任务扫频候选的最低峰值；低于它的候选直接不要。 */
#define APP_SWEEP_CANDIDATE_MIN_SCORE_VPP 2500UL  /* 任务扫频候选至少要比底噪高这么多；低于它直接不要。 */
#define APP_SWEEP_CAL_BASELINE_MIN_VPP 4000UL     /* 只管校准扣减：校准点高于它，任务扫频才会在附近做基线扣减；它不是候选剔除门限。 */
#define APP_SWEEP_CAL_BASELINE_SPREAD_HZ 350000UL /* 某个校准点超过上面门限后，左右各多少 Hz 也一起启用基线扣减。 */
#define APP_SWEEP_MAX_RETRY_COUNT 3U              /* 没找到有效候选时最多重扫几次，不含第一次。 */
#define APP_SWEEP_FINE_STAGE_ENABLE 1U            /* 是否开启细扫：1=粗扫后再细扫，0=粗扫结果直接锁定。 */
#define APP_SWEEP_FINE_FIXED_STEP_ENABLE 1U       /* 细扫步进来源：1=使用下面固定值，0=使用粗扫步进的 1/6。 */
#define APP_SWEEP_FINE_STEP_HZ 50000UL            /* 细扫固定步进是多少 Hz；当前先用 50k，兼顾精度和速度。 */
#define APP_SWEEP_FINE_HALF_SPAN_HZ 500000UL      /* 细扫中心左右各扫多少 Hz；200k 截止滤波器下先覆盖 ±500k。 */
#define APP_SWEEP_ADC_CLIP_LOW 10U                /* ADC 小于等于该值，认为接近下限打满。 */
#define APP_SWEEP_ADC_CLIP_HIGH 16373U            /* ADC 大于等于该值，认为接近上限打满。 */
#define APP_SWEEP_CLIP_LOG_ENABLE 0U              /* 是否逐点打印 ADC 打满明细；平时关掉，避免串口太多。 */
#define APP_SWEEP_CAL_POINT_LOG_ENABLE 1U         /* 校准时是否逐点打印基线，方便在 VOFA 看校准曲线。 */

#define APP_SWEEP_CAL_FLASH_ENABLE 1U             /* 是否把校准基线保存到板载 Flash；关掉后只保留 RAM 校准。 */
#define APP_SWEEP_CAL_FLASH_ADDR (APP_BOARD_FLASH_TOTAL_SIZE - APP_BOARD_FLASH_SECTOR_SIZE) /* 校准数据固定使用最后一个 4K 扇区。 */
#define APP_SWEEP_CAL_FLASH_MAGIC 0x53574341UL    /* Flash 校准记录标识，防止误读其他数据。 */
#define APP_SWEEP_CAL_FLASH_VERSION 1UL           /* Flash 校准记录版本，结构变化时递增。 */
#define APP_SWEEP_CAL_FLASH_CRC_SEED 2166136261UL /* 校准记录校验种子，用于判断掉电数据是否完整。 */

#define APP_SWEEP_BLOCK_THRESHOLD_IGNORE_HZ 300000UL /* 封锁频点附近这么宽不参与全局阈值估计，避免强电台抬高门槛。 */

typedef enum
{
    APP_SWEEP_STAGE_COARSE = 0,
    APP_SWEEP_STAGE_FINE,
    APP_SWEEP_STAGE_DONE
} app_sweep_stage_t;

typedef enum
{
    APP_SWEEP_CANDIDATE_SHAPE_PLATEAU = 0,
    APP_SWEEP_CANDIDATE_SHAPE_CONVEX,
    APP_SWEEP_CANDIDATE_SHAPE_CONCAVE
} app_sweep_candidate_shape_t;

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
    uint8_t dwell_clip_seen;
    uint16_t dwell_i_min;
    uint16_t dwell_i_max;
    uint16_t dwell_q_min;
    uint16_t dwell_q_max;
    uint32_t vpp_table[APP_SWEEP_MAX_STEPS];
    uint32_t target_freq_hz;
    uint8_t settle_blocks;
    uint8_t retry_count;
} app_sweep_ctx_t;

/* 候选频点由过阈值区间生成，可合并短凹谷，用于封锁过滤和最终排序。 */
typedef struct
{
    uint32_t freq_hz;
    uint32_t peak_vpp;
    uint32_t valley_vpp;
    uint32_t score_vpp;
    uint32_t block_hz;
    uint16_t start_step;
    uint16_t stop_step;
    uint16_t peak_step;
    uint16_t second_peak_step;
    uint16_t valley_step;
    uint16_t width_steps;
    uint8_t blocked;
    app_sweep_candidate_shape_t shape;
} app_sweep_candidate_t;

typedef struct
{
    uint32_t vpp;
    uint16_t i_min;
    uint16_t i_max;
    uint16_t q_min;
    uint16_t q_max;
    uint8_t clip;
} app_sweep_measure_t;

typedef struct
{
    uint32_t start_hz;
    uint32_t stop_hz;
    uint32_t step_hz;
    uint64_t baseline_sum_vpp;
    uint32_t baseline_avg_vpp;
    uint32_t baseline_vpp[APP_SWEEP_MAX_STEPS];
    uint8_t clip_flag[APP_SWEEP_MAX_STEPS];
    app_sweep_calibration_stats_t stats;
} app_sweep_calibration_ctx_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    uint32_t start_hz;
    uint32_t stop_hz;
    uint32_t step_hz;
    uint32_t point_count;
    uint32_t clip_count;
    uint32_t max_vpp;
    uint32_t min_vpp;
    uint32_t baseline_avg_vpp;
    uint32_t baseline_vpp[APP_SWEEP_MAX_STEPS];
    uint8_t clip_flag[APP_SWEEP_MAX_STEPS];
    uint32_t crc;
} app_sweep_cal_flash_record_t;

static app_sweep_ctx_t g_sweep;
static app_sweep_calibration_ctx_t g_sweep_cal;
static app_sweep_cal_flash_record_t g_sweep_cal_flash_record;

/* 频率封锁表：按 Hz 填写；0UL 为占位值，会被匹配逻辑忽略。 */
static const uint32_t g_sweep_block_freq_hz[] =
{
    //112500000UL, /* 现场固定电台，避免任务扫频误锁到 112.5 MHz。 */
    125000000UL  /* 现场固定电台，避免任务扫频误锁到 125 MHz。 */
};

static uint16_t sweep_step_count(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz);
static uint32_t sweep_freq_at(uint32_t start_hz, uint32_t step_hz, uint16_t step_index);
static uint32_t sweep_clip_to_request(uint32_t hz);
static uint32_t sweep_coarse_step_hz(uint32_t step_hz);
static uint32_t sweep_candidate_mid_freq_hz(uint16_t start_step, uint16_t stop_step);
static uint32_t sweep_candidate_mid_grid_freq_hz(uint16_t start_step, uint16_t stop_step);
static uint8_t sweep_freq_in_candidate(uint32_t freq_hz, uint16_t start_step, uint16_t stop_step);
static uint32_t sweep_candidate_convex_freq_hz(uint16_t start_step,
                                               uint16_t stop_step,
                                               uint16_t peak_step,
                                               uint16_t width_steps);
static void sweep_set_dds_amp_if_needed(void);
static void sweep_set_dds(uint32_t freq_hz);
static void sweep_start_stage(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz, app_sweep_stage_t stage);
static void sweep_start_coarse(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz);
static void sweep_repeat_coarse(void);
static void sweep_start_calibration(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz);
#if (APP_SWEEP_FINE_STAGE_ENABLE != 0U)
static void sweep_start_fine(uint32_t center_hz);
#endif
static void sweep_measure_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt, app_sweep_measure_t *measure);
static uint32_t sweep_eval_stage(uint8_t *found_out);
static uint8_t sweep_build_candidate(uint16_t start_step, uint16_t stop_step, app_sweep_candidate_t *candidate);
static void sweep_insert_log_candidate(app_sweep_candidate_t *log_candidates,
                                       uint16_t *log_count,
                                       const app_sweep_candidate_t *candidate);
static uint8_t sweep_is_blocked_freq(uint32_t freq_hz, uint32_t tol_hz, uint32_t *block_hz_out);
static uint8_t sweep_candidate_better(const app_sweep_candidate_t *a, const app_sweep_candidate_t *b);
static uint8_t sweep_candidate_has_energy(const app_sweep_candidate_t *candidate);
static uint8_t sweep_candidate_two_peak_valley(uint16_t start_step,
                                               uint16_t stop_step,
                                               uint16_t *second_peak_step_out,
                                               uint16_t *valley_step_out,
                                               uint32_t *valley_vpp_out);
static uint8_t sweep_candidate_is_concave(uint16_t peak_step,
                                          uint16_t second_peak_step,
                                          uint16_t valley_step,
                                          uint32_t peak_vpp,
                                          uint32_t second_peak_vpp,
                                          uint32_t valley_vpp);
static uint32_t sweep_candidate_tol_hz(void);
static uint8_t sweep_step_ignored_for_threshold(uint16_t step_index);
static uint32_t sweep_abs_diff_u32(uint32_t a, uint32_t b);
static void sweep_finish_no_center(void);
static uint8_t sweep_try_retry(void);
static uint8_t sweep_calibration_matches_current_stage(void);
static uint8_t sweep_calibration_step_enabled(uint16_t step_index);
static uint32_t sweep_apply_calibration(uint16_t step_index, uint32_t raw_vpp);
static void sweep_update_dwell_clip(const app_sweep_measure_t *measure);
static void sweep_clear_dwell_accumulator(void);
static void sweep_save_current_step(uint32_t avg_vpp);
static void sweep_save_calibration_step(uint32_t avg_vpp);
static uint8_t sweep_request_changed(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz);
#if (SWEEP_POINT_LOG_ENABLE != 0U)
static void sweep_log_point(uint32_t freq_hz, uint16_t step_index, uint32_t avg_vpp);
#endif
#if (APP_SWEEP_CLIP_LOG_ENABLE != 0U)
static void sweep_log_clip(uint32_t freq_hz, uint16_t step_index);
#endif
#if (APP_SWEEP_CAL_POINT_LOG_ENABLE != 0U)
static void sweep_log_cal_point(uint32_t freq_hz, uint16_t step_index, uint32_t avg_vpp, uint8_t clip);
#endif
static void sweep_log_cal_summary(void);
static uint32_t sweep_cal_flash_crc(const app_sweep_cal_flash_record_t *record);
static uint8_t sweep_cal_flash_record_valid(const app_sweep_cal_flash_record_t *record);
static void sweep_cal_flash_export(app_sweep_cal_flash_record_t *record);
static void sweep_cal_flash_import(const app_sweep_cal_flash_record_t *record);
#if (APP_SWEEP_CANDIDATE_LOG_ENABLE != 0U)
static void sweep_log_candidates(const app_sweep_candidate_t *candidates,
                                 uint16_t log_count,
                                 uint16_t total_count,
                                 uint16_t drop_count,
                                 uint32_t selected_hz);
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

/* 统一解析粗扫步进，避免宏值和调用入参不一致时反复重启扫频。 */
static uint32_t sweep_coarse_step_hz(uint32_t step_hz)
{
#if (APP_SWEEP_COARSE_STEP_HZ != 0UL)
    (void)step_hz;
    return APP_SWEEP_COARSE_STEP_HZ;
#else
    return (step_hz == 0UL) ? 1UL : step_hz;
#endif
}

/* 扫频使用固定幅度：默认按 CH0 上电幅度的 1.5 倍计算，超过 AD9959 幅度上限就限到最大值。 */
static void sweep_set_dds_amp_if_needed(void)
{
    const AppDdsStatus *st = AppDDS_GetStatus();
    AppDdsCmd cmd;
    uint16_t sweep_amp = (uint16_t)APP_SWEEP_DDS_AMP_CODE;

    if ((st != 0) && (st->amp_code[APP_SWEEP_DDS_CHANNEL] == sweep_amp))
    {
        return;
    }

    cmd = AppDDS_MakeSelectChCmd((uint8_t)APP_SWEEP_DDS_CHANNEL);
    (void)AppDDS_DispatchCmd(&cmd);
    cmd = AppDDS_MakeSetAmpCmd(sweep_amp);
    (void)AppDDS_DispatchCmd(&cmd);
}

static void sweep_set_dds(uint32_t freq_hz)
{
    AppDdsCmd cmd = AppDDS_MakeSetChFreqApplyCmd((uint8_t)APP_SWEEP_DDS_CHANNEL, freq_hz);

    g_sweep.target_freq_hz = freq_hz;
    g_sweep.settle_blocks = APP_SWEEP_SETTLE_BLOCKS;

    sweep_set_dds_amp_if_needed();
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
    sweep_clear_dwell_accumulator();
    g_sweep.peak_vpp = 0U;
    g_sweep.valley_vpp = 0xFFFFFFFFUL;
    g_sweep.peak_step = 0U;
    g_sweep.valley_step = APP_SWEEP_INVALID_STEP;
    g_sweep.active = 1U;

    sweep_set_dds(g_sweep.stage_start_hz);
}

static void sweep_start_coarse(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz)
{
    uint32_t coarse_step_hz = sweep_coarse_step_hz(step_hz);

    memset(&g_sweep, 0, sizeof(g_sweep));
    g_sweep.request_start_hz = start_hz;
    g_sweep.request_stop_hz = stop_hz;
    g_sweep.request_step_hz = coarse_step_hz;
    sweep_start_stage(g_sweep.request_start_hz,
                      g_sweep.request_stop_hz,
                      g_sweep.request_step_hz,
                      APP_SWEEP_STAGE_COARSE);
}

/* 沿用当前请求参数重新启动一次粗扫，并保留失败重试计数。 */
static void sweep_repeat_coarse(void)
{
    uint32_t start_hz = g_sweep.request_start_hz;
    uint32_t stop_hz = g_sweep.request_stop_hz;
    uint32_t step_hz = g_sweep.request_step_hz;
    uint8_t retry_count = (uint8_t)(g_sweep.retry_count + 1U);

    memset(&g_sweep, 0, sizeof(g_sweep));
    g_sweep.request_start_hz = start_hz;
    g_sweep.request_stop_hz = stop_hz;
    g_sweep.request_step_hz = step_hz;
    g_sweep.retry_count = retry_count;
    sweep_start_stage(g_sweep.request_start_hz,
                      g_sweep.request_stop_hz,
                      g_sweep.request_step_hz,
                      APP_SWEEP_STAGE_COARSE);
}

/* 启动一次基线校准粗扫，并清空上一轮 RAM 基线。 */
static void sweep_start_calibration(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz)
{
    uint32_t coarse_step_hz = sweep_coarse_step_hz(step_hz);

    memset(&g_sweep, 0, sizeof(g_sweep));
    memset(&g_sweep_cal, 0, sizeof(g_sweep_cal));

    g_sweep.request_start_hz = start_hz;
    g_sweep.request_stop_hz = stop_hz;
    g_sweep.request_step_hz = coarse_step_hz;
    g_sweep_cal.start_hz = start_hz;
    g_sweep_cal.stop_hz = stop_hz;
    g_sweep_cal.step_hz = coarse_step_hz;
    g_sweep_cal.stats.min_vpp = 0xFFFFFFFFUL;

    sweep_start_stage(g_sweep.request_start_hz,
                      g_sweep.request_stop_hz,
                      g_sweep.request_step_hz,
                      APP_SWEEP_STAGE_COARSE);
    g_sweep_cal.stats.point_count = g_sweep.step_count;
}

#if (APP_SWEEP_FINE_STAGE_ENABLE != 0U)
/* 围绕粗扫选中的候选做细扫，细扫步进来源由宏开关控制。 */
static void sweep_start_fine(uint32_t center_hz)
{
    uint32_t fine_span_hz = APP_SWEEP_FINE_HALF_SPAN_HZ;
#if (APP_SWEEP_FINE_FIXED_STEP_ENABLE != 0U)
    uint32_t fine_step_hz = APP_SWEEP_FINE_STEP_HZ;
#else
    uint32_t fine_step_hz = g_sweep.request_step_hz / 6UL;
#endif
    uint32_t fine_start_hz;
    uint32_t fine_stop_hz;

    if (fine_step_hz == 0UL)
    {
        fine_step_hz = 1UL;
    }
    if (fine_span_hz == 0UL)
    {
        fine_span_hz = g_sweep.request_step_hz * 2UL;
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
#endif

/* 所有候选被过滤或无有效候选时，结束扫频并保持 center_hz 为 0。 */
static void sweep_finish_no_center(void)
{
    g_sweep.center_hz = 0UL;
    g_sweep.stage = APP_SWEEP_STAGE_DONE;
    g_sweep.active = 0U;
    sweep_clear_dwell_accumulator();
    g_sweep.settle_blocks = 0U;
}

/* 无有效候选时最多重复粗扫，超过次数后才进入未锁定完成态。 */
static uint8_t sweep_try_retry(void)
{
    if (g_sweep.retry_count >= APP_SWEEP_MAX_RETRY_COUNT)
    {
        return 0U;
    }

    sweep_repeat_coarse();
    return 1U;
}

/* 判断当前扫频网格是否可直接使用 RAM 校准基线。 */
static uint8_t sweep_calibration_matches_current_stage(void)
{
    return ((g_sweep_cal.stats.valid != 0U) &&
            (g_sweep.stage == APP_SWEEP_STAGE_COARSE) &&
            (g_sweep_cal.start_hz == g_sweep.stage_start_hz) &&
            (g_sweep_cal.stop_hz == g_sweep.stage_stop_hz) &&
            (g_sweep_cal.step_hz == g_sweep.stage_step_hz) &&
            (g_sweep_cal.stats.point_count == g_sweep.step_count)) ? 1U : 0U;
}

/* 判断当前频点是否落入强校准点的固定 Hz 扩展范围内。 */
static uint8_t sweep_calibration_step_enabled(uint16_t step_index)
{
    uint16_t idx;
    uint32_t current_hz;

    if ((sweep_calibration_matches_current_stage() == 0U) ||
        (step_index >= g_sweep_cal.stats.point_count) ||
        (step_index >= APP_SWEEP_MAX_STEPS))
    {
        return 0U;
    }

    current_hz = sweep_freq_at(g_sweep_cal.start_hz, g_sweep_cal.step_hz, step_index);

    for (idx = 0U; idx < g_sweep_cal.stats.point_count; idx++)
    {
        uint32_t cal_hz = sweep_freq_at(g_sweep_cal.start_hz, g_sweep_cal.step_hz, idx);

        if ((g_sweep_cal.baseline_vpp[idx] >= APP_SWEEP_CAL_BASELINE_MIN_VPP) &&
            (sweep_abs_diff_u32(current_hz, cal_hz) <= APP_SWEEP_CAL_BASELINE_SPREAD_HZ))
        {
            return 1U;
        }
    }

    return 0U;
}

/* 将当前原始 Vpp 转换为相对校准基线的差分 Vpp。 */
static uint32_t sweep_apply_calibration(uint16_t step_index, uint32_t raw_vpp)
{
    uint32_t base_vpp;

    if (sweep_calibration_step_enabled(step_index) == 0U)
    {
        return raw_vpp;
    }

    base_vpp = g_sweep_cal.baseline_vpp[step_index];
    return ((raw_vpp > base_vpp) ? (raw_vpp - base_vpp) : 0UL) + g_sweep_cal.baseline_avg_vpp;
}

/* 累计同一频点多块 dwell 的 ADC 近轨信息。 */
static void sweep_update_dwell_clip(const app_sweep_measure_t *measure)
{
    if (measure == 0)
    {
        return;
    }

    if (g_sweep.dwell_count == 0U)
    {
        g_sweep.dwell_i_min = measure->i_min;
        g_sweep.dwell_i_max = measure->i_max;
        g_sweep.dwell_q_min = measure->q_min;
        g_sweep.dwell_q_max = measure->q_max;
    }
    else
    {
        if (measure->i_min < g_sweep.dwell_i_min) { g_sweep.dwell_i_min = measure->i_min; }
        if (measure->i_max > g_sweep.dwell_i_max) { g_sweep.dwell_i_max = measure->i_max; }
        if (measure->q_min < g_sweep.dwell_q_min) { g_sweep.dwell_q_min = measure->q_min; }
        if (measure->q_max > g_sweep.dwell_q_max) { g_sweep.dwell_q_max = measure->q_max; }
    }

    if (measure->clip != 0U)
    {
        g_sweep.dwell_clip_seen = 1U;
    }
}

/* 清除当前频点 dwell 累计值，准备进入下一个频点。 */
static void sweep_clear_dwell_accumulator(void)
{
    g_sweep.dwell_count = 0U;
    g_sweep.dwell_vpp_sum = 0ULL;
    g_sweep.dwell_clip_seen = 0U;
    g_sweep.dwell_i_min = 0xFFFFU;
    g_sweep.dwell_i_max = 0U;
    g_sweep.dwell_q_min = 0xFFFFU;
    g_sweep.dwell_q_max = 0U;
}

/* 计算当前 ADC 块的 I/Q 峰峰值和近轨状态，用于扫频与校准共用。 */
static void sweep_measure_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt, app_sweep_measure_t *measure)
{
    uint32_t idx;
    uint16_t i_min;
    uint16_t i_max;
    uint16_t q_min;
    uint16_t q_max;
    uint32_t i_vpp;
    uint32_t q_vpp;

    if (measure == 0)
    {
        return;
    }

    memset(measure, 0, sizeof(*measure));

    if ((i_buf == 0) || (q_buf == 0) || (sample_cnt == 0U))
    {
        return;
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
    measure->vpp = (i_vpp > q_vpp) ? i_vpp : q_vpp;
    measure->i_min = i_min;
    measure->i_max = i_max;
    measure->q_min = q_min;
    measure->q_max = q_max;
    if ((i_min <= APP_SWEEP_ADC_CLIP_LOW) ||
        (q_min <= APP_SWEEP_ADC_CLIP_LOW) ||
        (i_max >= APP_SWEEP_ADC_CLIP_HIGH) ||
        (q_max >= APP_SWEEP_ADC_CLIP_HIGH))
    {
        measure->clip = 1U;
    }
}

static uint32_t sweep_abs_diff_u32(uint32_t a, uint32_t b)
{
    return (a >= b) ? (a - b) : (b - a);
}

/* 封锁匹配容差取半个当前步进，且不小于最小容差。 */
static uint32_t sweep_candidate_tol_hz(void)
{
    uint32_t tol_hz = g_sweep.stage_step_hz / 2UL;

    if (tol_hz < APP_SWEEP_BLOCK_MATCH_MIN_TOL_HZ)
    {
        tol_hz = APP_SWEEP_BLOCK_MATCH_MIN_TOL_HZ;
    }

    return tol_hz;
}

/* 检查候选频率是否落入封锁表容差范围，并回填命中的封锁频率。 */
/* 阈值估计时跳过封锁频点附近，避免强干扰参与 peak/valley 估计。 */
static uint8_t sweep_step_ignored_for_threshold(uint16_t step_index)
{
    uint32_t freq_hz;
    uint32_t tol_hz = sweep_candidate_tol_hz();
    uint32_t dummy_block_hz;

    if (tol_hz < APP_SWEEP_BLOCK_THRESHOLD_IGNORE_HZ)
    {
        tol_hz = APP_SWEEP_BLOCK_THRESHOLD_IGNORE_HZ;
    }

    freq_hz = sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, step_index);
    return sweep_is_blocked_freq(freq_hz, tol_hz, &dummy_block_hz);
}

static uint8_t sweep_is_blocked_freq(uint32_t freq_hz, uint32_t tol_hz, uint32_t *block_hz_out)
{
    uint32_t idx;

    if (block_hz_out != 0)
    {
        *block_hz_out = 0UL;
    }

    for (idx = 0UL; idx < (sizeof(g_sweep_block_freq_hz) / sizeof(g_sweep_block_freq_hz[0])); idx++)
    {
        uint32_t block_hz = g_sweep_block_freq_hz[idx];

        if (block_hz == 0UL)
        {
            continue;
        }

        if (sweep_abs_diff_u32(freq_hz, block_hz) <= tol_hz)
        {
            if (block_hz_out != 0)
            {
                *block_hz_out = block_hz;
            }
            return 1U;
        }
    }

    return 0U;
}

/* 候选排序规则：优先比较相对底噪的峰值强度，分数相同再比较段宽和峰值。 */
static uint8_t sweep_candidate_better(const app_sweep_candidate_t *a, const app_sweep_candidate_t *b)
{
    if ((a == 0) || (b == 0))
    {
        return 0U;
    }

    if (a->score_vpp != b->score_vpp)
    {
        return (a->score_vpp > b->score_vpp) ? 1U : 0U;
    }

    if (a->width_steps != b->width_steps)
    {
        return (a->width_steps > b->width_steps) ? 1U : 0U;
    }

    return (a->peak_vpp > b->peak_vpp) ? 1U : 0U;
}

/* 过滤低能量候选，门限先保持偏低，避免弱信号被过早误杀。 */
static uint8_t sweep_candidate_has_energy(const app_sweep_candidate_t *candidate)
{
    if (candidate == 0)
    {
        return 0U;
    }

    if ((g_sweep.stage == APP_SWEEP_STAGE_COARSE) &&
        (candidate->shape == APP_SWEEP_CANDIDATE_SHAPE_CONVEX) &&
        (candidate->width_steps > APP_SWEEP_CANDIDATE_MAX_CONVEX_WIDTH_STEPS))
    {
        return 0U;
    }

    return ((candidate->peak_vpp >= APP_SWEEP_CANDIDATE_MIN_PEAK_VPP) &&
            (candidate->score_vpp >= APP_SWEEP_CANDIDATE_MIN_SCORE_VPP)) ? 1U : 0U;
}

/* 平台型 FM 候选使用阈值候选段中点作为中心频率。 */
static uint32_t sweep_candidate_mid_freq_hz(uint16_t start_step, uint16_t stop_step)
{
    uint32_t start_hz = sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, start_step);
    uint32_t stop_hz = sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, stop_step);

    return (start_hz / 2UL) + (stop_hz / 2UL) + ((start_hz & 1UL) & (stop_hz & 1UL));
}

/* 宽凸峰选点保持在粗扫网格上；偶数宽度半步时向右取整，避免锁到左肩。 */
static uint32_t sweep_candidate_mid_grid_freq_hz(uint16_t start_step, uint16_t stop_step)
{
    uint16_t mid_step = (uint16_t)(start_step + (((uint16_t)(stop_step - start_step)) + 1U) / 2U);

    return sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, mid_step);
}

/* 判断某个频率是否落在候选段覆盖的频率范围内，允许半个 step 的边界余量。 */
static uint8_t sweep_freq_in_candidate(uint32_t freq_hz, uint16_t start_step, uint16_t stop_step)
{
    uint32_t start_hz = sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, start_step);
    uint32_t stop_hz = sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, stop_step);
    uint32_t guard_hz = g_sweep.stage_step_hz / 2UL;

    if (freq_hz < start_hz)
    {
        return ((start_hz - freq_hz) <= guard_hz) ? 1U : 0U;
    }

    if (freq_hz > stop_hz)
    {
        return ((freq_hz - stop_hz) <= guard_hz) ? 1U : 0U;
    }

    return 1U;
}

/* 凸峰候选中心估计：粗扫宽凸峰取段中点，细扫宽凸峰优先保持粗扫中心。 */
static uint32_t sweep_candidate_convex_freq_hz(uint16_t start_step,
                                               uint16_t stop_step,
                                               uint16_t peak_step,
                                               uint16_t width_steps)
{
    if ((g_sweep.stage == APP_SWEEP_STAGE_FINE) &&
        (width_steps >= APP_SWEEP_FINE_HOLD_COARSE_MIN_WIDTH_STEPS) &&
        (g_sweep.coarse_center_hz != 0UL) &&
        (sweep_freq_in_candidate(g_sweep.coarse_center_hz, start_step, stop_step) != 0U))
    {
        return g_sweep.coarse_center_hz;
    }

    if ((g_sweep.stage == APP_SWEEP_STAGE_COARSE) &&
        (width_steps >= APP_SWEEP_CANDIDATE_WIDE_CONVEX_MID_STEPS))
    {
        return sweep_candidate_mid_grid_freq_hz(start_step, stop_step);
    }

    if ((g_sweep.stage == APP_SWEEP_STAGE_FINE) &&
        (width_steps >= APP_SWEEP_FINE_HOLD_COARSE_MIN_WIDTH_STEPS))
    {
        return sweep_candidate_mid_grid_freq_hz(start_step, stop_step);
    }

    return sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, peak_step);
}

/* 先找段内主肩峰，再在两侧找可分离次肩峰；谷点只允许落在两个肩峰之间。 */
static uint8_t sweep_candidate_two_peak_valley(uint16_t start_step,
                                               uint16_t stop_step,
                                               uint16_t *second_peak_step_out,
                                               uint16_t *valley_step_out,
                                               uint32_t *valley_vpp_out)
{
    uint32_t center2;
    uint16_t primary_peak_step;
    uint32_t primary_peak_vpp;
    uint16_t valley_step;
    uint32_t valley_vpp;
    uint16_t left_peak_step;
    uint16_t right_peak_step;
    uint32_t left_peak_vpp;
    uint32_t right_peak_vpp;
    uint16_t left_valley_step;
    uint16_t right_valley_step;
    uint32_t left_valley_vpp;
    uint32_t right_valley_vpp;
    uint32_t left_dip_vpp;
    uint32_t right_dip_vpp;
    uint32_t low_shoulder_vpp;
    uint16_t second_peak_step;
    uint32_t second_peak_vpp;
    uint16_t idx;
    uint8_t have_left_peak = 0U;
    uint8_t have_right_peak = 0U;
    uint8_t have_left_valley = 0U;
    uint8_t have_right_valley = 0U;

    if ((second_peak_step_out == 0) ||
        (valley_step_out == 0) ||
        (valley_vpp_out == 0) ||
        (start_step > stop_step))
    {
        return 0U;
    }

    if (start_step == stop_step)
    {
        *second_peak_step_out = start_step;
        *valley_step_out = start_step;
        *valley_vpp_out = g_sweep.vpp_table[start_step];
        return 1U;
    }

    primary_peak_step = start_step;
    primary_peak_vpp = g_sweep.vpp_table[start_step];

    for (idx = (uint16_t)(start_step + 1U); idx <= stop_step; idx++)
    {
        if (g_sweep.vpp_table[idx] > primary_peak_vpp)
        {
            primary_peak_vpp = g_sweep.vpp_table[idx];
            primary_peak_step = idx;
        }
    }

    left_peak_step = start_step;
    left_peak_vpp = 0UL;
    if (primary_peak_step > start_step)
    {
        for (idx = start_step; idx < primary_peak_step; idx++)
        {
            if (((uint32_t)primary_peak_step - (uint32_t)idx) < APP_SWEEP_CANDIDATE_MIN_PEAK_GAP_STEPS)
            {
                continue;
            }

            if ((have_left_peak == 0U) || (g_sweep.vpp_table[idx] > left_peak_vpp))
            {
                left_peak_step = idx;
                left_peak_vpp = g_sweep.vpp_table[idx];
                have_left_peak = 1U;
            }
        }
    }

    right_peak_step = stop_step;
    right_peak_vpp = 0UL;
    if (primary_peak_step < stop_step)
    {
        for (idx = (uint16_t)(primary_peak_step + 1U); idx <= stop_step; idx++)
        {
            if (((uint32_t)idx - (uint32_t)primary_peak_step) < APP_SWEEP_CANDIDATE_MIN_PEAK_GAP_STEPS)
            {
                continue;
            }

            if ((have_right_peak == 0U) || (g_sweep.vpp_table[idx] > right_peak_vpp))
            {
                right_peak_step = idx;
                right_peak_vpp = g_sweep.vpp_table[idx];
                have_right_peak = 1U;
            }
        }
    }

    if (have_left_peak != 0U)
    {
        center2 = (uint32_t)left_peak_step + (uint32_t)primary_peak_step;
        left_valley_step = (uint16_t)(left_peak_step + 1U);
        left_valley_vpp = g_sweep.vpp_table[left_valley_step];

        for (idx = (uint16_t)(left_peak_step + 2U); idx < primary_peak_step; idx++)
        {
            uint32_t idx_center_diff = sweep_abs_diff_u32((uint32_t)idx * 2UL, center2);
            uint32_t valley_center_diff = sweep_abs_diff_u32((uint32_t)left_valley_step * 2UL, center2);

            if ((g_sweep.vpp_table[idx] < left_valley_vpp) ||
                ((g_sweep.vpp_table[idx] == left_valley_vpp) && (idx_center_diff < valley_center_diff)))
            {
                left_valley_vpp = g_sweep.vpp_table[idx];
                left_valley_step = idx;
            }
        }

        low_shoulder_vpp = (primary_peak_vpp < left_peak_vpp) ? primary_peak_vpp : left_peak_vpp;
        left_dip_vpp = (low_shoulder_vpp > left_valley_vpp) ? (low_shoulder_vpp - left_valley_vpp) : 0UL;
        have_left_valley = 1U;
    }

    if (have_right_peak != 0U)
    {
        center2 = (uint32_t)primary_peak_step + (uint32_t)right_peak_step;
        right_valley_step = (uint16_t)(primary_peak_step + 1U);
        right_valley_vpp = g_sweep.vpp_table[right_valley_step];

        for (idx = (uint16_t)(primary_peak_step + 2U); idx < right_peak_step; idx++)
        {
            uint32_t idx_center_diff = sweep_abs_diff_u32((uint32_t)idx * 2UL, center2);
            uint32_t valley_center_diff = sweep_abs_diff_u32((uint32_t)right_valley_step * 2UL, center2);

            if ((g_sweep.vpp_table[idx] < right_valley_vpp) ||
                ((g_sweep.vpp_table[idx] == right_valley_vpp) && (idx_center_diff < valley_center_diff)))
            {
                right_valley_vpp = g_sweep.vpp_table[idx];
                right_valley_step = idx;
            }
        }

        low_shoulder_vpp = (primary_peak_vpp < right_peak_vpp) ? primary_peak_vpp : right_peak_vpp;
        right_dip_vpp = (low_shoulder_vpp > right_valley_vpp) ? (low_shoulder_vpp - right_valley_vpp) : 0UL;
        have_right_valley = 1U;
    }

    if ((have_left_valley != 0U) &&
        ((have_right_valley == 0U) ||
         (left_dip_vpp > right_dip_vpp) ||
         ((left_dip_vpp == right_dip_vpp) && (left_peak_vpp >= right_peak_vpp))))
    {
        second_peak_step = left_peak_step;
        valley_step = left_valley_step;
        valley_vpp = left_valley_vpp;
    }
    else if (have_right_valley != 0U)
    {
        second_peak_step = right_peak_step;
        valley_step = right_valley_step;
        valley_vpp = right_valley_vpp;
    }
    else
    {
        second_peak_step = start_step;
        second_peak_vpp = 0UL;

        for (idx = start_step; idx <= stop_step; idx++)
        {
            if ((idx != primary_peak_step) && (g_sweep.vpp_table[idx] > second_peak_vpp))
            {
                second_peak_step = idx;
                second_peak_vpp = g_sweep.vpp_table[idx];
            }
        }

        valley_step = primary_peak_step;
        valley_vpp = primary_peak_vpp;
    }

    *second_peak_step_out = second_peak_step;
    *valley_step_out = valley_step;
    *valley_vpp_out = valley_vpp;
    return 1U;
}

/* 判断非平台候选是否是真正的双肩凹谷：两峰要分开，谷点要在两峰之间且足够低。 */
static uint8_t sweep_candidate_is_concave(uint16_t peak_step,
                                          uint16_t second_peak_step,
                                          uint16_t valley_step,
                                          uint32_t peak_vpp,
                                          uint32_t second_peak_vpp,
                                          uint32_t valley_vpp)
{
    uint16_t left_peak;
    uint16_t right_peak;
    uint32_t low_shoulder_vpp;
    uint32_t dip_vpp;

    if (peak_step < second_peak_step)
    {
        left_peak = peak_step;
        right_peak = second_peak_step;
    }
    else
    {
        left_peak = second_peak_step;
        right_peak = peak_step;
    }

    if (((uint32_t)right_peak - (uint32_t)left_peak) < APP_SWEEP_CANDIDATE_MIN_PEAK_GAP_STEPS)
    {
        return 0U;
    }

    if ((valley_step <= left_peak) || (valley_step >= right_peak))
    {
        return 0U;
    }

    low_shoulder_vpp = (peak_vpp < second_peak_vpp) ? peak_vpp : second_peak_vpp;
    dip_vpp = (low_shoulder_vpp > valley_vpp) ? (low_shoulder_vpp - valley_vpp) : 0UL;

    return (dip_vpp >= APP_SWEEP_CANDIDATE_CONCAVE_DIP_RAW) ? 1U : 0U;
}

/* 将候选区间转换为候选：平台取段中点，凹谷取双肩低点，凸峰按阶段估计中心。 */
static uint8_t sweep_build_candidate(uint16_t start_step, uint16_t stop_step, app_sweep_candidate_t *candidate)
{
    uint16_t idx;
    uint16_t peak_step;
    uint16_t second_peak_step;
    uint16_t valley_step;
    uint16_t segment_min_step;
    uint32_t peak_vpp;
    uint32_t second_peak_vpp;
    uint32_t valley_vpp;
    uint32_t local_drop_vpp;
    uint32_t segment_min_vpp;
    uint16_t width_steps;
    app_sweep_candidate_shape_t shape;

    if ((candidate == 0) || (start_step > stop_step) || (stop_step >= g_sweep.step_count))
    {
        return 0U;
    }

    peak_step = start_step;
    peak_vpp = g_sweep.vpp_table[start_step];
    segment_min_step = start_step;
    segment_min_vpp = g_sweep.vpp_table[start_step];

    for (idx = (uint16_t)(start_step + 1U); idx <= stop_step; idx++)
    {
        if (g_sweep.vpp_table[idx] > peak_vpp)
        {
            peak_vpp = g_sweep.vpp_table[idx];
            peak_step = idx;
        }

        if (g_sweep.vpp_table[idx] < segment_min_vpp)
        {
            segment_min_vpp = g_sweep.vpp_table[idx];
            segment_min_step = idx;
        }
    }

    if (sweep_candidate_two_peak_valley(start_step,
                                        stop_step,
                                        &second_peak_step,
                                        &valley_step,
                                        &valley_vpp) == 0U)
    {
        return 0U;
    }

    second_peak_vpp = g_sweep.vpp_table[second_peak_step];
    local_drop_vpp = (peak_vpp > segment_min_vpp) ? (peak_vpp - segment_min_vpp) : 0UL;
    width_steps = (uint16_t)(stop_step - start_step + 1U);

    memset(candidate, 0, sizeof(*candidate));
    if (local_drop_vpp <= APP_SWEEP_CANDIDATE_PLATEAU_DROP_RAW)
    {
        shape = APP_SWEEP_CANDIDATE_SHAPE_PLATEAU;
        candidate->freq_hz = sweep_candidate_mid_freq_hz(start_step, stop_step);
        valley_step = segment_min_step;
        valley_vpp = segment_min_vpp;
    }
    else if (sweep_candidate_is_concave(peak_step,
                                        second_peak_step,
                                        valley_step,
                                        peak_vpp,
                                        second_peak_vpp,
                                        valley_vpp) != 0U)
    {
        shape = APP_SWEEP_CANDIDATE_SHAPE_CONCAVE;
        candidate->freq_hz = sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, valley_step);
    }
    else
    {
        shape = APP_SWEEP_CANDIDATE_SHAPE_CONVEX;
        candidate->freq_hz = sweep_candidate_convex_freq_hz(start_step, stop_step, peak_step, width_steps);
    }
    candidate->peak_vpp = peak_vpp;
    candidate->valley_vpp = valley_vpp;
    candidate->score_vpp = (peak_vpp > g_sweep.valley_vpp) ? (peak_vpp - g_sweep.valley_vpp) : 0UL;
    candidate->start_step = start_step;
    candidate->stop_step = stop_step;
    candidate->peak_step = peak_step;
    candidate->second_peak_step = second_peak_step;
    candidate->valley_step = valley_step;
    candidate->width_steps = width_steps;
    candidate->shape = shape;
    candidate->blocked = sweep_is_blocked_freq(candidate->freq_hz,
                                               sweep_candidate_tol_hz(),
                                               &candidate->block_hz);
    return 1U;
}

/* 维护按候选强度排序的日志缓存，只保留前 APP_SWEEP_MAX_CANDIDATES 个。 */
static void sweep_insert_log_candidate(app_sweep_candidate_t *log_candidates,
                                       uint16_t *log_count,
                                       const app_sweep_candidate_t *candidate)
{
    uint16_t pos;

    if ((log_candidates == 0) || (log_count == 0) || (candidate == 0))
    {
        return;
    }

    if (*log_count < APP_SWEEP_MAX_CANDIDATES)
    {
        pos = *log_count;
        (*log_count)++;
    }
    else
    {
        pos = (uint16_t)(APP_SWEEP_MAX_CANDIDATES - 1U);
        if (sweep_candidate_better(candidate, &log_candidates[pos]) == 0U)
        {
            return;
        }
    }

    while ((pos > 0U) && (sweep_candidate_better(candidate, &log_candidates[pos - 1U]) != 0U))
    {
        log_candidates[pos] = log_candidates[pos - 1U];
        pos--;
    }

    log_candidates[pos] = *candidate;
}

#if (APP_SWEEP_CANDIDATE_LOG_ENABLE != 0U)
/* 输出候选摘要和候选明细，保持短行便于串口解析。 */
static void sweep_log_candidates(const app_sweep_candidate_t *candidates,
                                 uint16_t log_count,
                                 uint16_t total_count,
                                 uint16_t drop_count,
                                 uint32_t selected_hz)
{
#if (APP_PRINT_LOG_ENABLE != 0U)
    char line[126];
    int n;
    uint16_t idx;

    n = snprintf(line,
                 sizeof(line),
                 "sweep:summary,%u,%u,%u,%lu\r\n",
                 (unsigned int)g_sweep.stage,
                 (unsigned int)total_count,
                 (unsigned int)drop_count,
                 (unsigned long)selected_hz);
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
        print_queue_send(line);
    }

    for (idx = 0U; idx < log_count; idx++)
    {
        n = snprintf(line,
                     sizeof(line),
                     "sweep:cand,%u,%u,%lu,%lu,%u,%u,%lu,%lu\r\n",
                     (unsigned int)g_sweep.stage,
                     (unsigned int)(idx + 1U),
                     (unsigned long)candidates[idx].freq_hz,
                     (unsigned long)candidates[idx].peak_vpp,
                     (unsigned int)candidates[idx].width_steps,
                     (unsigned int)candidates[idx].blocked,
                     (unsigned long)candidates[idx].block_hz,
                     (unsigned long)candidates[idx].score_vpp);
        if ((n > 0) && ((size_t)n < sizeof(line)))
        {
            print_queue_send(line);
        }

#if (APP_SWEEP_CANDIDATE_DEBUG_LOG_ENABLE != 0U)
        n = snprintf(line,
                     sizeof(line),
                     "sweep:cdbg,%u,%u,%u,%u,%u,%u,%u,%lu,%u\r\n",
                     (unsigned int)g_sweep.stage,
                     (unsigned int)(idx + 1U),
                     (unsigned int)candidates[idx].start_step,
                     (unsigned int)candidates[idx].stop_step,
                     (unsigned int)candidates[idx].peak_step,
                     (unsigned int)candidates[idx].second_peak_step,
                     (unsigned int)candidates[idx].valley_step,
                     (unsigned long)candidates[idx].valley_vpp,
                     (unsigned int)candidates[idx].shape);
        if ((n > 0) && ((size_t)n < sizeof(line)))
        {
            print_queue_send(line);
        }
#endif
    }
#else
    (void)candidates;
    (void)log_count;
    (void)total_count;
    (void)drop_count;
    (void)selected_hz;
#endif
}
#endif

/* 完整评估当前阶段：拆分候选区间、过滤封锁频率并选择最优候选。 */
static uint32_t sweep_eval_stage(uint8_t *found_out)
{
    app_sweep_candidate_t candidate;
    app_sweep_candidate_t selected;
    app_sweep_candidate_t log_candidates[APP_SWEEP_MAX_CANDIDATES];
    uint32_t rise_vpp;
    uint32_t threshold_vpp;
    uint32_t eval_peak_vpp;
    uint32_t eval_valley_vpp;
    uint8_t eval_valid = 0U;
    uint16_t idx;
    uint16_t log_count = 0U;
    uint16_t total_count = 0U;
    uint16_t drop_count = 0U;
    uint8_t have_selected = 0U;

    memset(&selected, 0, sizeof(selected));
    memset(log_candidates, 0, sizeof(log_candidates));

    if (found_out != 0)
    {
        *found_out = 0U;
    }

    if (g_sweep.valley_step == APP_SWEEP_INVALID_STEP)
    {
#if (APP_SWEEP_CANDIDATE_LOG_ENABLE != 0U)
        sweep_log_candidates(log_candidates, log_count, total_count, drop_count, 0UL);
#endif
        return 0UL;
    }

    eval_peak_vpp = 0UL;
    eval_valley_vpp = 0xFFFFFFFFUL;
    for (idx = 0U; idx < g_sweep.step_count; idx++)
    {
        if (sweep_step_ignored_for_threshold(idx) != 0U)
        {
            continue;
        }

        if (g_sweep.vpp_table[idx] > eval_peak_vpp)
        {
            eval_peak_vpp = g_sweep.vpp_table[idx];
        }

        if (g_sweep.vpp_table[idx] < eval_valley_vpp)
        {
            eval_valley_vpp = g_sweep.vpp_table[idx];
        }

        eval_valid = 1U;
    }

    if (eval_valid == 0U)
    {
        eval_peak_vpp = g_sweep.peak_vpp;
        eval_valley_vpp = g_sweep.valley_vpp;
    }

    rise_vpp = eval_peak_vpp - eval_valley_vpp;
    if (rise_vpp >= APP_SWEEP_MIN_RISE_RAW)
    {
        threshold_vpp = eval_valley_vpp + (rise_vpp >> APP_SWEEP_THRESHOLD_SHIFT);
        if ((threshold_vpp - eval_valley_vpp) < APP_SWEEP_MIN_RISE_RAW)
        {
            threshold_vpp = eval_valley_vpp + APP_SWEEP_MIN_RISE_RAW;
        }

        idx = 0U;
        while (idx < g_sweep.step_count)
        {
            if (g_sweep.vpp_table[idx] >= threshold_vpp)
            {
                uint16_t start_step = idx;
                uint16_t stop_step = idx;
                uint8_t span_done = 0U;

                while (span_done == 0U)
                {
                    while ((idx < g_sweep.step_count) && (g_sweep.vpp_table[idx] >= threshold_vpp))
                    {
                        stop_step = idx;
                        idx++;
                    }

                    if ((APP_SWEEP_CANDIDATE_BRIDGE_GAP_STEPS == 0U) || (idx >= g_sweep.step_count))
                    {
                        span_done = 1U;
                    }
                    else
                    {
                        uint16_t gap_count = 0U;

                        while ((idx < g_sweep.step_count) &&
                               (g_sweep.vpp_table[idx] < threshold_vpp) &&
                               (gap_count < APP_SWEEP_CANDIDATE_BRIDGE_GAP_STEPS))
                        {
                            idx++;
                            gap_count++;
                        }

                        if ((gap_count == 0U) ||
                            (idx >= g_sweep.step_count) ||
                            (g_sweep.vpp_table[idx] < threshold_vpp))
                        {
                            span_done = 1U;
                        }
                    }
                }

                if (sweep_build_candidate(start_step, stop_step, &candidate) != 0U)
                {
                    if (sweep_candidate_has_energy(&candidate) == 0U)
                    {
                        continue;
                    }

                    total_count++;
                    if (candidate.blocked != 0U)
                    {
                        drop_count++;
                    }
                    else if ((have_selected == 0U) || (sweep_candidate_better(&candidate, &selected) != 0U))
                    {
                        selected = candidate;
                        have_selected = 1U;
                    }
                    sweep_insert_log_candidate(log_candidates, &log_count, &candidate);
                }
            }
            else
            {
                idx++;
            }
        }
    }

#if (APP_SWEEP_CANDIDATE_LOG_ENABLE != 0U)
    sweep_log_candidates(log_candidates,
                         log_count,
                         total_count,
                         drop_count,
                         (have_selected != 0U) ? selected.freq_hz : 0UL);
#endif

    if (have_selected != 0U)
    {
        if (found_out != 0)
        {
            *found_out = 1U;
        }
        return selected.freq_hz;
    }

    return 0UL;
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

#if (APP_SWEEP_CLIP_LOG_ENABLE != 0U)
/* 输出当前频点的 ADC 近轨明细，默认关闭以避免串口过载。 */
static void sweep_log_clip(uint32_t freq_hz, uint16_t step_index)
{
#if (APP_PRINT_LOG_ENABLE != 0U)
    char line[128];
    int n;

    if (g_sweep.dwell_clip_seen == 0U)
    {
        return;
    }

    n = snprintf(line,
                 sizeof(line),
                 "sweep:clip,%u,%lu,%u,%u,%u,%u,%u\r\n",
                 (unsigned int)g_sweep.stage,
                 (unsigned long)freq_hz,
                 (unsigned int)step_index,
                 (unsigned int)g_sweep.dwell_i_min,
                 (unsigned int)g_sweep.dwell_i_max,
                 (unsigned int)g_sweep.dwell_q_min,
                 (unsigned int)g_sweep.dwell_q_max);
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
        print_queue_send(line);
    }
#else
    (void)freq_hz;
    (void)step_index;
#endif
}
#endif

#if (APP_SWEEP_CAL_POINT_LOG_ENABLE != 0U)
/* 输出校准模式逐点基线，频率按 MHz 打印，便于 VOFA 直接画图。 */
static void sweep_log_cal_point(uint32_t freq_hz, uint16_t step_index, uint32_t avg_vpp, uint8_t clip)
{
#if (APP_PRINT_LOG_ENABLE != 0U)
    char line[96];
    int n;

    n = snprintf(line,
                 sizeof(line),
                 "%lu.%03lu,%lu,%u,%u\r\n",
                 (unsigned long)(freq_hz / 1000000UL),
                 (unsigned long)((freq_hz % 1000000UL) / 1000UL),
                 (unsigned long)avg_vpp,
                 (unsigned int)step_index,
                 (unsigned int)clip);
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
        print_queue_send(line);
    }
#else
    (void)freq_hz;
    (void)step_index;
    (void)avg_vpp;
    (void)clip;
#endif
}
#endif

/* 输出校准完成摘要，给 UI 和串口工具确认 RAM 基线有效性。 */
static void sweep_log_cal_summary(void)
{
#if (APP_PRINT_LOG_ENABLE != 0U)
    char line[112];
    int n;

    n = snprintf(line,
                 sizeof(line),
                 "cal:summary,%u,%lu,%lu,%lu,%u\r\n",
                 (unsigned int)g_sweep_cal.stats.point_count,
                 (unsigned long)g_sweep_cal.stats.clip_count,
                 (unsigned long)g_sweep_cal.stats.max_vpp,
                 (unsigned long)g_sweep_cal.stats.min_vpp,
                 (unsigned int)g_sweep_cal.stats.valid);
    if ((n > 0) && ((size_t)n < sizeof(line)))
    {
        print_queue_send(line);
    }
#endif
}

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

/* 保存一个校准频点的 RAM 基线和 clip 标志。 */
static void sweep_save_calibration_step(uint32_t avg_vpp)
{
    uint16_t idx = g_sweep.step_index;

    if (idx < APP_SWEEP_MAX_STEPS)
    {
        g_sweep_cal.baseline_vpp[idx] = avg_vpp;
        g_sweep_cal.clip_flag[idx] = g_sweep.dwell_clip_seen;
        g_sweep_cal.baseline_sum_vpp += avg_vpp;
    }

    if (avg_vpp > g_sweep_cal.stats.max_vpp)
    {
        g_sweep_cal.stats.max_vpp = avg_vpp;
    }

    if (avg_vpp < g_sweep_cal.stats.min_vpp)
    {
        g_sweep_cal.stats.min_vpp = avg_vpp;
    }

    if (g_sweep.dwell_clip_seen != 0U)
    {
        g_sweep_cal.stats.clip_count++;
    }
}

static uint8_t sweep_request_changed(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz)
{
    uint32_t fixed_step_hz = sweep_coarse_step_hz(step_hz);

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
    app_sweep_measure_t measure;
    uint32_t avg_vpp;
    uint32_t raw_avg_vpp;
    uint32_t point_hz;
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

    sweep_measure_block(i_buf, q_buf, sample_cnt, &measure);
    sweep_update_dwell_clip(&measure);
    g_sweep.dwell_vpp_sum += measure.vpp;
    g_sweep.dwell_count++;

    if (g_sweep.dwell_count < APP_SWEEP_DWELL_BLOCKS_PER_STEP)
    {
        return 0U;
    }

    raw_avg_vpp = (uint32_t)(g_sweep.dwell_vpp_sum / (uint64_t)g_sweep.dwell_count);
    avg_vpp = sweep_apply_calibration(g_sweep.step_index, raw_avg_vpp);
    sweep_save_current_step(avg_vpp);
    point_hz = sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, g_sweep.step_index);
#if (SWEEP_POINT_LOG_ENABLE != 0U)
    sweep_log_point(point_hz, g_sweep.step_index, avg_vpp);
#endif
#if (APP_SWEEP_CLIP_LOG_ENABLE != 0U)
    sweep_log_clip(point_hz, g_sweep.step_index);
#endif
    sweep_clear_dwell_accumulator();

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
#if (APP_SWEEP_FINE_STAGE_ENABLE != 0U)
        sweep_start_fine(g_sweep.coarse_center_hz);
        return 0U;
#else
        g_sweep.center_hz = g_sweep.coarse_center_hz;
        g_sweep.stage = APP_SWEEP_STAGE_DONE;
        g_sweep.active = 0U;
        sweep_set_dds(g_sweep.center_hz + APP_SWEEP_FINAL_LO_OFFSET_HZ);
        return g_sweep.center_hz;
#endif
    }

    if (found == 0U)
    {
#if (APP_SWEEP_FINE_FALLBACK_TO_COARSE_ENABLE != 0U)
        if ((g_sweep.stage == APP_SWEEP_STAGE_FINE) && (g_sweep.coarse_center_hz != 0UL))
        {
            g_sweep.center_hz = g_sweep.coarse_center_hz;
            g_sweep.stage = APP_SWEEP_STAGE_DONE;
            g_sweep.active = 0U;
            sweep_set_dds(g_sweep.center_hz + APP_SWEEP_FINAL_LO_OFFSET_HZ);
            return g_sweep.center_hz;
        }
#endif

        if (sweep_try_retry() != 0U)
        {
            return 0U;
        }

        sweep_finish_no_center();
        return 0U;
    }

    g_sweep.center_hz = sweep_clip_to_request(estimate_hz);
    g_sweep.stage = APP_SWEEP_STAGE_DONE;
    g_sweep.active = 0U;
    sweep_set_dds(g_sweep.center_hz + APP_SWEEP_FINAL_LO_OFFSET_HZ);
    return g_sweep.center_hz;
}

/* 执行一次仅粗扫的环境基线校准，完成后返回 1。 */
uint8_t app_sweep_calibrate_baseline(uint32_t start_hz,
                                     uint32_t stop_hz,
                                     uint32_t step_hz,
                                     const uint16_t *i_buf,
                                     const uint16_t *q_buf,
                                     uint32_t sample_cnt)
{
    app_sweep_measure_t measure;
    uint32_t avg_vpp;
    uint32_t point_hz;

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

        sweep_start_calibration(start_hz, stop_hz, step_hz);
        return 0U;
    }

    if ((g_sweep.stage == APP_SWEEP_STAGE_DONE) &&
        (g_sweep_cal.stats.valid != 0U) &&
        (g_sweep_cal.start_hz == start_hz) &&
        (g_sweep_cal.stop_hz == stop_hz) &&
        (g_sweep_cal.step_hz == sweep_coarse_step_hz(step_hz)))
    {
        return 1U;
    }

    if (sweep_request_changed(start_hz, stop_hz, step_hz) != 0U)
    {
        sweep_start_calibration(start_hz, stop_hz, step_hz);
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

    sweep_measure_block(i_buf, q_buf, sample_cnt, &measure);
    sweep_update_dwell_clip(&measure);
    g_sweep.dwell_vpp_sum += measure.vpp;
    g_sweep.dwell_count++;

    if (g_sweep.dwell_count < APP_SWEEP_DWELL_BLOCKS_PER_STEP)
    {
        return 0U;
    }

    avg_vpp = (uint32_t)(g_sweep.dwell_vpp_sum / (uint64_t)g_sweep.dwell_count);
    sweep_save_calibration_step(avg_vpp);
    point_hz = sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, g_sweep.step_index);
#if (APP_SWEEP_CAL_POINT_LOG_ENABLE != 0U)
    sweep_log_cal_point(point_hz, g_sweep.step_index, avg_vpp, g_sweep.dwell_clip_seen);
#endif
    sweep_clear_dwell_accumulator();

    if ((g_sweep.step_index + 1U) < g_sweep.step_count)
    {
        g_sweep.step_index++;
        sweep_set_dds(sweep_freq_at(g_sweep.stage_start_hz, g_sweep.stage_step_hz, g_sweep.step_index));
        return 0U;
    }

    if (g_sweep_cal.stats.point_count != 0U)
    {
        g_sweep_cal.baseline_avg_vpp =
            (uint32_t)(g_sweep_cal.baseline_sum_vpp / (uint64_t)g_sweep_cal.stats.point_count);
    }
    g_sweep_cal.stats.valid = 1U;
    g_sweep.stage = APP_SWEEP_STAGE_DONE;
    g_sweep.active = 0U;
    g_sweep.settle_blocks = 0U;
    sweep_log_cal_summary();
    return 1U;
}

/* 读取当前 RAM 校准状态，供任务层和 UI 展示使用。 */
void app_sweep_get_calibration_stats(app_sweep_calibration_stats_t *stats_out)
{
    if (stats_out == 0)
    {
        return;
    }

    *stats_out = g_sweep_cal.stats;
}

/* 计算校准记录校验值，只覆盖 crc 字段之前的数据。 */
static uint32_t sweep_cal_flash_crc(const app_sweep_cal_flash_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t crc = APP_SWEEP_CAL_FLASH_CRC_SEED;
    uint32_t idx;

    if (record == 0)
    {
        return 0UL;
    }

    for (idx = 0UL; idx < (sizeof(app_sweep_cal_flash_record_t) - sizeof(uint32_t)); idx++)
    {
        crc ^= (uint32_t)bytes[idx];
        crc *= 16777619UL;
    }

    return crc;
}

/* 检查 Flash 中读出的校准记录是否属于当前固件和当前结构。 */
static uint8_t sweep_cal_flash_record_valid(const app_sweep_cal_flash_record_t *record)
{
    if (record == 0)
    {
        return 0U;
    }

    if ((record->magic != APP_SWEEP_CAL_FLASH_MAGIC) ||
        (record->version != APP_SWEEP_CAL_FLASH_VERSION) ||
        (record->record_size != sizeof(app_sweep_cal_flash_record_t)) ||
        (record->point_count == 0UL) ||
        (record->point_count > APP_SWEEP_MAX_STEPS) ||
        (record->step_hz == 0UL))
    {
        return 0U;
    }

    return (record->crc == sweep_cal_flash_crc(record)) ? 1U : 0U;
}

/* 将当前 RAM 校准基线打包成可直接写入 Flash 的固定记录。 */
static void sweep_cal_flash_export(app_sweep_cal_flash_record_t *record)
{
    if (record == 0)
    {
        return;
    }

    memset(record, 0, sizeof(*record));
    record->magic = APP_SWEEP_CAL_FLASH_MAGIC;
    record->version = APP_SWEEP_CAL_FLASH_VERSION;
    record->record_size = sizeof(*record);
    record->start_hz = g_sweep_cal.start_hz;
    record->stop_hz = g_sweep_cal.stop_hz;
    record->step_hz = g_sweep_cal.step_hz;
    record->point_count = g_sweep_cal.stats.point_count;
    record->clip_count = g_sweep_cal.stats.clip_count;
    record->max_vpp = g_sweep_cal.stats.max_vpp;
    record->min_vpp = g_sweep_cal.stats.min_vpp;
    record->baseline_avg_vpp = g_sweep_cal.baseline_avg_vpp;
    memcpy(record->baseline_vpp, g_sweep_cal.baseline_vpp, sizeof(record->baseline_vpp));
    memcpy(record->clip_flag, g_sweep_cal.clip_flag, sizeof(record->clip_flag));
    record->crc = sweep_cal_flash_crc(record);
}

/* 将通过校验的 Flash 校准记录恢复到扫频 RAM 基线。 */
static void sweep_cal_flash_import(const app_sweep_cal_flash_record_t *record)
{
    if (record == 0)
    {
        return;
    }

    memset(&g_sweep_cal, 0, sizeof(g_sweep_cal));
    g_sweep_cal.start_hz = record->start_hz;
    g_sweep_cal.stop_hz = record->stop_hz;
    g_sweep_cal.step_hz = record->step_hz;
    g_sweep_cal.baseline_avg_vpp = record->baseline_avg_vpp;
    g_sweep_cal.stats.point_count = (uint16_t)record->point_count;
    g_sweep_cal.stats.clip_count = record->clip_count;
    g_sweep_cal.stats.max_vpp = record->max_vpp;
    g_sweep_cal.stats.min_vpp = record->min_vpp;
    g_sweep_cal.stats.valid = 1U;
    memcpy(g_sweep_cal.baseline_vpp, record->baseline_vpp, sizeof(g_sweep_cal.baseline_vpp));
    memcpy(g_sweep_cal.clip_flag, record->clip_flag, sizeof(g_sweep_cal.clip_flag));
}

/* 从板载 Flash 恢复上次校准基线；失败时保持当前 RAM 状态不变。 */
uint8_t app_sweep_load_calibration_from_flash(void)
{
#if (APP_SWEEP_CAL_FLASH_ENABLE != 0U)
    app_board_flash_result_t result;

    result = app_board_flash_read(APP_SWEEP_CAL_FLASH_ADDR,
                                  (uint8_t *)&g_sweep_cal_flash_record,
                                  sizeof(g_sweep_cal_flash_record));
    if ((result != APP_BOARD_FLASH_OK) ||
        (sweep_cal_flash_record_valid(&g_sweep_cal_flash_record) == 0U))
    {
        return 0U;
    }

    sweep_cal_flash_import(&g_sweep_cal_flash_record);
    return 1U;
#else
    return 0U;
#endif
}

/* 将当前有效 RAM 校准基线写入板载 Flash，供下次上电继续使用。 */
uint8_t app_sweep_save_calibration_to_flash(void)
{
#if (APP_SWEEP_CAL_FLASH_ENABLE != 0U)
    app_board_flash_result_t result;

    if (g_sweep_cal.stats.valid == 0U)
    {
        return 0U;
    }

    sweep_cal_flash_export(&g_sweep_cal_flash_record);
    result = app_board_flash_erase_4k(APP_SWEEP_CAL_FLASH_ADDR);
    if (result != APP_BOARD_FLASH_OK)
    {
        return 0U;
    }

    result = app_board_flash_write(APP_SWEEP_CAL_FLASH_ADDR,
                                   (const uint8_t *)&g_sweep_cal_flash_record,
                                   sizeof(g_sweep_cal_flash_record));
    if (result != APP_BOARD_FLASH_OK)
    {
        return 0U;
    }

    memset(&g_sweep_cal_flash_record, 0, sizeof(g_sweep_cal_flash_record));
    result = app_board_flash_read(APP_SWEEP_CAL_FLASH_ADDR,
                                  (uint8_t *)&g_sweep_cal_flash_record,
                                  sizeof(g_sweep_cal_flash_record));
    return ((result == APP_BOARD_FLASH_OK) &&
            (sweep_cal_flash_record_valid(&g_sweep_cal_flash_record) != 0U)) ? 1U : 0U;
#else
    return 0U;
#endif
}

uint8_t app_sweep_is_done(void)
{
    return ((g_sweep.stage == APP_SWEEP_STAGE_DONE) && (g_sweep.active == 0U)) ? 1U : 0U;
}

void app_sweep_reset(void)
{
    memset(&g_sweep, 0, sizeof(g_sweep));
}
