#include "app_carrier_sync.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "cmsis_os2.h"
#include "dac.h"

/* VRFE 控制电压中心点，单位 uV；1590000uV 等效 1.590V。 */
/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_CARRIER_SYNC_CENTER_UV
/* 宏定义说明：APP_CARRIER_SYNC_CENTER_UV = 1590000；载波同步 DAC 的中心控制电压，复位或失锁时回到这个电压。 */
#define APP_CARRIER_SYNC_CENTER_UV 1590000
#endif

/* 第一版只允许在中心点附近小范围慢速调整，避免把 OCXO 控制端拉到危险区域。 */
/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_CARRIER_SYNC_MIN_UV
/* 宏定义说明：APP_CARRIER_SYNC_MIN_UV = 1400000；载波同步 DAC 允许输出的最小电压下限，防止控制量继续往低处跑。 */
#define APP_CARRIER_SYNC_MIN_UV 1400000
#endif

/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_CARRIER_SYNC_MAX_UV
/* 宏定义说明：APP_CARRIER_SYNC_MAX_UV = 1700000；载波同步 DAC 允许输出的最大电压上限，防止控制量继续往高处跑。 */
#define APP_CARRIER_SYNC_MAX_UV 1700000
#endif

/* 闭环更新周期，单位 ms；当前按 1s 慢速积分。 */
/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_CARRIER_SYNC_UPDATE_PERIOD_MS
/* 宏定义说明：APP_CARRIER_SYNC_UPDATE_PERIOD_MS = 0U；载波同步两次调节之间的最小时间间隔，0 表示每次调用都允许调节。 */
#define APP_CARRIER_SYNC_UPDATE_PERIOD_MS 0U
#endif

/* 残余频偏死区，单位 mHz；死区内不调整 VRFE，降低抖动。 */
/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_CARRIER_SYNC_DEADBAND_MHZ
/* 宏定义说明：APP_CARRIER_SYNC_DEADBAND_MHZ = 0；残余频偏死区，误差绝对值小于等于该值时不再调整 DAC。 */
#define APP_CARRIER_SYNC_DEADBAND_MHZ 0 
#endif

/* 每次闭环更新的电压步进，单位 uV；100uV 等效 0.1mV。 */
/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_CARRIER_SYNC_STEP_UV
/* 宏定义说明：APP_CARRIER_SYNC_STEP_UV = 100；每次根据残余频偏调节 DAC 的电压步进，单位为微伏。 */
#define APP_CARRIER_SYNC_STEP_UV 100
#endif

/*
 * 控制极性：
 *   +1：残余频偏为正时提高 VRFE 电压
 *   -1：残余频偏为正时降低 VRFE 电压
 * 若实测闭环方向相反，只改这个宏，不改扫频和 ADC 任务结构。
 */
/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_CARRIER_SYNC_CONTROL_POLARITY
/* 宏定义说明：APP_CARRIER_SYNC_CONTROL_POLARITY = 1；载波同步控制极性；决定残余频偏为正/负时 DAC 应该加还是减。 */
#define APP_CARRIER_SYNC_CONTROL_POLARITY 1
#endif

/* 宏定义说明：APP_CARRIER_SYNC_DAC_MAX_CODE = 4095U；12 位 DAC 的最大数字码值，用于把微伏换算成 DAC code。 */
#define APP_CARRIER_SYNC_DAC_MAX_CODE 4095U
/* 宏定义说明：APP_CARRIER_SYNC_DAC_REF_UV = 3300000UL；DAC 参考电压，单位微伏，用于电压到 code 的比例换算。 */
#define APP_CARRIER_SYNC_DAC_REF_UV   3300000UL

/* 宏定义说明：APP_CARRIER_PHASE_LOCK_ENABLE = 1U；频率稳定后允许相位闭环接管 VRFE，0 表示只保留频率闭环。 */
#ifndef APP_CARRIER_PHASE_LOCK_ENABLE
#define APP_CARRIER_PHASE_LOCK_ENABLE 1U
#endif

/* 宏定义说明：APP_CARRIER_PHASE_TARGET_MDEG = 90000；相位目标角，单位毫度；90000 表示先锁到 +Q 轴以避开 0 度跨越。 */
#ifndef APP_CARRIER_PHASE_TARGET_MDEG
#define APP_CARRIER_PHASE_TARGET_MDEG 90000
#endif

/* 宏定义说明：APP_CARRIER_PHASE_SAMPLE_STEP = 16U；逐点相位圆均值的抽样步进，16 表示 4096 点中取约 256 点。 */
#ifndef APP_CARRIER_PHASE_SAMPLE_STEP
#define APP_CARRIER_PHASE_SAMPLE_STEP 16U
#endif

/* 宏定义说明：APP_CARRIER_PHASE_MIN_POINT_MAG = 32U；单点去中心幅度低于该值时不参与相位平均，避免噪声点污染相位。 */
#ifndef APP_CARRIER_PHASE_MIN_POINT_MAG
#define APP_CARRIER_PHASE_MIN_POINT_MAG 32U
#endif

