#include "app_carrier_sync.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "cmsis_os2.h"
#include "dac.h"

/* 宏定义说明：APP_CARRIER_SYNC_CENTER_UV = 1590000；VRFE 中心电压，失锁或复位时回到这里。 */
#ifndef APP_CARRIER_SYNC_CENTER_UV
#define APP_CARRIER_SYNC_CENTER_UV 1590000
#endif

/* 宏定义说明：APP_CARRIER_SYNC_MIN_UV = 1400000；VRFE 电压下限，防止闭环把控制点拉出安全区。 */
#ifndef APP_CARRIER_SYNC_MIN_UV
#define APP_CARRIER_SYNC_MIN_UV 1400000
#endif

/* 宏定义说明：APP_CARRIER_SYNC_MAX_UV = 1700000；VRFE 电压上限，防止闭环把控制点拉出安全区。 */
#ifndef APP_CARRIER_SYNC_MAX_UV
#define APP_CARRIER_SYNC_MAX_UV 1700000
#endif

/* 宏定义说明：APP_CARRIER_SYNC_UPDATE_PERIOD_MS = 0U；两次频率闭环更新的最小间隔，0 表示每次调用都可更新。 */
#ifndef APP_CARRIER_SYNC_UPDATE_PERIOD_MS
#define APP_CARRIER_SYNC_UPDATE_PERIOD_MS 0U
#endif

/* 宏定义说明：APP_CARRIER_SYNC_DEADBAND_MHZ = 0；残余频偏死区，绝对值不超过该门限时不调 DAC。 */
#ifndef APP_CARRIER_SYNC_DEADBAND_MHZ
#define APP_CARRIER_SYNC_DEADBAND_MHZ 0
#endif

/* 宏定义说明：APP_CARRIER_SYNC_STEP_UV = 100；每次频率闭环调节的固定步进，单位 uV。 */
#ifndef APP_CARRIER_SYNC_STEP_UV
#define APP_CARRIER_SYNC_STEP_UV 100
#endif

/* 宏定义说明：APP_CARRIER_SYNC_CONTROL_POLARITY = 1；残余频偏为正时 DAC 调节方向极性。 */
#ifndef APP_CARRIER_SYNC_CONTROL_POLARITY
#define APP_CARRIER_SYNC_CONTROL_POLARITY 1
#endif

/* 宏定义说明：APP_CARRIER_SYNC_DAC_MAX_CODE = 4095U；12-bit DAC 最大码值。 */
#define APP_CARRIER_SYNC_DAC_MAX_CODE 4095U

/* 宏定义说明：APP_CARRIER_SYNC_DAC_REF_UV = 3300000UL；DAC 参考电压，单位 uV。 */
#define APP_CARRIER_SYNC_DAC_REF_UV 3300000UL

/* 宏定义说明：APP_CARRIER_PHASE_TARGET_MDEG = 0；相位观测默认参考 +I 轴。 */
#ifndef APP_CARRIER_PHASE_TARGET_MDEG
#define APP_CARRIER_PHASE_TARGET_MDEG 0
#endif

/* 宏定义说明：APP_CARRIER_PHASE_SAMPLE_STEP = 16U；逐点相位圆均值的抽样步进。 */
#ifndef APP_CARRIER_PHASE_SAMPLE_STEP
#define APP_CARRIER_PHASE_SAMPLE_STEP 16U
#endif

/* 宏定义说明：APP_CARRIER_PHASE_MIN_POINT_MAG = 32U；单点幅度低于该值时不参与相位平均。 */
#ifndef APP_CARRIER_PHASE_MIN_POINT_MAG
#define APP_CARRIER_PHASE_MIN_POINT_MAG 32U
#endif

/* 宏定义说明：APP_CARRIER_PHASE_MIN_USED_COUNT = 16U；有效点数低于该值时本 block 相位无效。 */
#ifndef APP_CARRIER_PHASE_MIN_USED_COUNT
#define APP_CARRIER_PHASE_MIN_USED_COUNT 16U
#endif

