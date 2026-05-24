#include "DemodTask.h"

#include "RtosTypes.h"
#include "AppDebugConfig.h"
#include "rx_demod.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "main.h"
#include "dac.h"
#include <stdio.h>
#include <string.h>

#define DEMOD_SAMPLE_RATE_HZ       2048000U
#define DEMOD_BLOCK_LOG_ENABLE     0U
#define DEMOD_START_LOG_ENABLE     1U
#define DEMOD_MODE_LOG_ENABLE      1U
#define DEMOD_DAC_OUT2_ENABLE      1U
#define DEMOD_DAC_IDLE_CODE        2048U
#define DEMOD_DEFAULT_SYMBOL_RATE_HZ 10000U
/* 解调算力监控开关：1=输出 demod_perf 周期/漏块/DAC 刷新统计，0=完全关闭该监控。 */
#define DEMOD_PERF_MONITOR_ENABLE  1U
/* 解调算力日志间隔，单位为已处理 ADC 块；512 块约 1 秒，避免串口日志影响实时性。 */
#define DEMOD_PERF_LOG_BLOCK_INTERVAL 512U
/* 运行中切频/切调制低算力监测开关：默认关闭，待上板专项测试后再打开。 */
#ifndef DEMOD_RUNTIME_MONITOR_ENABLE
#define DEMOD_RUNTIME_MONITOR_ENABLE 0U
#endif

#if (DEMOD_PERF_MONITOR_ENABLE != 0U)
typedef struct
{
    uint32_t cycles_last;
    uint32_t cycles_max;
    uint32_t over_budget_cnt;
    uint32_t block_cnt;
} demod_mode_perf_t;

typedef struct
{
    uint32_t cycles_last;
    uint32_t cycles_max;
    uint32_t cycles_budget;
    uint32_t over_budget_cnt;
    uint32_t block_cnt;
    uint32_t publish_overwrite_cnt;
    volatile uint32_t dac_half_cnt;
    volatile uint32_t dac_full_cnt;
    uint32_t dac_refresh_cnt;
    uint32_t dac_late_cnt;
    uint32_t dac_event_at_last_refresh;
    uint32_t log_next_block;
} demod_perf_stats_t;
#endif

#if (DEMOD_RUNTIME_MONITOR_ENABLE != 0U)
#include "ModDetectTask.h"
#include <math.h>

/* 监测抽点间隔；32 表示每 32 个 ADC 点取 1 点参与监测，降低运算量。 */
#define DEMOD_MONITOR_DECIM             32U
/* 建立基线所需的数据块数；块数越大越稳，响应越慢。 */
#define DEMOD_MONITOR_BASELINE_BLOCKS   4U
/* 连续异常块数门限；2 表示两块连续异常才认为运行中信号发生变化。 */
#define DEMOD_MONITOR_TRIGGER_BLOCKS    2U
/* 触发后的保持时间，单位为解调数据块，避免反复触发。 */
#define DEMOD_MONITOR_HOLDOFF_BLOCKS    12U
/* 低中频变化强触发门限，单位 Hz；对 CW/AM/ASK/PSK 更敏感。 */
#define DEMOD_MONITOR_LOW_IF_DELTA_HZ   1700L
/* 平均功率相对变化门限，单位千分比；不单独决定触发，只参与综合评分。 */
#define DEMOD_MONITOR_POWER_DELTA_PM    350U
/* 包络离散度相对变化门限，单位千分比；用于辅助发现 AM/ASK/PSK 形态变化。 */
#define DEMOD_MONITOR_ENV_CV_DELTA_PM   300U
/* 相位一致性变化门限，单位千分比；用于辅助发现相位/频率结构变化。 */
#define DEMOD_MONITOR_PHASE_DELTA_PM    260U

typedef struct
{
    uint32_t mean_power;
    uint32_t env_cv_pm;
    uint32_t short_phase_disp_pm;
    uint32_t long_phase_disp_pm;
    int32_t  low_if_hz;
} demod_monitor_features_t;

