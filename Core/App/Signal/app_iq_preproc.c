#include "app_iq_preproc.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* 常用圆周率常量 */
#define APP_IQ_PREPROC_PI              (3.14159265359f)
#define APP_IQ_PREPROC_TWO_PI          (6.28318530718f)

/* 残余频偏一阶 IIR 平滑系数：1 / 2^6 = 1 / 64
 * 说明：
 *   当前残余频偏只有几百 mHz，环路不需要很快。
 *   系数放慢可以降低 AM / ASK / PSK / FSK / FM 调制内容对频偏估计的影响。
 */
#define APP_IQ_PREPROC_SMOOTH_SHIFT    (6U)
#define APP_IQ_PREPROC_SMOOTH_DIVISOR  ((float)(1UL << APP_IQ_PREPROC_SMOOTH_SHIFT))

/* 当前 block 的平均 IQ 向量 */
typedef struct
{
    int32_t i;
    int32_t q;
    uint32_t mag;
} app_iq_mean_t;

volatile uint32_t app_iq_preproc_analyze_block_cnt = 0U;
volatile uint32_t app_iq_preproc_analyze_sample_cnt = 0U;
volatile uint32_t app_iq_preproc_rotate_block_cnt = 0U;
volatile uint32_t app_iq_preproc_rotate_sample_cnt = 0U;

/*
 * 将角度限制到 [-pi, pi] 范围内。
 * 作用：
 *   计算相位差和相位累加时，避免 +pi / -pi 跳变造成相位突变。
 */
static float wrap_pi(float angle)
{
    while (angle > APP_IQ_PREPROC_PI)
    {
        angle -= APP_IQ_PREPROC_TWO_PI;
    }

    while (angle < -APP_IQ_PREPROC_PI)
    {
        angle += APP_IQ_PREPROC_TWO_PI;
    }

    return angle;
}

/*
 * 将 float 数值限制到 ADC 输出范围。
 * 说明：
 *   - 小于 0 的值限制为 0
 *   - 大于 ADC 最大码值的值限制为 ADC 最大码值
 *   - 中间值通过 +0.5f 实现四舍五入
 */
static uint16_t clamp_adc_code(float value)
{
    if (value <= 0.0f)
    {
        return 0U;
    }

    if (value >= (float)APP_IQ_PREPROC_ADC_MAX_CODE)
    {
        return (uint16_t)APP_IQ_PREPROC_ADC_MAX_CODE;
    }

    return (uint16_t)(value + 0.5f);
}

/* 获取有效采样率；输入为 0 时使用默认采样率 */
static uint32_t default_sample_rate(uint32_t sample_rate_hz)
{
    return (sample_rate_hz != 0U) ? sample_rate_hz : APP_IQ_PREPROC_DEFAULT_SAMPLE_RATE_HZ;
}

/* 将中心电压转换为 ADC 码值；输入电压超过参考电压时先做限制 */
static uint16_t adc_code_from_mv(uint32_t mv)
{
    if (mv > APP_IQ_PREPROC_ADC_REF_MV)
    {
        mv = APP_IQ_PREPROC_ADC_REF_MV;
    }

    return APP_IQ_PREPROC_ADC_CODE_FROM_MV(mv);
}

/* 检查分析接口参数是否合法 */
static int analyze_args_are_valid(const app_iq_preproc_ctx_t *ctx,
                                  const uint16_t *i_buf,
                                  const uint16_t *q_buf,
                                  uint32_t sample_cnt)
{
    return (ctx != NULL) &&
           (i_buf != NULL) &&
           (q_buf != NULL) &&
           (sample_cnt != 0U);
}

/* 检查旋转接口参数是否合法 */
static int rotate_args_are_valid(const app_iq_preproc_ctx_t *ctx,
                                 const uint16_t *i_buf,
                                 const uint16_t *q_buf,
                                 uint32_t sample_cnt,
                                 const uint16_t *i_out,
                                 const uint16_t *q_out)
{
    return analyze_args_are_valid(ctx, i_buf, q_buf, sample_cnt) &&
           (i_out != NULL) &&
           (q_out != NULL);
}

