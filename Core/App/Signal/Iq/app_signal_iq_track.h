#ifndef APP_SIGNAL_IQ_TRACK_H
#define APP_SIGNAL_IQ_TRACK_H

#include <stdint.h>

#include "app_adc_log.h"
#include "app_iq_preproc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* IQ 预处理总开关，1 表示启用锁定后的残余频偏分析。 */
#ifndef ADC_IQ_PREPROC_ENABLE
#define ADC_IQ_PREPROC_ENABLE 1U
#endif

/* 运行时旋转开关，0 表示只分析不改输出数据。 */
#ifndef ADC_IQ_PREPROC_ROTATE_RUNTIME_ENABLE
#define ADC_IQ_PREPROC_ROTATE_RUNTIME_ENABLE 0U
#endif

/*
 * IQ 跟踪快照。
 * 说明：
 *   - Pipeline 通过这个快照取得残余频偏分析结果和验证日志快照。
 *   - active 表示当前是否已经进入锁定后的跟踪阶段。
 *   - result_valid 表示当前 block 是否生成了新的分析结果。
 */
typedef struct
{
    uint8_t active;                             /* 1 表示锁定后跟踪已激活。 */
    uint8_t result_valid;                       /* 1 表示当前 block 产生了新的分析结果。 */
    uint16_t adc_mid;                           /* 当前使用的 ADC 中点码值。 */
    app_iq_preproc_result_t result;             /* 最近一次 IQ 预处理结果。 */
    app_adc_log_iq_verify_snapshot_t verify;    /* 最近一次验证快照。 */
} app_signal_iq_track_snapshot_t;

/* 初始化 IQ 跟踪模块。 */
void app_signal_iq_track_init(uint32_t sample_rate_hz);

/* 复位锁定后 IQ 跟踪状态。 */
void app_signal_iq_track_reset(void);

/* 在锁定状态下处理一个 IQ block，返回 1 表示产生了新的分析结果。 */
uint8_t app_signal_iq_track_process_locked_block(const uint16_t *i_buf,
                                                 const uint16_t *q_buf,
                                                 uint32_t sample_cnt);

/* 复制当前 IQ 跟踪快照。 */
void app_signal_iq_track_get_snapshot(app_signal_iq_track_snapshot_t *out);

#ifdef __cplusplus
}
#endif

#endif /* APP_SIGNAL_IQ_TRACK_H */