typedef struct
{
    demod_monitor_features_t baseline;
    uint8_t baseline_valid;
    uint8_t baseline_blocks;
    uint8_t suspect_blocks;
    uint8_t holdoff_blocks;
    uint32_t reanalyze_request_count;
} demod_runtime_monitor_t;
#endif

/* ── ADC 块接收（与 sweep_task_publish_block 镜像模式）── */
typedef struct
{
    const uint16_t *i_buf;
    const uint16_t *q_buf;
    uint32_t        sample_cnt;
    uint8_t         pending;
} demod_block_t;

static demod_block_t      g_demod_block;
static demod_task_stats_t g_demod_stats;
static volatile uint8_t   g_demod_active = 0U;
static analyze_result_t   g_demod_result;
/* 解调输出缓冲区按 ADC 单块长度预留，后续接 DAC DMA 时直接复用。 */
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static uint16_t           g_demod_dac_out_buf[RX_DEMOD_MAX_BLOCK_SAMPLES];
static uint8_t            g_demod_dac_out2_started = 0U;
static uint8_t            g_demod_dac_irq_configured = 0U;
#if (DEMOD_PERF_MONITOR_ENABLE != 0U)
static demod_perf_stats_t g_demod_perf;
static demod_mode_perf_t  g_demod_mode_perf[RX_MODE_PSK + 1U];
#endif
#if (DEMOD_RUNTIME_MONITOR_ENABLE != 0U)
static demod_runtime_monitor_t g_demod_monitor;
#endif

/* ── 内部辅助：Analyze 模式 → DEMODE 模式 ── */
#if (DEMOD_RUNTIME_MONITOR_ENABLE != 0U)
static uint32_t demod_abs_i32(int32_t value)
{
    return (value < 0) ? (uint32_t)(-value) : (uint32_t)value;
}

static uint64_t demod_abs_i64(int64_t value)
{
    return (value < 0) ? (uint64_t)(-value) : (uint64_t)value;
}

/* 提取低算力运行监测特征：抽点计算功率、包络离散度和块级低 IF 粗估计。 */
static void demod_monitor_extract_features(const uint16_t *i_buf,
                                           const uint16_t *q_buf,
                                           uint32_t sample_cnt,
                                           demod_monitor_features_t *features)
{
    uint32_t idx;
    uint32_t used = 0U;
    int64_t power_sum = 0;
    int64_t power_sq_sum = 0;
    int64_t short_re = 0;
    int64_t short_im = 0;
    int64_t long_re = 0;
    int64_t long_im = 0;
    int64_t if_re = 0;
    int64_t if_im = 0;
    const uint32_t short_lag = DEMOD_MONITOR_DECIM;
    const uint32_t long_lag = DEMOD_MONITOR_DECIM * 4U;

    if (features == NULL)
    {
        return;
    }

    memset(features, 0, sizeof(*features));
    if ((i_buf == NULL) || (q_buf == NULL) || (sample_cnt <= long_lag))
    {
        return;
    }

    for (idx = long_lag; idx < sample_cnt; idx += DEMOD_MONITOR_DECIM)
    {
        int32_t i0 = (int32_t)i_buf[idx] - 8192L;
        int32_t q0 = (int32_t)q_buf[idx] - 8192L;
        int32_t is = (int32_t)i_buf[idx - short_lag] - 8192L;
        int32_t qs = (int32_t)q_buf[idx - short_lag] - 8192L;
        int32_t il = (int32_t)i_buf[idx - long_lag] - 8192L;
        int32_t ql = (int32_t)q_buf[idx - long_lag] - 8192L;
        int64_t power = (int64_t)i0 * (int64_t)i0 + (int64_t)q0 * (int64_t)q0;

        power_sum += power;
        power_sq_sum += power * power;
        if_re += (int64_t)i0 * (int64_t)is + (int64_t)q0 * (int64_t)qs;
        if_im += (int64_t)q0 * (int64_t)is - (int64_t)i0 * (int64_t)qs;
        short_re += (int64_t)i0 * (int64_t)is + (int64_t)q0 * (int64_t)qs;
        short_im += (int64_t)q0 * (int64_t)is - (int64_t)i0 * (int64_t)qs;
        long_re += (int64_t)i0 * (int64_t)il + (int64_t)q0 * (int64_t)ql;
        long_im += (int64_t)q0 * (int64_t)il - (int64_t)i0 * (int64_t)ql;
        used++;
    }

    if ((used == 0U) || (power_sum <= 0))
    {
        return;
    }

    features->mean_power = (uint32_t)(power_sum / (int64_t)used);
    if (features->mean_power != 0U)
    {
        int64_t mean_power = power_sum / (int64_t)used;
        int64_t var = (power_sq_sum / (int64_t)used) - (mean_power * mean_power);
        if (var < 0)
        {
            var = 0;
        }
        features->env_cv_pm = (uint32_t)(((uint64_t)var * 1000ULL) /
                                         ((uint64_t)features->mean_power * (uint64_t)features->mean_power));
    }

    if ((if_re != 0) || (if_im != 0))
    {
        float phase = atan2f((float)if_im, (float)if_re);
        features->low_if_hz = (int32_t)((phase * (float)DEMOD_SAMPLE_RATE_HZ) /
                                        (6.283185307f * (float)short_lag));
    }

    if (power_sum > 0)
    {
        uint64_t short_coh = demod_abs_i64(short_re) + demod_abs_i64(short_im);
        uint64_t long_coh = demod_abs_i64(long_re) + demod_abs_i64(long_im);
        uint64_t total = (uint64_t)power_sum;

        features->short_phase_disp_pm = (short_coh >= total) ? 0U : (uint32_t)(((total - short_coh) * 1000ULL) / total);
        features->long_phase_disp_pm = (long_coh >= total) ? 0U : (uint32_t)(((total - long_coh) * 1000ULL) / total);
    }
}