/* 宏定义说明：APP_CARRIER_PHASE_MIN_USED_COUNT = 16U；参与相位平均的最少点数，低于该值认为本 block 相位无效。 */
#ifndef APP_CARRIER_PHASE_MIN_USED_COUNT
#define APP_CARRIER_PHASE_MIN_USED_COUNT 16U
#endif

/* 宏定义说明：APP_CARRIER_PHASE_MIN_RESULTANT_PM = 150U；圆均值一致性门限，低于该值说明相位太分散，冻结相位环。 */
#ifndef APP_CARRIER_PHASE_MIN_RESULTANT_PM
#define APP_CARRIER_PHASE_MIN_RESULTANT_PM 150U
#endif

/* 宏定义说明：APP_CARRIER_PHASE_FREQ_STABLE_MHZ = 50；残余频偏连续小于该 mHz 门限后，才允许相位接管。 */
#ifndef APP_CARRIER_PHASE_FREQ_STABLE_MHZ
#define APP_CARRIER_PHASE_FREQ_STABLE_MHZ 50
#endif

/* 宏定义说明：APP_CARRIER_PHASE_STABLE_REQUIRED_COUNT = 500U；频偏稳定累计 block 数，500 个 2ms block 约等于 1 秒。 */
#ifndef APP_CARRIER_PHASE_STABLE_REQUIRED_COUNT
#define APP_CARRIER_PHASE_STABLE_REQUIRED_COUNT 500U
#endif

/* 宏定义说明：APP_CARRIER_PHASE_FREQ_PROTECT_MHZ = 500；相位模式下残余频偏超过该值时退回频率闭环。 */
#ifndef APP_CARRIER_PHASE_FREQ_PROTECT_MHZ
#define APP_CARRIER_PHASE_FREQ_PROTECT_MHZ 500
#endif

/* 宏定义说明：APP_CARRIER_PHASE_UPDATE_PERIOD_MS = 20U；相位闭环两次 DAC 调节的最小间隔，避免过快拉动 OCXO。 */
#ifndef APP_CARRIER_PHASE_UPDATE_PERIOD_MS
#define APP_CARRIER_PHASE_UPDATE_PERIOD_MS 20U
#endif

/* 宏定义说明：APP_CARRIER_PHASE_DEADBAND_MDEG = 500；相位误差死区，单位毫度；500 表示 0.5 度内默认不再调节。 */
#ifndef APP_CARRIER_PHASE_DEADBAND_MDEG
#define APP_CARRIER_PHASE_DEADBAND_MDEG 500
#endif

/* 宏定义说明：APP_CARRIER_PHASE_RATE_DEADBAND_MDEG = 150；相邻 block 相位变化小于该值时认为相位速度接近 0。 */
#ifndef APP_CARRIER_PHASE_RATE_DEADBAND_MDEG
#define APP_CARRIER_PHASE_RATE_DEADBAND_MDEG 150
#endif

/* 宏定义说明：APP_CARRIER_PHASE_DPLL_RATE_LEAD = 128；相位变化率阻尼权重，按 2ms block 估算约 256ms 后的相位趋势并提前减速。 */
#ifndef APP_CARRIER_PHASE_DPLL_RATE_LEAD
#define APP_CARRIER_PHASE_DPLL_RATE_LEAD 128
#endif

/* 宏定义说明：APP_CARRIER_PHASE_DPLL_RESIDUAL_GAIN = 1536；残余频偏 mHz 折算到相位控制量的权重，用于相位模式下优先压低频差。 */
#ifndef APP_CARRIER_PHASE_DPLL_RESIDUAL_GAIN
#define APP_CARRIER_PHASE_DPLL_RESIDUAL_GAIN 1536
#endif

/* 宏定义说明：APP_CARRIER_PHASE_DPLL_INTEGRAL_SCALE_MDEG_PER_UV = 90000；相位控制量每累计到该毫度值，频率修正改变约 1uV。 */
#ifndef APP_CARRIER_PHASE_DPLL_INTEGRAL_SCALE_MDEG_PER_UV
#define APP_CARRIER_PHASE_DPLL_INTEGRAL_SCALE_MDEG_PER_UV 90000
#endif

/* 宏定义说明：APP_CARRIER_PHASE_DPLL_MAX_STEP_UV = 3；相位 DPLL 单次允许调整 VRFE 的最大步进，单位 uV；2uV 实测捕获能力不足。 */
#ifndef APP_CARRIER_PHASE_DPLL_MAX_STEP_UV
#define APP_CARRIER_PHASE_DPLL_MAX_STEP_UV 3
#endif

/* 宏定义说明：APP_CARRIER_PHASE_DPLL_TRIM_LIMIT_UV = 3000；相位环只允许在接管基准电压附近微调的最大范围，单位 uV。 */
#ifndef APP_CARRIER_PHASE_DPLL_TRIM_LIMIT_UV
#define APP_CARRIER_PHASE_DPLL_TRIM_LIMIT_UV 3000
#endif

