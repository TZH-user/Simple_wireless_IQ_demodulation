#include "app_phase_align.h"

#include <math.h>
#include <string.h>

#include "cmsis_os2.h"

/* 宏定义说明：APP_PHASE_ALIGN_PI 是相位换算使用的圆周率常量。 */
#define APP_PHASE_ALIGN_PI 3.14159265359f
/* 宏定义说明：APP_PHASE_ALIGN_TWO_PI 是把相位限制到 [-pi, pi] 时使用的 2pi 常量。 */
#define APP_PHASE_ALIGN_TWO_PI 6.28318530718f
/* 宏定义说明：APP_PHASE_ALIGN_URAD_PER_RAD 是弧度转换为微弧度的比例。 */
#define APP_PHASE_ALIGN_URAD_PER_RAD 1000000.0f
/* 宏定义说明：APP_PHASE_ALIGN_PI_URAD 是 pi 弧度对应的微弧度，用于整数相位差回绕。 */
#define APP_PHASE_ALIGN_PI_URAD 3141593L
/* 宏定义说明：APP_PHASE_ALIGN_TWO_PI_URAD 是 2pi 弧度对应的微弧度，用于整数相位差回绕。 */
#define APP_PHASE_ALIGN_TWO_PI_URAD 6283185L

/* 宏定义说明：APP_PHASE_ALIGN_STABLE_WINDOW_MS 是进入相位环前的稳定观测窗口，单位 ms。 */
#ifndef APP_PHASE_ALIGN_STABLE_WINDOW_MS
#define APP_PHASE_ALIGN_STABLE_WINDOW_MS 3000U
#endif

/* 宏定义说明：APP_PHASE_ALIGN_RMS_LIMIT_URAD 是 30 度相位 RMS 门限，单位微弧度。 */
#ifndef APP_PHASE_ALIGN_RMS_LIMIT_URAD
#define APP_PHASE_ALIGN_RMS_LIMIT_URAD 523599L
#endif

/* 宏定义说明：APP_PHASE_ALIGN_DEADBAND_URAD 是认为相位已经接近 0 度的死区，单位微弧度。 */
#ifndef APP_PHASE_ALIGN_DEADBAND_URAD
#define APP_PHASE_ALIGN_DEADBAND_URAD 0L
#endif

/* 宏定义说明：APP_PHASE_ALIGN_MIN_CENTROID_MAG 是判定 IQ 重心可靠的最小幅度。 */
#ifndef APP_PHASE_ALIGN_MIN_CENTROID_MAG
#define APP_PHASE_ALIGN_MIN_CENTROID_MAG 64U
#endif

/* 宏定义说明：APP_PHASE_ALIGN_MIN_CONFIDENCE_PERCENT 是允许进入相位环的调制识别置信度下限。 */
#ifndef APP_PHASE_ALIGN_MIN_CONFIDENCE_PERCENT
#define APP_PHASE_ALIGN_MIN_CONFIDENCE_PERCENT 35U
#endif

/* 宏定义说明：APP_PHASE_ALIGN_FREQ_PROTECT_MHZ 是相位环期间允许的最大残余频偏，单位 mHz。 */
#ifndef APP_PHASE_ALIGN_FREQ_PROTECT_MHZ
#define APP_PHASE_ALIGN_FREQ_PROTECT_MHZ 300L
#endif

typedef struct
{
    app_phase_align_status_t status;      /* 对外可读的相位归零状态快照。 */
    uint32_t window_start_tick;           /* 当前 3 秒 RMS 窗口起始 RTOS tick。 */
    uint64_t phase_sq_sum;                /* 当前窗口内相对起点的相位波动平方和，单位 urad^2。 */
    uint32_t phase_sample_count;          /* 当前窗口内参与 RMS 统计的 block 数。 */
    int32_t stable_ref_phase_urad;        /* 当前稳定窗口第一个有效相位，作为波动 RMS 的参考点。 */
    uint8_t stable_ref_valid;             /* 1 表示 stable_ref_phase_urad 已经由有效重心初始化。 */
    uint8_t reserved[3];
} app_phase_align_ctx_t;

static app_phase_align_ctx_t g_phase_align;