/* 更新运行中监测状态；达到连续异常门限后请求重新扫频识别。 */
static void demod_runtime_monitor_process(const uint16_t *i_buf,
                                          const uint16_t *q_buf,
                                          uint32_t sample_cnt,
                                          analyze_mode_t mode)
{
    demod_monitor_features_t now;
    uint8_t score = 0U;
    uint32_t power_delta_pm = 0U;
    uint32_t env_delta_pm;
    uint32_t short_phase_delta_pm;
    uint32_t long_phase_delta_pm;
    uint32_t low_if_delta_hz;

    demod_monitor_extract_features(i_buf, q_buf, sample_cnt, &now);

    if (g_demod_monitor.baseline_valid == 0U)
    {
        if (g_demod_monitor.baseline_blocks == 0U)
        {
            g_demod_monitor.baseline = now;
        }
        else
        {
            g_demod_monitor.baseline.mean_power =
                (g_demod_monitor.baseline.mean_power + now.mean_power) / 2U;
            g_demod_monitor.baseline.env_cv_pm =
                (g_demod_monitor.baseline.env_cv_pm + now.env_cv_pm) / 2U;
            g_demod_monitor.baseline.short_phase_disp_pm =
                (g_demod_monitor.baseline.short_phase_disp_pm + now.short_phase_disp_pm) / 2U;
            g_demod_monitor.baseline.long_phase_disp_pm =
                (g_demod_monitor.baseline.long_phase_disp_pm + now.long_phase_disp_pm) / 2U;
            g_demod_monitor.baseline.low_if_hz =
                (g_demod_monitor.baseline.low_if_hz + now.low_if_hz) / 2;
        }

        g_demod_monitor.baseline_blocks++;
        if (g_demod_monitor.baseline_blocks >= DEMOD_MONITOR_BASELINE_BLOCKS)
        {
            g_demod_monitor.baseline_valid = 1U;
        }
        return;
    }

    if (g_demod_monitor.holdoff_blocks != 0U)
    {
        g_demod_monitor.holdoff_blocks--;
        return;
    }

    if (g_demod_monitor.baseline.mean_power != 0U)
    {
        uint32_t power_abs = (now.mean_power > g_demod_monitor.baseline.mean_power) ?
                             (now.mean_power - g_demod_monitor.baseline.mean_power) :
                             (g_demod_monitor.baseline.mean_power - now.mean_power);
        power_delta_pm = (uint32_t)(((uint64_t)power_abs * 1000ULL) /
                                    (uint64_t)g_demod_monitor.baseline.mean_power);
    }

    env_delta_pm = (now.env_cv_pm > g_demod_monitor.baseline.env_cv_pm) ?
                   (now.env_cv_pm - g_demod_monitor.baseline.env_cv_pm) :
                   (g_demod_monitor.baseline.env_cv_pm - now.env_cv_pm);
    short_phase_delta_pm = (now.short_phase_disp_pm > g_demod_monitor.baseline.short_phase_disp_pm) ?
                           (now.short_phase_disp_pm - g_demod_monitor.baseline.short_phase_disp_pm) :
                           (g_demod_monitor.baseline.short_phase_disp_pm - now.short_phase_disp_pm);
    long_phase_delta_pm = (now.long_phase_disp_pm > g_demod_monitor.baseline.long_phase_disp_pm) ?
                          (now.long_phase_disp_pm - g_demod_monitor.baseline.long_phase_disp_pm) :
                          (g_demod_monitor.baseline.long_phase_disp_pm - now.long_phase_disp_pm);
    low_if_delta_hz = demod_abs_i32(now.low_if_hz - g_demod_monitor.baseline.low_if_hz);

    if ((mode == ANALYZE_MODE_CW) || (mode == ANALYZE_MODE_AM) ||
        (mode == ANALYZE_MODE_ASK) || (mode == ANALYZE_MODE_PSK))
    {
        if (low_if_delta_hz >= DEMOD_MONITOR_LOW_IF_DELTA_HZ)
        {
            score += 2U;
        }
    }

    if (power_delta_pm >= DEMOD_MONITOR_POWER_DELTA_PM) { score++; }
    if (env_delta_pm >= DEMOD_MONITOR_ENV_CV_DELTA_PM) { score++; }
    if (short_phase_delta_pm >= DEMOD_MONITOR_PHASE_DELTA_PM) { score++; }
    if (long_phase_delta_pm >= DEMOD_MONITOR_PHASE_DELTA_PM) { score++; }

    if (score >= 2U)
    {
        g_demod_monitor.suspect_blocks++;
    }
    else
    {
        g_demod_monitor.suspect_blocks = 0U;
    }

    if (g_demod_monitor.suspect_blocks >= DEMOD_MONITOR_TRIGGER_BLOCKS)
    {
        char log_buf[180];
        int n;

        g_demod_monitor.reanalyze_request_count++;
        g_demod_monitor.suspect_blocks = 0U;
        g_demod_monitor.holdoff_blocks = DEMOD_MONITOR_HOLDOFF_BLOCKS;

        n = snprintf(log_buf,
                     sizeof(log_buf),
                     "demod:monitor trigger score=%u low_if=%ld env=%lu ps=%lu pl=%lu cnt=%lu\r\n",
                     (unsigned int)score,
                     (long)now.low_if_hz,
                     (unsigned long)now.env_cv_pm,
                     (unsigned long)now.short_phase_disp_pm,
                     (unsigned long)now.long_phase_disp_pm,
                     (unsigned long)g_demod_monitor.reanalyze_request_count);
        if ((n > 0) && ((size_t)n < sizeof(log_buf)))
        {
            print_queue_send(log_buf);
        }

        demod_task_stop();
        moddetect_task_request_mode(MODDETECT_RUN_TASK);
    }
}
#endif