/* 宏定义说明：APP_CARRIER_PHASE_DPLL_FRAC_SCALE = 256；相位环内部小数 uV 累加比例，用于支持低于 1uV 的慢速修正。 */
#define APP_CARRIER_PHASE_DPLL_FRAC_SCALE 256

/*
 * 宏定义说明：APP_CARRIER_PHASE_CONTROL_POLARITY 控制相位闭环方向。
 * +1：相位控制量为正时增加 DAC，方向与已验证的 residual_freq_millihz 频率环保持一致。
 * -1：如果示波器/日志显示越调越远，只改这个宏翻转方向，不改扫频。
 */
#ifndef APP_CARRIER_PHASE_CONTROL_POLARITY
#define APP_CARRIER_PHASE_CONTROL_POLARITY 1
#endif

/* 宏定义说明：APP_CARRIER_SYNC_PI 是相位换算使用的圆周率常量。 */
#define APP_CARRIER_SYNC_PI 3.14159265359f
/* 宏定义说明：APP_CARRIER_SYNC_TWO_PI 是相位回绕使用的 2pi 常量。 */
#define APP_CARRIER_SYNC_TWO_PI 6.28318530718f
/* 宏定义说明：APP_CARRIER_SYNC_URAD_PER_RAD 是弧度转换微弧度的比例。 */
#define APP_CARRIER_SYNC_URAD_PER_RAD 1000000.0f
/* 宏定义说明：APP_CARRIER_SYNC_MDEG_PER_RAD 是弧度转换毫度的比例。 */
#define APP_CARRIER_SYNC_MDEG_PER_RAD 57295.7795f
/* 宏定义说明：APP_CARRIER_SYNC_RAD_PER_MDEG 是毫度转换弧度的比例。 */
#define APP_CARRIER_SYNC_RAD_PER_MDEG (APP_CARRIER_SYNC_PI / 180000.0f)

typedef struct
{
    app_carrier_sync_status_t status;          /* 对外可读状态快照。 */
    uint32_t last_update_tick;                 /* 上一次频率闭环调节的 RTOS tick。 */
    uint32_t last_phase_update_tick;           /* 上一次相位闭环调节的 RTOS tick。 */
    int32_t prev_phase_error_mdeg;             /* 上一个有效 block 的相位误差，用于估计相位变化率。 */
    uint8_t prev_phase_valid;                  /* 上一个相位误差是否有效，避免接管首个 block 误算变化率。 */
    int32_t phase_base_uv;                     /* 相位接管瞬间继承的频率闭环电压，后续相位环只在该基准附近微调。 */
    int32_t phase_trim_uv_q;                   /* 相位环内部频率修正量，单位为 uV 的 Q8 小数。 */
} app_carrier_sync_ctx_t;

static app_carrier_sync_ctx_t g_carrier_sync;
app_carrier_sync_status_t g_carrier_sync_status_dbg;

/* 发布一份调试快照，方便 STLINK 变量窗口直接查看闭环模式和相位误差。 */
static void app_carrier_sync_publish_debug_status(void)
{
    g_carrier_sync_status_dbg = g_carrier_sync.status;
}

/* 计算 int32 绝对值，用于频偏和相位误差门限判断。 */
static int32_t app_carrier_sync_abs_i32(int32_t value)
{
    /* 判断：负数需要取反后再参与绝对值门限比较。 */
    if (value < 0)
    {
        return -value;
    }

    return value;
}

/* 把相位限制到 [-pi, pi]，避免目标 0 度附近出现正负 180 度跨越误判。 */
static float app_carrier_sync_wrap_pi(float phase_rad)
{
    /* 判断：相位超过 +pi 时减去 2pi，回到主值范围。 */
    while (phase_rad > APP_CARRIER_SYNC_PI)
    {
        phase_rad -= APP_CARRIER_SYNC_TWO_PI;
    }

    /* 判断：相位低于 -pi 时加上 2pi，回到主值范围。 */
    while (phase_rad < -APP_CARRIER_SYNC_PI)
    {
        phase_rad += APP_CARRIER_SYNC_TWO_PI;
    }

    return phase_rad;
}

/* 把毫度差值限制到 [-180000, 180000]，用于处理相位目标附近的跨越。 */
static int32_t app_carrier_sync_wrap_mdeg(int32_t phase_mdeg)
{
    /* 判断：相位差大于 +180 度时减去 360 度，得到最短方向误差。 */
    while (phase_mdeg > 180000)
    {
        phase_mdeg -= 360000;
    }

    /* 判断：相位差小于 -180 度时加上 360 度，得到最短方向误差。 */
    while (phase_mdeg < -180000)
    {
        phase_mdeg += 360000;
    }

    return phase_mdeg;
}

/*
 * 计算一个 ADC block 的逐点相位圆均值。
 * 说明：
 *   - 对抽样点逐点去中心，得到以 I 正方向为 x 正半轴的 IQ 坐标。
 *   - 单点相位等价为 atan2(Q, I)，但实现时累加单位向量，避免每点调用 atan2f。
 *   - 最后只调用一次 atan2f 得到圆均值相位，能正确处理 ±180 度跨越。
 */