/* 将相位限制到 [-pi, pi]，避免跨越正负 pi 时相位误差跳变。 */
static float app_phase_align_wrap_pi(float angle)
{
    /* 判断：相位大于 pi 时需要向负方向折返，避免相位误差超过主值范围。 */
    while (angle > APP_PHASE_ALIGN_PI)
    {
        angle -= APP_PHASE_ALIGN_TWO_PI;
    }

    /* 判断：相位小于 -pi 时需要向正方向折返，避免相位误差超过主值范围。 */
    while (angle < -APP_PHASE_ALIGN_PI)
    {
        angle += APP_PHASE_ALIGN_TWO_PI;
    }

    return angle;
}

/* 计算 int32 的绝对值，避免在多个判断中重复写三目表达式。 */
static int32_t app_phase_align_abs_i32(int32_t value)
{
    /* 判断：负数需要取反后再参与门限比较。 */
    if (value < 0)
    {
        return -value;
    }

    return value;
}

/* 计算两个相位之间的主值差，结果限制在 [-pi, pi]，避免跨越 180 度时误判为大跳变。 */
static int32_t app_phase_align_wrap_delta_urad(int32_t phase_urad, int32_t ref_urad)
{
    int32_t delta = phase_urad - ref_urad;

    /* 判断：相位差大于 pi 时说明跨过了 +180 度边界，需要减去 2pi 回到主值范围。 */
    while (delta > APP_PHASE_ALIGN_PI_URAD)
    {
        delta -= APP_PHASE_ALIGN_TWO_PI_URAD;
    }

    /* 判断：相位差小于 -pi 时说明跨过了 -180 度边界，需要加上 2pi 回到主值范围。 */
    while (delta < -APP_PHASE_ALIGN_PI_URAD)
    {
        delta += APP_PHASE_ALIGN_TWO_PI_URAD;
    }

    return delta;
}

/* 用整数平方根计算窗口 RMS，避免把 64 位平方和转成 double。 */
static uint32_t app_phase_align_isqrt_u64(uint64_t value)
{
    uint64_t bit = 1ULL << 62;
    uint64_t root = 0ULL;

    /* 判断：寻找不大于输入值的最高 4 进制位，作为整数开方起点。 */
    while (bit > value)
    {
        bit >>= 2U;
    }

    /* 判断：bit 非零表示整数开方迭代尚未完成。 */
    while (bit != 0ULL)
    {
        /* 判断：当前候选根如果不超过剩余值，则接受该位。 */
        if (value >= (root + bit))
        {
            value -= root + bit;
            root = (root >> 1U) + bit;
        }
        else
        {
            root >>= 1U;
        }
        bit >>= 2U;
    }

    return (root > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)root;
}

/* 判断调制识别结果是否足够稳定，稳定后才允许相位环从预检查进入闭环。 */
static uint8_t app_phase_align_mod_status_ready(const app_mod_detect_status_t *mod_status)
{
    /* 判断：没有调制识别状态时不能确认当前信号类型，只能冻结相位闭环。 */
    if (mod_status == NULL)
    {
        return 0U;
    }

    /* 判断：识别任务尚未处理任何 block 时，stable_mode 还没有意义。 */
    if (mod_status->block_count == 0U)
    {
        return 0U;
    }

    /* 判断：没有载波时不允许进入相位归零，避免噪声重心误导 VRFE。 */
    if (mod_status->carrier_present == 0U)
    {
        return 0U;
    }

    /* 判断：稳定投票置信度不足时冻结相位环，等待更多识别 block。 */
    if (mod_status->stable_confidence_percent < APP_PHASE_ALIGN_MIN_CONFIDENCE_PERCENT)
    {
        return 0U;
    }

    return 1U;
}

/* 判断当前调制类型是否需要更严格的重心可靠度。 */
static uint32_t app_phase_align_mode_min_mag(app_mod_detect_mode_t mode)
{
    /* 判断：FM/FSK/PSK 的统计重心更容易被调制内容抵消，因此要求更高重心幅度。 */
    if ((mode == APP_MODDET_MODE_FM) ||
        (mode == APP_MODDET_MODE_FSK) ||
        (mode == APP_MODDET_MODE_PSK))
    {
        return APP_PHASE_ALIGN_MIN_CENTROID_MAG * 2U;
    }

    return APP_PHASE_ALIGN_MIN_CENTROID_MAG;
}