#if (DEMOD_PERF_MONITOR_ENABLE != 0U)
/* 初始化 DWT 周期计数器；若 DDS 延时已打开 DWT，这里只复用，不重复清零。 */
static void demod_perf_counter_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
    {
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
}

static uint32_t demod_perf_mode_index(RxMode mode)
{
    return ((uint32_t)mode <= (uint32_t)RX_MODE_PSK) ? (uint32_t)mode : 0UL;
}

static uint32_t demod_perf_budget_cycles(uint32_t sample_cnt)
{
    if (DEMOD_SAMPLE_RATE_HZ == 0U)
    {
        return 0U;
    }

    return (uint32_t)(((uint64_t)SystemCoreClock * (uint64_t)sample_cnt) /
                      (uint64_t)DEMOD_SAMPLE_RATE_HZ);
}

static void demod_perf_reset_runtime(void)
{
    memset(&g_demod_perf, 0, sizeof(g_demod_perf));
    memset(g_demod_mode_perf, 0, sizeof(g_demod_mode_perf));
    g_demod_perf.log_next_block = DEMOD_PERF_LOG_BLOCK_INTERVAL;
}

static void demod_perf_update(RxMode mode, uint32_t cycles, uint32_t budget)
{
    uint32_t mode_idx = demod_perf_mode_index(mode);
    demod_mode_perf_t *mode_perf = &g_demod_mode_perf[mode_idx];

    g_demod_perf.cycles_last = cycles;
    g_demod_perf.cycles_budget = budget;
    if (cycles > g_demod_perf.cycles_max)
    {
        g_demod_perf.cycles_max = cycles;
    }
    if ((budget != 0U) && (cycles > budget))
    {
        g_demod_perf.over_budget_cnt++;
    }
    g_demod_perf.block_cnt++;

    mode_perf->cycles_last = cycles;
    if (cycles > mode_perf->cycles_max)
    {
        mode_perf->cycles_max = cycles;
    }
    if ((budget != 0U) && (cycles > budget))
    {
        mode_perf->over_budget_cnt++;
    }
    mode_perf->block_cnt++;
}