/* 宏定义说明：APP_CARRIER_PHASE_MIN_RESULTANT_PM = 150U；相位集中度低于该值时本 block 相位无效。 */
#ifndef APP_CARRIER_PHASE_MIN_RESULTANT_PM
#define APP_CARRIER_PHASE_MIN_RESULTANT_PM 150U
#endif

/* 宏定义说明：APP_CARRIER_SYNC_PI；相位换算使用的 pi 常量。 */
#define APP_CARRIER_SYNC_PI 3.14159265359f

/* 宏定义说明：APP_CARRIER_SYNC_TWO_PI；相位回绕使用的 2pi 常量。 */
#define APP_CARRIER_SYNC_TWO_PI 6.28318530718f

/* 宏定义说明：APP_CARRIER_SYNC_URAD_PER_RAD；弧度到微弧度换算系数。 */
#define APP_CARRIER_SYNC_URAD_PER_RAD 1000000.0f

/* 宏定义说明：APP_CARRIER_SYNC_MDEG_PER_RAD；弧度到毫度换算系数。 */
#define APP_CARRIER_SYNC_MDEG_PER_RAD 57295.7795f

/* 宏定义说明：APP_CARRIER_SYNC_RAD_PER_MDEG；毫度到弧度换算系数。 */
#define APP_CARRIER_SYNC_RAD_PER_MDEG (APP_CARRIER_SYNC_PI / 180000.0f)

typedef struct
{
    app_carrier_sync_status_t status; /* 对外可读状态快照。 */
    uint32_t last_update_tick;        /* 上一次频率闭环调节的 RTOS tick。 */
} app_carrier_sync_ctx_t;

static app_carrier_sync_ctx_t g_carrier_sync;
app_carrier_sync_status_t g_carrier_sync_status_dbg;

/* 发布调试快照，方便 STLINK 变量窗口观察当前闭环状态。 */
static void app_carrier_sync_publish_debug_status(void)
{
    g_carrier_sync_status_dbg = g_carrier_sync.status;
}

/* 计算 int32 绝对值，用于频偏和相位门限判断。 */
static int32_t app_carrier_sync_abs_i32(int32_t value)
{
    if (value < 0)
    {
        return -value;
    }

    return value;
}

/* 把相位限制到 [-pi, pi]，避免 atan2 主值跨越造成误判。 */
static float app_carrier_sync_wrap_pi(float phase_rad)
{
    while (phase_rad > APP_CARRIER_SYNC_PI)
    {
        phase_rad -= APP_CARRIER_SYNC_TWO_PI;
    }

    while (phase_rad < -APP_CARRIER_SYNC_PI)
    {
        phase_rad += APP_CARRIER_SYNC_TWO_PI;
    }

    return phase_rad;
}

/* 把目标电压限制在 VRFE 允许范围内。 */
static int32_t app_carrier_sync_clamp_uv(int32_t uv)
{
    if (uv < APP_CARRIER_SYNC_MIN_UV)
    {
        return APP_CARRIER_SYNC_MIN_UV;
    }

    if (uv > APP_CARRIER_SYNC_MAX_UV)
    {
        return APP_CARRIER_SYNC_MAX_UV;
    }

    return uv;
}

/* 把 uV 电压换算成 12-bit DAC code。 */
static uint16_t app_carrier_sync_uv_to_dac_code(int32_t uv)
{
    int32_t safe_uv = app_carrier_sync_clamp_uv(uv);
    uint32_t code = (uint32_t)((((uint64_t)safe_uv * APP_CARRIER_SYNC_DAC_MAX_CODE) +
                                (APP_CARRIER_SYNC_DAC_REF_UV / 2ULL)) /
                               APP_CARRIER_SYNC_DAC_REF_UV);

    if (code > APP_CARRIER_SYNC_DAC_MAX_CODE)
    {
        code = APP_CARRIER_SYNC_DAC_MAX_CODE;
    }

    return (uint16_t)code;
}