/*
 * 计算当前 block 的 I/Q 平均值。
 * 输出：
 *   平均 I、平均 Q，以及平均 IQ 向量幅度。
 * 注意：
 *   mean.i / mean.q 已经减去了 ADC 中点。
 */
static app_iq_mean_t calc_block_mean(const app_iq_preproc_ctx_t *ctx,
                                     const uint16_t *i_buf,
                                     const uint16_t *q_buf,
                                     uint32_t sample_cnt)
{
    uint64_t sum_i = 0ULL;
    uint64_t sum_q = 0ULL;
    uint32_t idx;
    int64_t mag_sq;
    app_iq_mean_t mean;

    /* 双缓冲模式下，本函数只读当前半缓冲/整缓冲，不保存输入指针 */
    for (idx = 0U; idx < sample_cnt; idx++)
    {
        sum_i += i_buf[idx];
        sum_q += q_buf[idx];
    }

    /* 计算均值，并去除 ADC 中点 */
    mean.i = (int32_t)(sum_i / sample_cnt) - (int32_t)ctx->adc_mid;
    mean.q = (int32_t)(sum_q / sample_cnt) - (int32_t)ctx->adc_mid;

    /* 每个 block 只做一次 sqrtf，第一版先保留，后续再按 H743 负载决定是否改成平方门限 */
    mag_sq = ((int64_t)mean.i * (int64_t)mean.i) +
             ((int64_t)mean.q * (int64_t)mean.q);
    mean.mag = (uint32_t)(sqrtf((float)mag_sq) + 0.5f);

    return mean;
}

/* 判断当前 block 的平均幅度是否足够用于相位估计 */
/* 锁定后绝对相位对齐到 +I 轴；以 block 中心相位为锚点，反推出首点旋转相位。 */
/* 锁定后绝对相位对齐到 +I 轴，使旋转输出的平均 Q 分量趋近 0。 */
/* 锁定后由 I/Q 两路去中心幅度计算相位 atan2(Q, I)，再把该相位旋到 0。 */
static int mean_is_valid(const app_iq_preproc_ctx_t *ctx, const app_iq_mean_t *mean)
{
    return mean->mag >= (uint32_t)ctx->min_mean_mag;
}

/*
 * 根据相邻 block 的平均相位差，更新残余频偏估计。
 * 处理流程：
 *   1. 当前平均 IQ 幅度过低时，不更新频偏
 *   2. 当前 block 与上一有效 block 做相位差
 *   3. 将相位差转换为每采样点相位变化量
 *   4. 使用一阶 IIR 低带宽平滑，得到慢速残余频偏
 */
static void update_residual_frequency(app_iq_preproc_ctx_t *ctx,
                                      const app_iq_mean_t *mean,
                                      uint32_t sample_cnt)
{
    float curr_phase;
    float prev_phase;
    float delta_phase;
    float inst_rad_per_sample;

    if (!mean_is_valid(ctx, mean))
    {
        return;
    }

    if (ctx->phase_valid != 0U)
    {
        /* 当前平均相位和上一有效平均相位 */
        curr_phase = atan2f((float)mean->q, (float)mean->i);
        prev_phase = atan2f(ctx->prev_mean_q, ctx->prev_mean_i);

        /* 相位差限制到 [-pi, pi]，避免跨 pi 跳变 */
        delta_phase = wrap_pi(curr_phase - prev_phase);

        /* 换算为每个采样点的相位步进 */
        inst_rad_per_sample = delta_phase / (float)sample_cnt;

        /* 一阶 IIR 平滑残余频偏估计 */
        ctx->residual_rad_per_sample +=
            (inst_rad_per_sample - ctx->residual_rad_per_sample) /
            APP_IQ_PREPROC_SMOOTH_DIVISOR;
    }

    /* 更新上一有效 block 的平均 IQ 信息 */
    ctx->prev_mean_i = (float)mean->i;
    ctx->prev_mean_q = (float)mean->q;
    ctx->phase_valid = 1U;
}