static void demod_perf_log_if_due(RxMode mode)
{
    char log_buf[192];
    int n;

    if (g_demod_perf.block_cnt < g_demod_perf.log_next_block)
    {
        return;
    }
    g_demod_perf.log_next_block = g_demod_perf.block_cnt + DEMOD_PERF_LOG_BLOCK_INTERVAL;

    n = snprintf(log_buf,
                 sizeof(log_buf),
                 "demod_perf:mode=%u cycles_last=%lu cycles_max=%lu budget=%lu over=%lu blocks=%lu pub_ovr=%lu dac_half=%lu dac_full=%lu dac_refresh=%lu dac_late=%lu\r\n",
                 (unsigned int)mode,
                 (unsigned long)g_demod_perf.cycles_last,
                 (unsigned long)g_demod_perf.cycles_max,
                 (unsigned long)g_demod_perf.cycles_budget,
                 (unsigned long)g_demod_perf.over_budget_cnt,
                 (unsigned long)g_demod_perf.block_cnt,
                 (unsigned long)g_demod_perf.publish_overwrite_cnt,
                 (unsigned long)g_demod_perf.dac_half_cnt,
                 (unsigned long)g_demod_perf.dac_full_cnt,
                 (unsigned long)g_demod_perf.dac_refresh_cnt,
                 (unsigned long)g_demod_perf.dac_late_cnt);
    if ((n > 0) && ((size_t)n < sizeof(log_buf)))
    {
        print_queue_send(log_buf);
    }
}
#endif

static RxMode demod_map_mode(analyze_mode_t analyze_mode)
{
    switch (analyze_mode)
    {
    case ANALYZE_MODE_AM:  return RX_MODE_AM;
    case ANALYZE_MODE_ASK: return RX_MODE_ASK;
    case ANALYZE_MODE_FM:  return RX_MODE_FM;
    case ANALYZE_MODE_FSK: return RX_MODE_FSK;
    case ANALYZE_MODE_PSK: return RX_MODE_PSK;
    case ANALYZE_MODE_CW:  return RX_MODE_LOOPBACK;
    default:               return RX_MODE_LOOPBACK;
    }
}

static uint32_t demod_symbol_rate_from_result(const analyze_result_t *result)
{
    if (result == NULL)
    {
        return DEMOD_DEFAULT_SYMBOL_RATE_HZ;
    }

    if (((result->param_valid_mask & ANALYZE_PARAM_SYMBOL_RATE_VALID) != 0U) &&
        (result->symbol_rate_hz != 0U))
    {
        return result->symbol_rate_hz;
    }

    if (((result->mode == ANALYZE_MODE_ASK) ||
         (result->mode == ANALYZE_MODE_PSK)) &&
        (result->mod_hz != 0U))
    {
        return result->mod_hz;
    }

    return DEMOD_DEFAULT_SYMBOL_RATE_HZ;
}