/* 写 DAC1_OUT1；第一次写入时顺便启动 DAC 通道。 */
static void app_carrier_sync_apply_uv(int32_t uv)
{
    uint16_t old_code = g_carrier_sync.status.dac_code;
    uint16_t code = app_carrier_sync_uv_to_dac_code(uv);

    g_carrier_sync.status.control_uv = app_carrier_sync_clamp_uv(uv);
    g_carrier_sync.status.dac_code = code;
    g_carrier_sync.status.dac_code_delta = (int16_t)((int32_t)code - (int32_t)old_code);
    g_carrier_sync.status.dac_code_changed = (code != old_code) ? 1U : 0U;

#if (APP_CARRIER_SYNC_ENABLE != 0U)
    if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, code) != HAL_OK)
    {
        g_carrier_sync.status.last_error = -1;
        return;
    }

    if (g_carrier_sync.status.dac_started == 0U)
    {
        if (HAL_DAC_Start(&hdac1, DAC_CHANNEL_1) != HAL_OK)
        {
            g_carrier_sync.status.last_error = -2;
            return;
        }

        g_carrier_sync.status.dac_started = 1U;
    }
#endif

    g_carrier_sync.status.last_error = 0;
    app_carrier_sync_publish_debug_status();
}

/* 清空相位观测字段，避免沿用上一轮锁定的旧结果。 */
static void app_carrier_sync_clear_phase_observation(void)
{
    g_carrier_sync.status.phase_valid = 0U;
    g_carrier_sync.status.phase_used_count = 0U;
    g_carrier_sync.status.phase_resultant_pm = 0U;
    g_carrier_sync.status.phase_error_urad = 0;
    g_carrier_sync.status.phase_error_mdeg = 0;
    g_carrier_sync.status.phase_abs_mdeg = 0;
}

/*
 * 计算一个 ADC block 的逐点相位圆均值。
 * 说明：
 *   - 抽样点先去中心，再换算成单位相位向量。
 *   - 最后只做一次 atan2f，降低单块计算量，同时保留跨 ±180° 的正确性。
 */
