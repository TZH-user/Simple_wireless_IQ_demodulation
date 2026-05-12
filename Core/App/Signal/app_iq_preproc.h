#ifndef APP_IQ_PREPROC_H
#define APP_IQ_PREPROC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 当前 ADC 采样率：2.048 MHz */
#define APP_IQ_PREPROC_DEFAULT_SAMPLE_RATE_HZ  (2048000UL)

/* 当前双缓冲处理建议 block 点数：4096 点，对应 2 ms */
#define APP_IQ_PREPROC_DEFAULT_BLOCK_SAMPLES   (4096U)

/* 平均 IQ 向量最小幅度门限，低于该值不更新频偏估计 */
#define APP_IQ_PREPROC_MIN_MEAN_MAG            (32U)

#define APP_IQ_PREPROC_ADC_REF_MV              (3300U)   /* ADC 参考电压，单位 mV */
#define APP_IQ_PREPROC_CENTER_MV               (1650U)   /* 默认 IQ 中心电压，单位 mV */
#define APP_IQ_PREPROC_ADC_MAX_CODE            (16383U)  /* 14 位 ADC 最大码值 */

/*
 * 第一版算力策略：
 *   0 - 只分析残余频偏，rotate_block 输出数据透传
 *   1 - 启用逐点相位旋转补偿
 * 说明：
 *   H743XI/H6 在 2.048 MHz、4096 点双缓冲下，第一版建议先保持为 0，
 *   观察 residual_freq_millihz 稳定性后再决定是否打开逐点旋转。
 */
#ifndef APP_IQ_PREPROC_ENABLE_ROTATE
#define APP_IQ_PREPROC_ENABLE_ROTATE           (1U)
#endif

#define APP_IQ_PREPROC_ADC_CODE_FROM_MV(mv)                                \
    ((uint16_t)((((uint32_t)(mv) * APP_IQ_PREPROC_ADC_MAX_CODE) +          \
                  (APP_IQ_PREPROC_ADC_REF_MV / 2U)) /                     \
                 APP_IQ_PREPROC_ADC_REF_MV))

/* 默认 ADC 中点码值：1650 / 3300 * 16383 ≈ 8192 */
#define APP_IQ_PREPROC_ADC_MID                                             \
    APP_IQ_PREPROC_ADC_CODE_FROM_MV(APP_IQ_PREPROC_CENTER_MV)

typedef struct
{
    uint32_t sample_rate_hz;
    uint16_t adc_mid;
    uint16_t min_mean_mag;

    uint8_t phase_valid;
    uint8_t reserved[3];

    float phase_acc_rad;
    float residual_rad_per_sample;
    float prev_mean_i;
    float prev_mean_q;

    uint32_t block_count;
} app_iq_preproc_ctx_t;

typedef struct
{
    int32_t mean_i;
    int32_t mean_q;
    uint32_t mean_mag;
    int32_t residual_freq_millihz;
    float phase_acc_rad;
    uint32_t block_count;
} app_iq_preproc_result_t;

/*
 * 实时性观测计数器：
 *   - analyze_* 只在“分析模式”成功处理一个 block 后增加
 *   - rotate_*  只在“处理/旋转模式”成功处理一个 block 后增加
 * 说明：
 *   这些变量不参与算法判据，只用于调试器变量窗口观察处理节拍。
 */
extern volatile uint32_t app_iq_preproc_analyze_block_cnt;
extern volatile uint32_t app_iq_preproc_rotate_block_cnt;

void app_iq_preproc_init(app_iq_preproc_ctx_t *ctx, uint32_t sample_rate_hz);
void app_iq_preproc_reset(app_iq_preproc_ctx_t *ctx);

/* 运行时修改 ADC 中心电压，单位 mV */
void app_iq_preproc_set_center_mv(app_iq_preproc_ctx_t *ctx, uint32_t center_mv);

/* 运行时直接修改 ADC 中点码值 */
void app_iq_preproc_set_adc_mid(app_iq_preproc_ctx_t *ctx, uint16_t adc_mid);

/*
 * 第一版推荐接口：只分析当前 ADC DMA block，不做逐点相位旋转。
 * 适合在双缓冲 half/full 完成后，把对应半缓冲指针传入处理。
 */
uint8_t app_iq_preproc_analyze_block_u16(app_iq_preproc_ctx_t *ctx,
                                         const uint16_t *i_buf,
                                         const uint16_t *q_buf,
                                         uint32_t sample_cnt,
                                         app_iq_preproc_result_t *result_out);

/*
 * 兼容原接口。
 * 当 APP_IQ_PREPROC_ENABLE_ROTATE = 0 时，输出数据透传；
 * 当 APP_IQ_PREPROC_ENABLE_ROTATE = 1 时，输出相位旋转后的 IQ 数据。
 */
uint8_t app_iq_preproc_rotate_block_u16(app_iq_preproc_ctx_t *ctx,
                                        const uint16_t *i_buf,
                                        const uint16_t *q_buf,
                                        uint32_t sample_cnt,
                                        uint16_t *i_out,
                                        uint16_t *q_out,
                                        app_iq_preproc_result_t *result_out);

#ifdef __cplusplus
}
#endif

#endif /* APP_IQ_PREPROC_H */