/* 对当前 block 去中心求 IQ 重心，并把 atan2(Q, I) 作为相位归零误差。 */
static uint8_t app_phase_align_calc_centroid_phase(const uint16_t *i_buf,
                                                   const uint16_t *q_buf,
                                                   uint32_t sample_cnt,
                                                   uint16_t adc_mid,
                                                   app_mod_detect_mode_t mode,
                                                   int32_t *phase_urad_out,
                                                   uint32_t *mag_out)
{
    int64_t sum_i = 0;
    int64_t sum_q = 0;
    uint32_t used_count = 0U;
    uint32_t idx;
    float phase_rad;
    uint64_t mag_sq;
    uint32_t min_mag;

    /* 判断：输入指针或输出指针为空时无法计算相位，直接返回无效。 */
    if ((i_buf == NULL) || (q_buf == NULL) || (phase_urad_out == NULL) || (mag_out == NULL))
    {
        return 0U;
    }

    /* 判断：采样点数为 0 时没有可统计样本，直接返回无效。 */
    if (sample_cnt == 0U)
    {
        return 0U;
    }

    for (idx = 0U; idx < sample_cnt; idx++)
    {
        int32_t ci = (int32_t)i_buf[idx] - (int32_t)adc_mid;
        int32_t cq = (int32_t)q_buf[idx] - (int32_t)adc_mid;
        uint32_t mag2 = (uint32_t)((ci * ci) + (cq * cq));

        /* 判断：AM/ASK 低幅度段相位不可信，只用有明显载波幅度的点参与重心统计。 */
        if (((mode == APP_MODDET_MODE_AM) || (mode == APP_MODDET_MODE_ASK)) &&
            (mag2 < (APP_PHASE_ALIGN_MIN_CENTROID_MAG * APP_PHASE_ALIGN_MIN_CENTROID_MAG)))
        {
            continue;
        }

        sum_i += ci;
        sum_q += cq;
        used_count++;
    }

    /* 判断：幅度门控后没有剩余样本时，说明当前 block 不适合更新相位环。 */
    if (used_count == 0U)
    {
        *phase_urad_out = 0;
        *mag_out = 0U;
        return 0U;
    }

    mag_sq = (uint64_t)((sum_i * sum_i) + (sum_q * sum_q));
    *mag_out = app_phase_align_isqrt_u64(mag_sq) / used_count;
    min_mag = app_phase_align_mode_min_mag(mode);

    /* 判断：重心幅度低于当前调制类型门限时，相位误差不可靠，应冻结相位环。 */
    if (*mag_out < min_mag)
    {
        *phase_urad_out = 0;
        return 0U;
    }

    /* 函数跳转：调用 atan2f()，把去中心后的 Q/I 重心转换为目标为 0 的相位误差。 */
    phase_rad = atan2f((float)sum_q, (float)sum_i);
    phase_rad = app_phase_align_wrap_pi(phase_rad);
    *phase_urad_out = (int32_t)(phase_rad * APP_PHASE_ALIGN_URAD_PER_RAD);

    return 1U;
}

/* 清空 3 秒相位 RMS 窗口，通常在重新锁定或窗口到期后调用。 */
static void app_phase_align_reset_window(uint32_t now_tick)
{
    g_phase_align.window_start_tick = now_tick;
    g_phase_align.phase_sq_sum = 0ULL;
    g_phase_align.phase_sample_count = 0U;
    g_phase_align.stable_ref_phase_urad = 0;
    g_phase_align.stable_ref_valid = 0U;
    g_phase_align.status.stable_window_ms = 0U;
    g_phase_align.status.phase_rms_urad = 0U;
}

void app_phase_align_init(void)
{
    /* 函数跳转：调用 memset()，清空相位归零上下文，避免复位前状态残留。 */
    memset(&g_phase_align, 0, sizeof(g_phase_align));
    g_phase_align.status.stage = APP_PHASE_ALIGN_STAGE_IDLE;
    g_phase_align.window_start_tick = osKernelGetTickCount();
}