/* ── 内部辅助：运行一次解调处理 ── */
static void demod_process_block(RxMode            rx_mode,
                                const uint16_t   *i_buf,
                                const uint16_t   *q_buf,
                                uint32_t          n,
                                uint16_t         *dac_out)
{
    switch (rx_mode)
    {
    case RX_MODE_AM:
        RxDemod_AM_ProcessBlock(i_buf, q_buf, n, dac_out);
        break;
    case RX_MODE_ASK:
        RxDemod_ASK_ProcessBlock(i_buf, q_buf, n, dac_out);
        break;
    case RX_MODE_FM:
        RxDemod_FM_ProcessBlock(i_buf, q_buf, n, dac_out);
        break;
    case RX_MODE_FSK:
        RxDemod_FSK_ProcessBlock(i_buf, q_buf, n, dac_out);
        break;
    case RX_MODE_PSK:
        RxDemod_PSK_ProcessBlock(i_buf, q_buf, n, dac_out);
        break;
    default:
        break;
    }
}

/* ── 由 AdcTask 调用，发布 ADC 数据块给解调任务 ── */
#if (DEMOD_DAC_OUT2_ENABLE != 0U)
static void demod_dac_clean_cache(const uint16_t *buf, uint32_t sample_cnt)
{
#if (__DCACHE_PRESENT == 1U)
    uintptr_t start_addr;
    uintptr_t end_addr;

    if ((buf == NULL) || (sample_cnt == 0U))
    {
        return;
    }

    start_addr = ((uintptr_t)buf) & ~(uintptr_t)31U;
    end_addr = ((uintptr_t)&buf[sample_cnt] + 31U) & ~(uintptr_t)31U;
    SCB_CleanDCache_by_Addr((uint32_t *)start_addr, (int32_t)(end_addr - start_addr));
#else
    (void)buf;
    (void)sample_cnt;
#endif
}

static void demod_dac_fill_idle(void)
{
    uint32_t i;

    for (i = 0U; i < RX_DEMOD_MAX_BLOCK_SAMPLES; ++i)
    {
        g_demod_dac_out_buf[i] = DEMOD_DAC_IDLE_CODE;
    }

    demod_dac_clean_cache(g_demod_dac_out_buf, RX_DEMOD_MAX_BLOCK_SAMPLES);
}

static void demod_dac_out2_start(void)
{
    if (g_demod_dac_out2_started != 0U)
    {
        return;
    }

    if (g_demod_dac_irq_configured == 0U)
    {
        HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
        g_demod_dac_irq_configured = 1U;
    }

    demod_dac_fill_idle();

    if (HAL_DAC_Start_DMA(&hdac1,
                          DAC_CHANNEL_2,
                          (uint32_t *)g_demod_dac_out_buf,
                          RX_DEMOD_MAX_BLOCK_SAMPLES,
                          DAC_ALIGN_12B_R) == HAL_OK)
    {
        g_demod_dac_out2_started = 1U;
    }
}

static void demod_dac_out2_stop(void)
{
    if (g_demod_dac_out2_started != 0U)
    {
        (void)HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_2);
        g_demod_dac_out2_started = 0U;
    }

    (void)HAL_DAC_Start(&hdac1, DAC_CHANNEL_2);
    (void)HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, DEMOD_DAC_IDLE_CODE);
}

void HAL_DAC_ConvHalfCpltCallbackCh2(DAC_HandleTypeDef *hdac)
{
    if (hdac == &hdac1)
    {
#if (DEMOD_PERF_MONITOR_ENABLE != 0U)
        g_demod_perf.dac_half_cnt++;
#else
        (void)hdac;
#endif
    }
}

void HAL_DAC_ConvCpltCallbackCh2(DAC_HandleTypeDef *hdac)
{
    if (hdac == &hdac1)
    {
#if (DEMOD_PERF_MONITOR_ENABLE != 0U)
        g_demod_perf.dac_full_cnt++;
#else
        (void)hdac;
#endif
    }
}
#endif

void demod_task_publish_block(const uint16_t *i_buf,
                              const uint16_t *q_buf,
                              uint32_t        sample_cnt)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