static uint8_t app_carrier_sync_calc_point_phase(const uint16_t *i_buf,
                                                 const uint16_t *q_buf,
                                                 uint32_t sample_cnt,
                                                 uint16_t adc_mid,
                                                 int32_t *phase_error_urad_out,
                                                 int32_t *phase_error_mdeg_out,
                                                 int32_t *phase_abs_mdeg_out,
                                                 uint32_t *used_count_out,
                                                 uint32_t *resultant_pm_out)
{
    float sum_cos = 0.0f;
    float sum_sin = 0.0f;
    uint32_t used_count = 0U;
    uint32_t step = APP_CARRIER_PHASE_SAMPLE_STEP;
    uint32_t idx;
    const uint32_t min_mag_sq = APP_CARRIER_PHASE_MIN_POINT_MAG * APP_CARRIER_PHASE_MIN_POINT_MAG;
    float mean_phase_rad;
    float target_rad;
    float error_rad;
    float resultant;
    uint32_t resultant_pm;

    if ((i_buf == NULL) || (q_buf == NULL) ||
        (phase_error_urad_out == NULL) || (phase_error_mdeg_out == NULL) ||
        (phase_abs_mdeg_out == NULL) || (used_count_out == NULL) ||
        (resultant_pm_out == NULL) || (sample_cnt == 0U))
    {
        return 0U;
    }

    if (step == 0U)
    {
        step = 1U;
    }

    for (idx = 0U; idx < sample_cnt; idx += step)
    {
        int32_t ci = (int32_t)i_buf[idx] - (int32_t)adc_mid;
        int32_t cq = (int32_t)q_buf[idx] - (int32_t)adc_mid;
        int32_t mag_sq_i32 = (ci * ci) + (cq * cq);
        uint32_t mag_sq = (mag_sq_i32 > 0) ? (uint32_t)mag_sq_i32 : 0U;

        if (mag_sq < min_mag_sq)
        {
            continue;
        }

        {
            float mag = sqrtf((float)mag_sq);

            if (mag <= 0.0f)
            {
                continue;
            }

            sum_cos += (float)ci / mag;
            sum_sin += (float)cq / mag;
            used_count++;
        }
    }

    *used_count_out = used_count;

    if (used_count < APP_CARRIER_PHASE_MIN_USED_COUNT)
    {
        *phase_error_urad_out = 0;
        *phase_error_mdeg_out = 0;
        *phase_abs_mdeg_out = 0;
        *resultant_pm_out = 0U;
        return 0U;
    }

    resultant = sqrtf((sum_cos * sum_cos) + (sum_sin * sum_sin)) / (float)used_count;
    resultant_pm = (uint32_t)((resultant * 1000.0f) + 0.5f);
    *resultant_pm_out = resultant_pm;

    if (resultant_pm < APP_CARRIER_PHASE_MIN_RESULTANT_PM)
    {
        *phase_error_urad_out = 0;
        *phase_error_mdeg_out = 0;
        *phase_abs_mdeg_out = 0;
        return 0U;
    }

    mean_phase_rad = atan2f(sum_sin, sum_cos);
    target_rad = (float)APP_CARRIER_PHASE_TARGET_MDEG * APP_CARRIER_SYNC_RAD_PER_MDEG;
    error_rad = app_carrier_sync_wrap_pi(target_rad - mean_phase_rad);

    *phase_error_urad_out = (int32_t)(error_rad * APP_CARRIER_SYNC_URAD_PER_RAD);
    *phase_error_mdeg_out = (int32_t)(error_rad * APP_CARRIER_SYNC_MDEG_PER_RAD);
    *phase_abs_mdeg_out = (int32_t)(mean_phase_rad * APP_CARRIER_SYNC_MDEG_PER_RAD);

    return 1U;
}

/* 频率闭环主路径：按 residual_freq_millihz 的符号固定步进调节 VRFE。 */
static void app_carrier_sync_update_frequency(uint32_t now_tick, const app_iq_preproc_result_t *iq_result)
{
    int32_t residual;
    int32_t abs_residual;
    int32_t direction;
    int32_t next_uv;

    if ((uint32_t)(now_tick - g_carrier_sync.last_update_tick) < APP_CARRIER_SYNC_UPDATE_PERIOD_MS)
    {
        return;
    }
    g_carrier_sync.last_update_tick = now_tick;

    residual = iq_result->residual_freq_millihz;
    abs_residual = app_carrier_sync_abs_i32(residual);
    g_carrier_sync.status.locked_gate = 1U;
    g_carrier_sync.status.residual_freq_millihz = residual;

    if (abs_residual <= APP_CARRIER_SYNC_DEADBAND_MHZ)
    {
        g_carrier_sync.status.hold_count++;
        app_carrier_sync_publish_debug_status();
        return;
    }

    direction = (residual > 0) ? 1 : -1;
    next_uv = g_carrier_sync.status.control_uv +
              (direction * APP_CARRIER_SYNC_CONTROL_POLARITY * APP_CARRIER_SYNC_STEP_UV);
    next_uv = app_carrier_sync_clamp_uv(next_uv);

    app_carrier_sync_apply_uv(next_uv);
    g_carrier_sync.status.update_count++;
    g_carrier_sync.status.freq_update_count++;
    app_carrier_sync_publish_debug_status();
}

void app_carrier_sync_init(void)
{
    memset(&g_carrier_sync, 0, sizeof(g_carrier_sync));
    g_carrier_sync.status.enabled = (APP_CARRIER_SYNC_ENABLE != 0U) ? 1U : 0U;
    g_carrier_sync.status.mode = APP_CARRIER_SYNC_MODE_FREQ;
    app_carrier_sync_apply_uv(APP_CARRIER_SYNC_CENTER_UV);
    app_carrier_sync_publish_debug_status();
}