/*
 * 第一版默认只做分析，不做逐点旋转。
 * 说明：
 *   H743XI/H6 在 2.048 MHz、4096 点双缓冲下，每 2 ms 进入一次处理窗口。
 *   为了先确认残余频偏估计是否稳定，默认只输出诊断结果，输出数据直接透传。
 */
#if (APP_IQ_PREPROC_ENABLE_ROTATE == 0U)
static void copy_samples_if_needed(const uint16_t *i_buf,
                                   const uint16_t *q_buf,
                                   uint32_t sample_cnt,
                                   uint16_t *i_out,
                                   uint16_t *q_out)
{
    if (i_out != i_buf)
    {
        memcpy(i_out, i_buf, sample_cnt * sizeof(uint16_t));
    }

    if (q_out != q_buf)
    {
        memcpy(q_out, q_buf, sample_cnt * sizeof(uint16_t));
    }
}
#endif

#if (APP_IQ_PREPROC_ENABLE_ROTATE != 0U)
/*
 * 对当前 block 的每个 IQ 点执行相位旋转。
 * 说明：
 *   使用 cos/sin 递推，避免每个采样点都调用 sinf/cosf。
 *   该路径计算量明显大于分析路径，第一版默认关闭。
 */
static void rotate_samples(const app_iq_preproc_ctx_t *ctx,
                           float phase0,
                           const uint16_t *i_buf,
                           const uint16_t *q_buf,
                           uint32_t sample_cnt,
                           uint16_t *i_out,
                           uint16_t *q_out)
{
    float c = cosf(phase0);
    float s = sinf(phase0);
    float c_step = cosf(ctx->residual_rad_per_sample);
    float s_step = sinf(ctx->residual_rad_per_sample);
    uint32_t idx;

    for (idx = 0U; idx < sample_cnt; idx++)
    {
        /* 去除 ADC 中点，得到以 0 为中心的 IQ 数据 */
        float i = (float)((int32_t)i_buf[idx] - (int32_t)ctx->adc_mid);
        float q = (float)((int32_t)q_buf[idx] - (int32_t)ctx->adc_mid);

        /* 计算下一点的旋转因子 */
        float next_c = (c * c_step) - (s * s_step);
        float next_s = (s * c_step) + (c * s_step);

        /* IQ 相位旋转，并加回 ADC 中点 */
        i_out[idx] = clamp_adc_code((c * i) + (s * q) + (float)ctx->adc_mid);
        q_out[idx] = clamp_adc_code((-s * i) + (c * q) + (float)ctx->adc_mid);

        /* 更新递推旋转因子 */
        c = next_c;
        s = next_s;
    }
}
#endif

/* 将每采样点相位步进转换为残余频偏，单位 mHz */
static int32_t residual_freq_millihz(const app_iq_preproc_ctx_t *ctx)
{
    return (int32_t)((ctx->residual_rad_per_sample *
                      (float)ctx->sample_rate_hz *
                      1000.0f) /
                     APP_IQ_PREPROC_TWO_PI);
}

/* 填充处理结果和诊断信息；result 为 NULL 时不输出 */
static void fill_result(const app_iq_preproc_ctx_t *ctx,
                        const app_iq_mean_t *mean,
                        app_iq_preproc_result_t *result)
{
    if (result == NULL)
    {
        return;
    }

    result->mean_i = mean->i;
    result->mean_q = mean->q;
    result->mean_mag = mean->mag;
    result->residual_freq_millihz = residual_freq_millihz(ctx);
    result->phase_acc_rad = ctx->phase_acc_rad;
    result->block_count = ctx->block_count;
}