static uint8_t app_carrier_sync_calc_point_phase(const uint16_t *i_buf,
                                                 const uint16_t *q_buf,
                                                 uint32_t sample_cnt,
                                                 uint16_t adc_mid,
                                                 int32_t *phase_error_urad_out,
                                                 int32_t *phase_error_mdeg_out,
                                                 uint32_t *used_count_out,
                                                 uint32_t *resultant_pm_out)
{
    float sum_cos = 0.0f;
    float sum_sin = 0.0f;
    uint32_t used_count = 0U;
    uint32_t idx;
    uint32_t step = APP_CARRIER_PHASE_SAMPLE_STEP;
    const uint32_t min_mag_sq = APP_CARRIER_PHASE_MIN_POINT_MAG * APP_CARRIER_PHASE_MIN_POINT_MAG;
    float mean_phase_rad;
    float target_rad;
    float error_rad;
    float resultant;
    uint32_t resultant_pm;

    /* 判断：输入/输出指针为空或采样数为 0 时，无法计算相位，直接返回无效。 */
    if ((i_buf == NULL) || (q_buf == NULL) ||
        (phase_error_urad_out == NULL) || (phase_error_mdeg_out == NULL) ||
        (used_count_out == NULL) || (resultant_pm_out == NULL) ||
        (sample_cnt == 0U))
    {
        return 0U;
    }

    /* 判断：抽样步进被配置为 0 时强制改为 1，防止 for 循环无法前进。 */
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

        /* 判断：当前点幅度过低时相位主要由噪声决定，因此不参与圆均值。 */
        if (mag_sq < min_mag_sq)
        {
            continue;
        }

        {
            /* 函数跳转：调用 sqrtf()，把单点幅度平方换成幅度，用于归一化单位相位向量。 */
            float mag = sqrtf((float)mag_sq);

            /* 判断：幅度为 0 是保护性分支，避免除零。 */
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

    /* 判断：有效点数不足时，当前 block 的逐点相位均值不可靠。 */
    if (used_count < APP_CARRIER_PHASE_MIN_USED_COUNT)
    {
        *phase_error_urad_out = 0;
        *phase_error_mdeg_out = 0;
        *resultant_pm_out = 0U;
        return 0U;
    }

    /* 函数跳转：调用 sqrtf()，计算圆均值向量长度，用于衡量点相位是否集中。 */
    resultant = sqrtf((sum_cos * sum_cos) + (sum_sin * sum_sin)) / (float)used_count;
    resultant_pm = (uint32_t)((resultant * 1000.0f) + 0.5f);
    *resultant_pm_out = resultant_pm;

    /* 判断：点相位过于分散时冻结相位环，避免调制或噪声把 VRFE 拉偏。 */
    if (resultant_pm < APP_CARRIER_PHASE_MIN_RESULTANT_PM)
    {
        *phase_error_urad_out = 0;
        *phase_error_mdeg_out = 0;
        return 0U;
    }

    /* 函数跳转：调用 atan2f()，由单位相位向量圆均值得到当前 block 的绝对相位。 */
    mean_phase_rad = atan2f(sum_sin, sum_cos);
    target_rad = (float)APP_CARRIER_PHASE_TARGET_MDEG * APP_CARRIER_SYNC_RAD_PER_MDEG;
    error_rad = app_carrier_sync_wrap_pi(mean_phase_rad - target_rad);

    *phase_error_urad_out = (int32_t)(error_rad * APP_CARRIER_SYNC_URAD_PER_RAD);
    *phase_error_mdeg_out = (int32_t)(error_rad * APP_CARRIER_SYNC_MDEG_PER_RAD);

    return 1U;
}

/* 将目标电压限制在 VRFE 允许调整范围内。 */
static int32_t app_carrier_sync_clamp_uv(int32_t uv)
{
    /* 判断：`uv < APP_CARRIER_SYNC_MIN_UV`。含义：判断载波同步 DAC 调节是否需要执行或是否成功；成立后直接返回/退出当前函数或返回指定结果。 */
    if (uv < APP_CARRIER_SYNC_MIN_UV)
    {
        return APP_CARRIER_SYNC_MIN_UV;
    }

    /* 判断：`uv > APP_CARRIER_SYNC_MAX_UV`。含义：判断载波同步 DAC 调节是否需要执行或是否成功；成立后直接返回/退出当前函数或返回指定结果。 */
    if (uv > APP_CARRIER_SYNC_MAX_UV)
    {
        return APP_CARRIER_SYNC_MAX_UV;
    }

    return uv;
}

/* 将 uV 电压转换成 12-bit DAC code，按 3.3V 参考电压计算。 */
static uint16_t app_carrier_sync_uv_to_dac_code(int32_t uv)
{
    /* 函数跳转：调用 app_carrier_sync_clamp_uv()，把控制电压限制在允许的最小/最大微伏范围内。 */
    int32_t safe_uv = app_carrier_sync_clamp_uv(uv);
    uint32_t code = (uint32_t)((((uint64_t)safe_uv * APP_CARRIER_SYNC_DAC_MAX_CODE) +
                                (APP_CARRIER_SYNC_DAC_REF_UV / 2ULL)) /
                               APP_CARRIER_SYNC_DAC_REF_UV);

    /* 判断：`code > APP_CARRIER_SYNC_DAC_MAX_CODE`。含义：判断载波同步 DAC 调节是否需要执行或是否成功；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (code > APP_CARRIER_SYNC_DAC_MAX_CODE)
    {
        code = APP_CARRIER_SYNC_DAC_MAX_CODE;
    }

    return (uint16_t)code;
}

/* 写入 DAC1_OUT1；第一次写入时同步启动 DAC 通道。 */
static void app_carrier_sync_apply_uv(int32_t uv)
{
    /* 函数跳转：调用 app_carrier_sync_uv_to_dac_code()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
    uint16_t code = app_carrier_sync_uv_to_dac_code(uv);

    /* 函数跳转：调用 app_carrier_sync_clamp_uv()，把控制电压限制在允许的最小/最大微伏范围内。 */
    g_carrier_sync.status.control_uv = app_carrier_sync_clamp_uv(uv);
    g_carrier_sync.status.dac_code = code;

/* 条件编译判断：判断 `(APP_CARRIER_SYNC_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (APP_CARRIER_SYNC_ENABLE != 0U)
    /* 判断：`HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, code) != HAL_OK`。含义：判断载波同步 DAC 调节是否需要执行或是否成功；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, code) != HAL_OK)
    {
        g_carrier_sync.status.last_error = -1;
        return;
    }

    /* 判断：`g_carrier_sync.status.dac_started == 0U`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；判断载波同步 DAC 调节是否需要执行或是否成功；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (g_carrier_sync.status.dac_started == 0U)
    {
        /* 判断：`HAL_DAC_Start(&hdac1, DAC_CHANNEL_1) != HAL_OK`。含义：判断载波同步 DAC 调节是否需要执行或是否成功；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
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

/* 初始化闭环状态，并让 VRFE 先回到 1.55V 中心点。 */
void app_carrier_sync_init(void)
{
    /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
    memset(&g_carrier_sync, 0, sizeof(g_carrier_sync));
    g_carrier_sync.status.enabled = (APP_CARRIER_SYNC_ENABLE != 0U) ? 1U : 0U;
    g_carrier_sync.status.mode = APP_CARRIER_SYNC_MODE_FREQ;
    g_carrier_sync.status.phase_lock_enabled = (APP_CARRIER_PHASE_LOCK_ENABLE != 0U) ? 1U : 0U;
    g_carrier_sync.status.phase_target_mdeg = APP_CARRIER_PHASE_TARGET_MDEG;
    /* 函数跳转：调用 app_carrier_sync_apply_uv()，把目标控制电压限幅并写到 DAC，更新当前同步状态。 */
    app_carrier_sync_apply_uv(APP_CARRIER_SYNC_CENTER_UV);
    app_carrier_sync_publish_debug_status();
}

/* 退出锁定态或重新扫频时回到中心点，避免沿用上一轮积分偏置。 */
void app_carrier_sync_reset_to_center(void)
{
    g_carrier_sync.status.locked_gate = 0U;
    g_carrier_sync.status.residual_freq_millihz = 0;
    g_carrier_sync.status.mode = APP_CARRIER_SYNC_MODE_FREQ;
    g_carrier_sync.status.phase_valid = 0U;
    g_carrier_sync.status.phase_takeover = 0U;
    g_carrier_sync.status.phase_allow_update = 0U;
    g_carrier_sync.status.phase_stable_count = 0U;
    g_carrier_sync.status.phase_used_count = 0U;
    g_carrier_sync.status.phase_resultant_pm = 0U;
    g_carrier_sync.status.phase_error_urad = 0;
    g_carrier_sync.status.phase_error_mdeg = 0;
    g_carrier_sync.status.phase_delta_mdeg = 0;
    g_carrier_sync.status.phase_control_mdeg = 0;
    g_carrier_sync.status.phase_delta_uv = 0;
    g_carrier_sync.prev_phase_error_mdeg = 0;
    g_carrier_sync.prev_phase_valid = 0U;
    g_carrier_sync.phase_base_uv = APP_CARRIER_SYNC_CENTER_UV;
    g_carrier_sync.phase_trim_uv_q = 0;
    /* 函数跳转：调用 osKernelGetTickCount()，读取 RTOS tick，用于记录扫频开始/结束或控制更新间隔。 */
    g_carrier_sync.last_update_tick = osKernelGetTickCount();
    g_carrier_sync.last_phase_update_tick = g_carrier_sync.last_update_tick;
    /* 函数跳转：调用 app_carrier_sync_apply_uv()，把目标控制电压限幅并写到 DAC，更新当前同步状态。 */
    app_carrier_sync_apply_uv(APP_CARRIER_SYNC_CENTER_UV);
    app_carrier_sync_publish_debug_status();
}

/* 频率闭环路径：按 residual_freq_millihz 的符号定点步进调整 VRFE。 */
static void app_carrier_sync_update_frequency(uint32_t now_tick, const app_iq_preproc_result_t *iq_result)
{
    int32_t residual;
    int32_t abs_residual;
    int32_t direction;
    int32_t next_uv;

    /* 判断：`(uint32_t)(now_tick - g_carrier_sync.last_update_tick) < APP_CARRIER_SYNC_UPDATE_PERIOD_MS`。含义：判断载波同步 DAC 调节是否需要执行或是否成功；成立后直接返回/退出当前函数或返回指定结果。 */
    if ((uint32_t)(now_tick - g_carrier_sync.last_update_tick) < APP_CARRIER_SYNC_UPDATE_PERIOD_MS)
    {
        return;
    }
    g_carrier_sync.last_update_tick = now_tick;

    residual = iq_result->residual_freq_millihz;
    abs_residual = app_carrier_sync_abs_i32(residual);
    g_carrier_sync.status.locked_gate = 1U;
    g_carrier_sync.status.residual_freq_millihz = residual;
    g_carrier_sync.status.phase_takeover = 0U;
    g_carrier_sync.status.phase_allow_update = 0U;

    /* 判断：`abs_residual <= APP_CARRIER_SYNC_DEADBAND_MHZ`。含义：判断载波同步 DAC 调节是否需要执行或是否成功；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (abs_residual <= APP_CARRIER_SYNC_DEADBAND_MHZ)
    {
        g_carrier_sync.status.hold_count++;
        app_carrier_sync_publish_debug_status();
        return;
    }

    direction = (residual > 0) ? 1 : -1;
    next_uv = g_carrier_sync.status.control_uv +
              (direction * APP_CARRIER_SYNC_CONTROL_POLARITY * APP_CARRIER_SYNC_STEP_UV);
    /* 函数跳转：调用 app_carrier_sync_clamp_uv()，把控制电压限制在允许的最小/最大微伏范围内。 */
    next_uv = app_carrier_sync_clamp_uv(next_uv);

    /* 函数跳转：调用 app_carrier_sync_apply_uv()，把目标控制电压限幅并写到 DAC，更新当前同步状态。 */
    app_carrier_sync_apply_uv(next_uv);
    g_carrier_sync.status.update_count++;
    g_carrier_sync.status.freq_update_count++;
    app_carrier_sync_publish_debug_status();
}

/* 相位闭环路径：相位接管后只按逐点相位误差微调 VRFE，频率环不再叠加输出。 */
static void app_carrier_sync_update_phase(uint32_t now_tick)
{
    int32_t abs_phase_mdeg;
    int32_t abs_delta_mdeg;
    int32_t control_mdeg;
    int32_t trim_step_q;
    int32_t trim_limit_q;
    int32_t desired_uv;
    int32_t delta_uv;
    int32_t next_uv;

    /* 判断：刚从频率闭环切入相位闭环时，继承当前 DAC 电压作为相位环基准，不回中心也不突跳。 */
    if (g_carrier_sync.status.phase_takeover == 0U)
    {
        g_carrier_sync.phase_base_uv = g_carrier_sync.status.control_uv;
        g_carrier_sync.phase_trim_uv_q = 0;
        g_carrier_sync.prev_phase_valid = 0U;
    }

    g_carrier_sync.status.mode = APP_CARRIER_SYNC_MODE_PHASE_LOCK;
    g_carrier_sync.status.phase_takeover = 1U;
    g_carrier_sync.status.phase_allow_update = 0U;

    /* 判断：当前 block 的逐点相位无效时冻结相位环，保持当前 DAC 电压不动。 */
    if (g_carrier_sync.status.phase_valid == 0U)
    {
        g_carrier_sync.prev_phase_valid = 0U;
        g_carrier_sync.status.phase_delta_mdeg = 0;
        g_carrier_sync.status.phase_control_mdeg = 0;
        g_carrier_sync.status.phase_delta_uv = 0;
        g_carrier_sync.status.hold_count++;
        app_carrier_sync_publish_debug_status();
        return;
    }

    /* 判断：已有上一个有效相位时，计算相邻 block 的相位变化率；首次进入相位模式时变化率置 0。 */
    if (g_carrier_sync.prev_phase_valid != 0U)
    {
        g_carrier_sync.status.phase_delta_mdeg =
            app_carrier_sync_wrap_mdeg(g_carrier_sync.status.phase_error_mdeg -
                                       g_carrier_sync.prev_phase_error_mdeg);
    }
    else
    {
        g_carrier_sync.status.phase_delta_mdeg = 0;
        g_carrier_sync.prev_phase_valid = 1U;
    }
    g_carrier_sync.prev_phase_error_mdeg = g_carrier_sync.status.phase_error_mdeg;

    abs_phase_mdeg = app_carrier_sync_abs_i32(g_carrier_sync.status.phase_error_mdeg);
    abs_delta_mdeg = app_carrier_sync_abs_i32(g_carrier_sync.status.phase_delta_mdeg);

    /* 判断：相位误差和相位变化率都落入死区时保持当前 DAC，不反复抖动。 */
    if ((abs_phase_mdeg <= APP_CARRIER_PHASE_DEADBAND_MDEG) &&
        (abs_delta_mdeg <= APP_CARRIER_PHASE_RATE_DEADBAND_MDEG))
    {
        g_carrier_sync.status.phase_control_mdeg = 0;
        g_carrier_sync.status.phase_delta_uv = 0;
        g_carrier_sync.status.hold_count++;
        app_carrier_sync_publish_debug_status();
        return;
    }

    /* 判断：相位更新时间未到时只更新状态，不写 DAC。 */
    if ((uint32_t)(now_tick - g_carrier_sync.last_phase_update_tick) < APP_CARRIER_PHASE_UPDATE_PERIOD_MS)
    {
        app_carrier_sync_publish_debug_status();
        return;
    }
    g_carrier_sync.last_phase_update_tick = now_tick;

    control_mdeg = g_carrier_sync.status.phase_error_mdeg +
                   (APP_CARRIER_PHASE_DPLL_RATE_LEAD * g_carrier_sync.status.phase_delta_mdeg) +
                   (APP_CARRIER_PHASE_DPLL_RESIDUAL_GAIN * g_carrier_sync.status.residual_freq_millihz);
    trim_step_q = APP_CARRIER_PHASE_CONTROL_POLARITY *
                  ((control_mdeg * APP_CARRIER_PHASE_DPLL_FRAC_SCALE) /
                   APP_CARRIER_PHASE_DPLL_INTEGRAL_SCALE_MDEG_PER_UV);

    /* 判断：误差未进死区但小数换算得到 0 时，补一个 Q8 最小量让慢漂仍能被纠正。 */
    if ((trim_step_q == 0) && (control_mdeg != 0))
    {
        trim_step_q = (control_mdeg > 0) ? APP_CARRIER_PHASE_CONTROL_POLARITY : -APP_CARRIER_PHASE_CONTROL_POLARITY;
    }

    g_carrier_sync.phase_trim_uv_q += trim_step_q;
    trim_limit_q = APP_CARRIER_PHASE_DPLL_TRIM_LIMIT_UV * APP_CARRIER_PHASE_DPLL_FRAC_SCALE;

    /* 判断：相位修正量超过正向安全范围时限幅，避免相位环把 OCXO 拉离频率锁定点太远。 */
    if (g_carrier_sync.phase_trim_uv_q > trim_limit_q)
    {
        g_carrier_sync.phase_trim_uv_q = trim_limit_q;
    }
    /* 判断：相位修正量超过负向安全范围时限幅，避免相位环把 OCXO 拉离频率锁定点太远。 */
    else if (g_carrier_sync.phase_trim_uv_q < -trim_limit_q)
    {
        g_carrier_sync.phase_trim_uv_q = -trim_limit_q;
    }

    desired_uv = g_carrier_sync.phase_base_uv +
                 (g_carrier_sync.phase_trim_uv_q / APP_CARRIER_PHASE_DPLL_FRAC_SCALE);
    delta_uv = desired_uv - g_carrier_sync.status.control_uv;

    /* 判断：相位 DPLL 希望的本次 DAC 正向变化过大时限幅，避免相位接近目标后被一次拉过头。 */
    if (delta_uv > APP_CARRIER_PHASE_DPLL_MAX_STEP_UV)
    {
        delta_uv = APP_CARRIER_PHASE_DPLL_MAX_STEP_UV;
    }
    /* 判断：相位 DPLL 希望的本次 DAC 负向变化过大时限幅，避免反向一次拉过头。 */
    else if (delta_uv < -APP_CARRIER_PHASE_DPLL_MAX_STEP_UV)
    {
        delta_uv = -APP_CARRIER_PHASE_DPLL_MAX_STEP_UV;
    }

    next_uv = g_carrier_sync.status.control_uv + delta_uv;
    next_uv = app_carrier_sync_clamp_uv(next_uv);

    /* 判断：小数修正尚未累计到 1uV 时，只更新内部修正量，不重复写相同 DAC code。 */
    if (next_uv == g_carrier_sync.status.control_uv)
    {
        g_carrier_sync.status.phase_control_mdeg = control_mdeg;
        g_carrier_sync.status.phase_delta_uv = 0;
        g_carrier_sync.status.hold_count++;
        app_carrier_sync_publish_debug_status();
        return;
    }

    g_carrier_sync.status.phase_allow_update = 1U;
    g_carrier_sync.status.phase_control_mdeg = control_mdeg;
    g_carrier_sync.status.phase_delta_uv = next_uv - g_carrier_sync.status.control_uv;
    app_carrier_sync_apply_uv(next_uv);
    g_carrier_sync.status.update_count++;
    g_carrier_sync.status.phase_update_count++;
    app_carrier_sync_publish_debug_status();
}

/* 锁定后按频率稳定状态选择“频率闭环”或“相位闭环”。 */
void app_carrier_sync_update_iq(uint8_t locked_gate,
                                const uint16_t *i_buf,
                                const uint16_t *q_buf,
                                uint32_t sample_cnt,
                                uint16_t adc_mid,
                                const app_iq_preproc_result_t *iq_result)
{
    uint32_t now_tick;
    int32_t residual;
    int32_t abs_residual;

    /* 判断：载波同步被宏或状态关闭时直接退出，不改 DAC。 */
    if ((APP_CARRIER_SYNC_ENABLE == 0U) || (g_carrier_sync.status.enabled == 0U))
    {
        return;
    }

    /* 判断：扫频未锁定或 IQ 结果为空时，退出闭环并回到中心安全电压。 */
    if ((locked_gate == 0U) || (iq_result == NULL))
    {
        /* 判断：上一轮处于锁定门控时才执行回中心，避免未锁定阶段重复写 DAC。 */
        if (g_carrier_sync.status.locked_gate != 0U)
        {
            app_carrier_sync_reset_to_center();
        }
        g_carrier_sync.status.locked_gate = 0U;
        g_carrier_sync.status.mode = APP_CARRIER_SYNC_MODE_FREQ;
        g_carrier_sync.status.phase_takeover = 0U;
        g_carrier_sync.status.phase_allow_update = 0U;
        g_carrier_sync.status.hold_count++;
        app_carrier_sync_publish_debug_status();
        return;
    }

    now_tick = osKernelGetTickCount();
    residual = iq_result->residual_freq_millihz;
    abs_residual = app_carrier_sync_abs_i32(residual);
    g_carrier_sync.status.locked_gate = 1U;
    g_carrier_sync.status.residual_freq_millihz = residual;
    g_carrier_sync.status.phase_lock_enabled = (APP_CARRIER_PHASE_LOCK_ENABLE != 0U) ? 1U : 0U;
    g_carrier_sync.status.phase_target_mdeg = APP_CARRIER_PHASE_TARGET_MDEG;

    /* 判断：相位闭环关闭时，完全保持原来的频率闭环行为。 */
    if (APP_CARRIER_PHASE_LOCK_ENABLE == 0U)
    {
        g_carrier_sync.status.mode = APP_CARRIER_SYNC_MODE_FREQ;
        app_carrier_sync_update_frequency(now_tick, iq_result);
        return;
    }

    g_carrier_sync.status.phase_valid =
        app_carrier_sync_calc_point_phase(i_buf,
                                          q_buf,
                                          sample_cnt,
                                          adc_mid,
                                          &g_carrier_sync.status.phase_error_urad,
                                          &g_carrier_sync.status.phase_error_mdeg,
                                          &g_carrier_sync.status.phase_used_count,
                                          &g_carrier_sync.status.phase_resultant_pm);

    /* 判断：相位模式下频偏重新变大时退回频率闭环，防止相位环在失频状态继续拉 DAC。 */
    if (abs_residual > APP_CARRIER_PHASE_FREQ_PROTECT_MHZ)
    {
        g_carrier_sync.status.mode = APP_CARRIER_SYNC_MODE_FREQ;
        g_carrier_sync.status.phase_stable_count = 0U;
        g_carrier_sync.status.phase_takeover = 0U;
        app_carrier_sync_update_frequency(now_tick, iq_result);
        return;
    }

    /* 判断：尚未达到相位接管门限时，才按频偏稳定门限累计或清零；接管后只由保护门限退回。 */
    if (g_carrier_sync.status.phase_stable_count < APP_CARRIER_PHASE_STABLE_REQUIRED_COUNT)
    {
        /* 判断：残余频偏足够小，本 block 计入相位接管前稳定计数。 */
        if (abs_residual <= APP_CARRIER_PHASE_FREQ_STABLE_MHZ)
        {
            g_carrier_sync.status.phase_stable_count++;
        }
        else
        {
            g_carrier_sync.status.phase_stable_count = 0U;
        }
    }

    /* 判断：稳定计数未满时标记为等待相位接管，并继续频率闭环。 */
    if (g_carrier_sync.status.phase_stable_count < APP_CARRIER_PHASE_STABLE_REQUIRED_COUNT)
    {
        g_carrier_sync.status.mode = APP_CARRIER_SYNC_MODE_PHASE_WAIT;
        app_carrier_sync_update_frequency(now_tick, iq_result);
        return;
    }

    app_carrier_sync_update_phase(now_tick);
}

/* 兼容旧接口；没有原始 IQ block 时只能运行频率闭环。 */
void app_carrier_sync_update(uint8_t locked_gate, const app_iq_preproc_result_t *iq_result)
{
    app_carrier_sync_update_iq(locked_gate, NULL, NULL, 0U, 0U, iq_result);
}

/* 返回当前闭环状态快照。 */
void app_carrier_sync_get_status(app_carrier_sync_status_t *status_out)
{
    /* 判断：`status_out == NULL`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后直接返回/退出当前函数或返回指定结果。 */
    if (status_out == NULL)
    {
        return;
    }

    *status_out = g_carrier_sync.status;
}