void app_carrier_sync_reset_to_center(void)
{
    g_carrier_sync.status.locked_gate = 0U;
    g_carrier_sync.status.mode = APP_CARRIER_SYNC_MODE_FREQ;
    g_carrier_sync.status.residual_freq_millihz = 0;
    g_carrier_sync.status.dac_code_delta = 0;
    g_carrier_sync.status.dac_code_changed = 0U;
    app_carrier_sync_clear_phase_observation();
    g_carrier_sync.last_update_tick = osKernelGetTickCount();
    app_carrier_sync_apply_uv(APP_CARRIER_SYNC_CENTER_UV);
    app_carrier_sync_publish_debug_status();
}

void app_carrier_sync_update_iq(uint8_t locked_gate,
                                const uint16_t *i_buf,
                                const uint16_t *q_buf,
                                uint32_t sample_cnt,
                                uint16_t adc_mid,
                                const app_iq_preproc_result_t *iq_result)
{
    uint32_t now_tick;

    if ((APP_CARRIER_SYNC_ENABLE == 0U) || (g_carrier_sync.status.enabled == 0U))
    {
        return;
    }

    if ((locked_gate == 0U) || (iq_result == NULL))
    {
        if (g_carrier_sync.status.locked_gate != 0U)
        {
            app_carrier_sync_reset_to_center();
        }

        g_carrier_sync.status.locked_gate = 0U;
        g_carrier_sync.status.mode = APP_CARRIER_SYNC_MODE_FREQ;
        g_carrier_sync.status.hold_count++;
        app_carrier_sync_publish_debug_status();
        return;
    }

    now_tick = osKernelGetTickCount();
    g_carrier_sync.status.locked_gate = 1U;
    g_carrier_sync.status.mode = APP_CARRIER_SYNC_MODE_FREQ;
    g_carrier_sync.status.residual_freq_millihz = iq_result->residual_freq_millihz;

    if ((i_buf != NULL) && (q_buf != NULL) && (sample_cnt != 0U))
    {
        g_carrier_sync.status.phase_valid =
            app_carrier_sync_calc_point_phase(i_buf,
                                              q_buf,
                                              sample_cnt,
                                              adc_mid,
                                              &g_carrier_sync.status.phase_error_urad,
                                              &g_carrier_sync.status.phase_error_mdeg,
                                              &g_carrier_sync.status.phase_abs_mdeg,
                                              &g_carrier_sync.status.phase_used_count,
                                              &g_carrier_sync.status.phase_resultant_pm);
    }
    else
    {
        app_carrier_sync_clear_phase_observation();
    }

    app_carrier_sync_update_frequency(now_tick, iq_result);
}

void app_carrier_sync_update(uint8_t locked_gate, const app_iq_preproc_result_t *iq_result)
{
    app_carrier_sync_update_iq(locked_gate, NULL, NULL, 0U, 0U, iq_result);
}

void app_carrier_sync_on_unlock(void)
{
    /* 走失锁路径，停止使用锁定后的 IQ 结果。 */
    app_carrier_sync_update(0U, NULL);
}

void app_carrier_sync_process_locked_block(const uint16_t *i_buf,
                                           const uint16_t *q_buf,
                                           uint32_t sample_cnt,
                                           uint16_t adc_mid,
                                           const app_iq_preproc_result_t *iq_result)
{
    /* 走锁定路径，继续沿用现有频率闭环和相位观测逻辑。 */
    app_carrier_sync_update_iq(1U, i_buf, q_buf, sample_cnt, adc_mid, iq_result);
}

void app_carrier_sync_get_status(app_carrier_sync_status_t *status_out)
{
    if (status_out == NULL)
    {
        return;
    }

    *status_out = g_carrier_sync.status;
}