#if (DEMOD_PERF_MONITOR_ENABLE != 0U)
    if (g_demod_block.pending != 0U)
    {
        g_demod_perf.publish_overwrite_cnt++;
    }
#endif
    g_demod_block.i_buf      = i_buf;
    g_demod_block.q_buf      = q_buf;
    g_demod_block.sample_cnt = sample_cnt;
    g_demod_block.pending    = 1U;
    __set_PRIMASK(primask);
}

/* ── 公共 API ── */

void demod_task_start_with_result(const analyze_result_t *result)
{
    uint32_t primask;

    if (result == NULL)
    {
        return;
    }

    RxDemod_Reset();
#if (DEMOD_PERF_MONITOR_ENABLE != 0U)
    demod_perf_reset_runtime();
#endif

    primask = __get_PRIMASK();
    __disable_irq();
    g_demod_result = *result;
    g_demod_active = 1U;
    __set_PRIMASK(primask);

#if (DEMOD_RUNTIME_MONITOR_ENABLE != 0U)
    memset(&g_demod_monitor, 0, sizeof(g_demod_monitor));
#endif

#if (DEMOD_START_LOG_ENABLE != 0U)
    {
        char log_buf[160];
        int n = snprintf(log_buf, sizeof(log_buf),
                         "demod: start mode=%d center=%luHz mod=%luHz sym=%luHz fsk_sep=%luHz low_if=%ldHz\r\n",
                         (int)result->mode,
                         (unsigned long)result->center_hz,
                         (unsigned long)result->mod_hz,
                         (unsigned long)result->symbol_rate_hz,
                         (unsigned long)result->fsk_separation_hz,
                         (long)result->low_if_hz);
        if ((n > 0) && ((size_t)n < sizeof(log_buf)))
        {
            print_queue_send(log_buf);
        }
    }
#endif
}

void demod_task_stop(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    g_demod_active          = 0U;
    g_demod_stats.state     = DEMOD_STATE_IDLE;
    __set_PRIMASK(primask);

    RxDemod_Reset();
#if (DEMOD_RUNTIME_MONITOR_ENABLE != 0U)
    memset(&g_demod_monitor, 0, sizeof(g_demod_monitor));
#endif
#if (DEMOD_DAC_OUT2_ENABLE != 0U)
    demod_dac_out2_stop();
#endif
}

void demod_task_get_stats(demod_task_stats_t *stats_out)
{
    uint32_t primask;

    if (stats_out == NULL)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    *stats_out = g_demod_stats;
    __set_PRIMASK(primask);
}

uint8_t demod_task_is_running(void)
{
    return (g_demod_active != 0U) ? 1U : 0U;
}