/*
 * 处理一个 block 的公共流程。
 * 作用：
 *   1. 计算 block 平均 IQ
 *   2. 更新残余频偏估计
 *   3. 按需执行透传或相位旋转
 *   4. 更新相位累加器和诊断信息
 */
static uint8_t process_block(app_iq_preproc_ctx_t *ctx,
                             const uint16_t *i_buf,
                             const uint16_t *q_buf,
                             uint32_t sample_cnt,
                             uint16_t *i_out,
                             uint16_t *q_out,
                             app_iq_preproc_result_t *result_out,
                             uint8_t need_output)
{
    float phase0;
    app_iq_mean_t mean;

    /* 计算当前 block 平均 IQ 向量 */
    mean = calc_block_mean(ctx, i_buf, q_buf, sample_cnt);

    /* 根据平均相位变化更新残余频偏估计 */
    update_residual_frequency(ctx, &mean, sample_cnt);

    /* 保存当前 block 起始相位 */
    phase0 = ctx->phase_acc_rad;

    if (need_output != 0U)
    {
#if (APP_IQ_PREPROC_ENABLE_ROTATE != 0U)
        /* 若后续确认算力允许，可打开该宏启用逐点相位旋转 */
        rotate_samples(ctx, phase0, i_buf, q_buf, sample_cnt, i_out, q_out);
#else
        /* 第一版默认透传，避免在 ADC 双缓冲回调周期内引入过高计算量 */
        copy_samples_if_needed(i_buf, q_buf, sample_cnt, i_out, q_out);
#endif
    }

    /* 更新相位累加器；即使第一版不旋转，也持续累计用于观察稳定性 */
    ctx->phase_acc_rad = wrap_pi(phase0 + (ctx->residual_rad_per_sample * (float)sample_cnt));

    /* block 计数增加 */
    ctx->block_count++;

    /* 输出处理结果 */
    fill_result(ctx, &mean, result_out);

    return 1U;
}

/*
 * 初始化 IQ 预处理上下文。
 * 作用：
 *   - 清空上下文
 *   - 设置采样率
 *   - 设置 ADC 中点
 *   - 设置最小平均幅度门限
 */
void app_iq_preproc_init(app_iq_preproc_ctx_t *ctx, uint32_t sample_rate_hz)
{
    if (ctx == NULL)
    {
        return;
    }

    /* 清空全部状态 */
    memset(ctx, 0, sizeof(*ctx));

    /* 设置默认配置 */
    ctx->sample_rate_hz = default_sample_rate(sample_rate_hz);
    ctx->adc_mid = APP_IQ_PREPROC_ADC_MID;
    ctx->min_mean_mag = APP_IQ_PREPROC_MIN_MEAN_MAG;
}

/*
 * 复位 IQ 预处理上下文。
 * 与 init 的区别：
 *   reset 会保留采样率、ADC 中点和最小平均幅度门限，
 *   仅清空相位、频偏估计、block 计数等动态状态。
 */
void app_iq_preproc_reset(app_iq_preproc_ctx_t *ctx)
{
    uint32_t sample_rate_hz;
    uint16_t adc_mid;
    uint16_t min_mean_mag;

    if (ctx == NULL)
    {
        return;
    }

    /* 保存配置参数 */
    sample_rate_hz = ctx->sample_rate_hz;
    adc_mid = ctx->adc_mid;
    min_mean_mag = ctx->min_mean_mag;

    /* 清空动态状态 */
    memset(ctx, 0, sizeof(*ctx));

    /* 恢复配置参数；若配置为 0，则恢复默认值 */
    ctx->sample_rate_hz = default_sample_rate(sample_rate_hz);
    ctx->adc_mid = (adc_mid != 0U) ? adc_mid : APP_IQ_PREPROC_ADC_MID;
    ctx->min_mean_mag = (min_mean_mag != 0U) ? min_mean_mag : APP_IQ_PREPROC_MIN_MEAN_MAG;
}