void app_phase_align_reset(void)
{
    uint32_t now_tick;

    /* 函数跳转：调用 memset()，清空相位归零上下文，失锁后不沿用上一轮判断。 */
    memset(&g_phase_align, 0, sizeof(g_phase_align));
    g_phase_align.status.stage = APP_PHASE_ALIGN_STAGE_IDLE;
    now_tick = osKernelGetTickCount();
    app_phase_align_reset_window(now_tick);
}

void app_phase_align_update(uint8_t locked_gate,
                            const uint16_t *i_buf,
                            const uint16_t *q_buf,
                            uint32_t sample_cnt,
                            uint16_t adc_mid,
                            const app_iq_preproc_result_t *iq_result,
                            const app_mod_detect_status_t *mod_status)
{
    uint32_t now_tick;
    int32_t phase_error_urad = 0;
    uint32_t centroid_mag = 0U;
    uint8_t centroid_valid;
    uint64_t phase_sq;
    int32_t phase_delta_urad;

    /* 判断：扫频未锁定或 IQ 结果为空时，必须退出相位归零并清空状态。 */
    if ((locked_gate == 0U) || (iq_result == NULL))
    {
        app_phase_align_reset();
        return;
    }

    now_tick = osKernelGetTickCount();
    g_phase_align.status.mode = (mod_status != NULL) ? mod_status->mode : APP_MODDET_MODE_UNKNOWN;
    g_phase_align.status.stable_mode = (mod_status != NULL) ? mod_status->stable_mode : APP_MODDET_MODE_UNKNOWN;
    g_phase_align.status.stable_confidence_percent =
        (mod_status != NULL) ? mod_status->stable_confidence_percent : 0U;

    /* 函数跳转：调用 app_phase_align_calc_centroid_phase()，计算当前 block 的 IQ 重心相位。 */
    centroid_valid = app_phase_align_calc_centroid_phase(i_buf,
                                                         q_buf,
                                                         sample_cnt,
                                                         adc_mid,
                                                         g_phase_align.status.stable_mode,
                                                         &phase_error_urad,
                                                         &centroid_mag);
    g_phase_align.status.phase_error_urad = phase_error_urad;
    g_phase_align.status.centroid_mag = centroid_mag;
    g_phase_align.status.centroid_valid = centroid_valid;
    g_phase_align.status.allow_vrfe_update = 0U;
    g_phase_align.status.vrfe_phase_takeover = 0U;

    /* 判断：残余频偏重新变大时退出相位接管，回到频率稳定等待阶段。 */
    if (app_phase_align_abs_i32(iq_result->residual_freq_millihz) > APP_PHASE_ALIGN_FREQ_PROTECT_MHZ)
    {
        g_phase_align.status.stage = APP_PHASE_ALIGN_STAGE_FREQ_STABLE_WAIT;
        app_phase_align_reset_window(now_tick);
        return;
    }

    /* 判断：首次从空闲进入锁定后流程时，启动频率稳定等待窗口。 */
    if (g_phase_align.status.stage == APP_PHASE_ALIGN_STAGE_IDLE)
    {
        g_phase_align.status.stage = APP_PHASE_ALIGN_STAGE_FREQ_STABLE_WAIT;
        app_phase_align_reset_window(now_tick);
    }

    /* 判断：重心有效时才把当前相位波动纳入 3 秒 RMS 统计。 */
    if (centroid_valid != 0U)
    {
        /* 判断：当前稳定窗口还没有参考相位时，用第一个有效重心相位作为波动参考点。 */
        if (g_phase_align.stable_ref_valid == 0U)
        {
            g_phase_align.stable_ref_phase_urad = phase_error_urad;
            g_phase_align.stable_ref_valid = 1U;
        }

        /* 函数跳转：调用 app_phase_align_wrap_delta_urad()，计算当前相位相对窗口起点的主值差。 */
        phase_delta_urad = app_phase_align_wrap_delta_urad(phase_error_urad,
                                                           g_phase_align.stable_ref_phase_urad);
        phase_sq = (uint64_t)app_phase_align_abs_i32(phase_delta_urad) *
                   (uint64_t)app_phase_align_abs_i32(phase_delta_urad);
        g_phase_align.phase_sq_sum += phase_sq;
        g_phase_align.phase_sample_count++;
    }
    else
    {
        g_phase_align.status.freeze_count++;
    }

    g_phase_align.status.stable_window_ms = now_tick - g_phase_align.window_start_tick;

    /* 判断：当前 RMS 窗口有样本时，计算窗口内相对起点的相位波动 RMS 供稳定判据和调试观察使用。 */
    if (g_phase_align.phase_sample_count != 0U)
    {
        g_phase_align.status.phase_rms_urad =
            app_phase_align_isqrt_u64(g_phase_align.phase_sq_sum / g_phase_align.phase_sample_count);
    }

    /* 判断：频率稳定等待阶段必须满 3 秒且相位波动 RMS 不超过 30 度后才允许进入调制预检查。 */
    if (g_phase_align.status.stage == APP_PHASE_ALIGN_STAGE_FREQ_STABLE_WAIT)
    {
        if (g_phase_align.status.stable_window_ms < APP_PHASE_ALIGN_STABLE_WINDOW_MS)
        {
            return;
        }

        if ((g_phase_align.phase_sample_count == 0U) ||
            (g_phase_align.status.phase_rms_urad > APP_PHASE_ALIGN_RMS_LIMIT_URAD))
        {
            app_phase_align_reset_window(now_tick);
            return;
        }

        g_phase_align.status.stage = APP_PHASE_ALIGN_STAGE_MOD_PRECHECK;
    }

    /* 判断：调制预检查阶段要等待稳定模式和置信度，避免未知调制直接拉动 VRFE。 */
    if (g_phase_align.status.stage == APP_PHASE_ALIGN_STAGE_MOD_PRECHECK)
    {
        if (app_phase_align_mod_status_ready(mod_status) == 0U)
        {
            g_phase_align.status.freeze_count++;
            return;
        }

        g_phase_align.status.stage = APP_PHASE_ALIGN_STAGE_PHASE_ALIGN;
    }

    /* 判断：相位归零和保持阶段由相位环接管 VRFE，但重心不可靠时冻结输出。 */
    if ((g_phase_align.status.stage == APP_PHASE_ALIGN_STAGE_PHASE_ALIGN) ||
        (g_phase_align.status.stage == APP_PHASE_ALIGN_STAGE_PHASE_HOLD) ||
        (g_phase_align.status.stage == APP_PHASE_ALIGN_STAGE_MOD_RECHECK))
    {
        g_phase_align.status.vrfe_phase_takeover = 1U;

        if (centroid_valid == 0U)
        {
            g_phase_align.status.freeze_count++;
            return;
        }

        g_phase_align.status.allow_vrfe_update = 1U;
        g_phase_align.status.update_count++;

        if (app_phase_align_abs_i32(phase_error_urad) <= APP_PHASE_ALIGN_DEADBAND_URAD)
        {
            g_phase_align.status.stage = APP_PHASE_ALIGN_STAGE_MOD_RECHECK;
        }
        else
        {
            g_phase_align.status.stage = APP_PHASE_ALIGN_STAGE_PHASE_ALIGN;
        }
    }

    /* 判断：复核阶段看到调制识别仍有效后进入保持阶段。 */
    if (g_phase_align.status.stage == APP_PHASE_ALIGN_STAGE_MOD_RECHECK)
    {
        if (app_phase_align_mod_status_ready(mod_status) != 0U)
        {
            g_phase_align.status.stage = APP_PHASE_ALIGN_STAGE_PHASE_HOLD;
        }
    }
}

void app_phase_align_get_status(app_phase_align_status_t *status_out)
{
    /* 判断：输出指针为空时不能复制状态，直接返回。 */
    if (status_out == NULL)
    {
        return;
    }

    *status_out = g_phase_align.status;
}

uint8_t app_phase_align_phase_takeover_active(void)
{
    return g_phase_align.status.vrfe_phase_takeover;
}

uint8_t app_phase_align_allow_vrfe_update(void)
{
    return g_phase_align.status.allow_vrfe_update;
}

int32_t app_phase_align_get_phase_error_urad(void)
{
    return g_phase_align.status.phase_error_urad;
}