/* ── RTOS 任务入口 ── */
void StartDemodTask(void *argument)
{
    (void)argument;

    /* 解调输出缓冲区已放到静态 DMA 区，任务这里只保留运行状态。 */
    RxMode   current_mode = RX_MODE_LOOPBACK;
    uint8_t  mode_configured = 0U;

#if (DEMOD_PERF_MONITOR_ENABLE != 0U)
    demod_perf_counter_init();
#endif
    RxDemod_Init(DEMOD_SAMPLE_RATE_HZ);

    for (;;)
    {
        demod_block_t block;
        uint32_t      primask;

        /* 未激活时休眠，等 demod_task_start_with_result 唤醒。 */
        if (g_demod_active == 0U)
        {
            mode_configured = 0U;
            g_demod_block.pending = 0U;
            osDelay(20U);
            continue;
        }

        /* 首次激活或模式变更时，重新配置解调器。 */
        if (mode_configured == 0U)
        {
            analyze_result_t result;
            uint32_t demod_symbol_rate_hz;

            primask = __get_PRIMASK();
            __disable_irq();
            result = g_demod_result;
            __set_PRIMASK(primask);

            current_mode = demod_map_mode(result.mode);
            demod_symbol_rate_hz = demod_symbol_rate_from_result(&result);
            RxDemod_SetMode(current_mode);
            RxDemod_ConfigureSignal(demod_symbol_rate_hz,
                                     result.low_if_hz,
                                     result.fsk_separation_hz);
            mode_configured = 1U;

            g_demod_stats.state     = DEMOD_STATE_RUNNING;
            g_demod_stats.mode      = result.mode;
            g_demod_stats.center_hz = result.center_hz;
            g_demod_stats.mod_hz    = ((result.mode == ANALYZE_MODE_FSK) ||
                                       (result.mode == ANALYZE_MODE_PSK)) ?
                                      demod_symbol_rate_hz : result.mod_hz;
            g_demod_stats.depth_pm  = result.depth_pm;
            g_demod_stats.low_if_hz = result.low_if_hz;
            g_demod_stats.block_cnt = 0U;
            g_demod_stats.sample_cnt = 0U;

#if (DEMOD_DAC_OUT2_ENABLE != 0U)
            demod_dac_out2_start();
#endif

#if (DEMOD_MODE_LOG_ENABLE != 0U)
            {
                char log_buf[96];
                int n = snprintf(log_buf, sizeof(log_buf),
                                 "demod: mode set rx=%d analyze=%d\r\n",
                                 (int)current_mode, (int)result.mode);
                if ((n > 0) && ((size_t)n < sizeof(log_buf)))
                {
                    print_queue_send(log_buf);
                }
            }
#endif
        }

        /* 等待 ADC 数据块就绪 */
        if (DemodBlockReadySemHandle == NULL)
        {
            osDelay(10U);
            continue;
        }

        if (osSemaphoreAcquire(DemodBlockReadySemHandle, osWaitForever) != osOK)
        {
            continue;
        }

        /* 临界区领取数据块，与 AdcTask ISR 互斥。 */
        primask = __get_PRIMASK();
        __disable_irq();
        block = g_demod_block;
        g_demod_block.pending = 0U;
        __set_PRIMASK(primask);

        if (block.pending == 0U)
        {
            continue;
        }

        if (g_demod_active == 0U)
        {
            continue;
        }

        if (block.sample_cnt > RX_DEMOD_MAX_BLOCK_SAMPLES)
        {
            block.sample_cnt = RX_DEMOD_MAX_BLOCK_SAMPLES;
        }

        /* 运行解调算法 */
#if (DEMOD_PERF_MONITOR_ENABLE != 0U)
        {
            uint32_t t0 = DWT->CYCCNT;
            uint32_t cycles;
            uint32_t budget;

            demod_process_block(current_mode,
                                block.i_buf,
                                block.q_buf,
                                block.sample_cnt,
                                g_demod_dac_out_buf);
            cycles = DWT->CYCCNT - t0;
            budget = demod_perf_budget_cycles(block.sample_cnt);
            demod_perf_update(current_mode, cycles, budget);
        }
#else
        demod_process_block(current_mode,
                            block.i_buf,
                            block.q_buf,
                            block.sample_cnt,
                            g_demod_dac_out_buf);
#endif

#if (DEMOD_RUNTIME_MONITOR_ENABLE != 0U)
        demod_runtime_monitor_process(block.i_buf,
                                      block.q_buf,
                                      block.sample_cnt,
                                      g_demod_stats.mode);
#endif

#if (DEMOD_DAC_OUT2_ENABLE != 0U)
        demod_dac_clean_cache(g_demod_dac_out_buf, block.sample_cnt);
#if (DEMOD_PERF_MONITOR_ENABLE != 0U)
        {
            uint32_t dac_events = g_demod_perf.dac_half_cnt + g_demod_perf.dac_full_cnt;
            if ((dac_events - g_demod_perf.dac_event_at_last_refresh) > 2U)
            {
                g_demod_perf.dac_late_cnt++;
            }
            g_demod_perf.dac_event_at_last_refresh = dac_events;
            g_demod_perf.dac_refresh_cnt++;
        }
#endif
#endif

        /* DAC1_OUT2(PA5) 的 DMA 已在进入解调时启动，这里只刷新输出缓冲。 */
        (void)g_demod_dac_out_buf;

        g_demod_stats.block_cnt++;
        g_demod_stats.sample_cnt += block.sample_cnt;
#if (DEMOD_PERF_MONITOR_ENABLE != 0U)
        demod_perf_log_if_due(current_mode);
#endif
    }
}