/* 设置 IQ 中心电压，单位 mV */
void app_iq_preproc_set_center_mv(app_iq_preproc_ctx_t *ctx, uint32_t center_mv)
{
    if (ctx != NULL)
    {
        ctx->adc_mid = adc_code_from_mv(center_mv);
    }
}

/* 直接设置 ADC 中点码值 */
void app_iq_preproc_set_adc_mid(app_iq_preproc_ctx_t *ctx, uint16_t adc_mid)
{
    if (ctx != NULL)
    {
        ctx->adc_mid = adc_mid;
    }
}

/*
 * 第一版分析接口。
 * 输入：
 *   ctx        - IQ 预处理上下文
 *   i_buf      - 输入 I 路采样数据，通常来自 ADC DMA 半缓冲或全缓冲
 *   q_buf      - 输入 Q 路采样数据，通常来自 ADC DMA 半缓冲或全缓冲
 *   sample_cnt - 当前 block 中的采样点数，例如 4096
 * 输出：
 *   result_out - 可选的处理结果/诊断信息
 * 返回值：
 *   1U - 分析成功
 *   0U - 参数错误
 * 核心功能：
 *   1. 计算当前 block 的 I/Q 平均值
 *   2. 根据相邻 block 的平均相位变化估计残余频偏
 *   3. 更新相位累加器
 *   4. 输出 residual_freq_millihz 供后续判断是否需要打开旋转补偿
 */
uint8_t app_iq_preproc_analyze_block_u16(app_iq_preproc_ctx_t *ctx,
                                         const uint16_t *i_buf,
                                         const uint16_t *q_buf,
                                         uint32_t sample_cnt,
                                         app_iq_preproc_result_t *result_out)
{
    if (!analyze_args_are_valid(ctx, i_buf, q_buf, sample_cnt))
    {
        return 0U;
    }

    if (process_block(ctx, i_buf, q_buf, sample_cnt, NULL, NULL, result_out, 0U) == 0U)
    {
        return 0U;
    }

    app_iq_preproc_analyze_block_cnt++;

    return 1U;
}

/*
 * 对一个 block 的 uint16_t IQ 数据进行预处理。
 * 输入：
 *   ctx        - IQ 预处理上下文
 *   i_buf      - 输入 I 路采样数据
 *   q_buf      - 输入 Q 路采样数据
 *   sample_cnt - 当前 block 中的采样点数
 * 输出：
 *   i_out      - 输出 I 路采样数据
 *   q_out      - 输出 Q 路采样数据
 *   result_out - 可选的处理结果/诊断信息
 * 返回值：
 *   1U - 处理成功
 *   0U - 参数错误
 * 第一版说明：
 *   默认 APP_IQ_PREPROC_ENABLE_ROTATE = 0，只做频偏分析，输出数据透传。
 *   后续确认 H743XI/H6 算力余量后，可将该宏改为 1，启用逐点相位旋转。
 */
uint8_t app_iq_preproc_rotate_block_u16(app_iq_preproc_ctx_t *ctx,
                                        const uint16_t *i_buf,
                                        const uint16_t *q_buf,
                                        uint32_t sample_cnt,
                                        uint16_t *i_out,
                                        uint16_t *q_out,
                                        app_iq_preproc_result_t *result_out)
{
    if (!rotate_args_are_valid(ctx, i_buf, q_buf, sample_cnt, i_out, q_out))
    {
        return 0U;
    }

    if (process_block(ctx, i_buf, q_buf, sample_cnt, i_out, q_out, result_out, 1U) == 0U)
    {
        return 0U;
    }

    app_iq_preproc_rotate_block_cnt++;
    app_iq_preproc_rotate_sample_cnt += sample_cnt;

    return 1U;
}
