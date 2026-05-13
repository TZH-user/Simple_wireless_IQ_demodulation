#ifndef APP_CARRIER_SYNC_H
#define APP_CARRIER_SYNC_H

#include <stdint.h>

#include "app_iq_preproc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 宏定义说明：APP_CARRIER_SYNC_ENABLE = 1U；载波同步总开关，1 表示允许 VRFE 频率闭环工作。 */
#ifndef APP_CARRIER_SYNC_ENABLE
#define APP_CARRIER_SYNC_ENABLE 1U
#endif

/* 宏定义说明：APP_CARRIER_SYNC_MODE_FREQ = 0U；当前仅保留频率闭环模式。 */
#define APP_CARRIER_SYNC_MODE_FREQ 0U

/*
 * 载波同步状态快照。
 * 说明：
 *   - 当前版本只保留“频率闭环 + 相位观测”。
 *   - `phase_*` 字段只用于观测，不再参与 DAC 相位接管控制。
 */
typedef struct
{
    uint8_t enabled;               /* 闭环模块是否启用。 */
    uint8_t locked_gate;           /* 当前是否处于允许闭环调节的锁定状态。 */
    uint8_t dac_started;           /* DAC1 通道是否已经启动。 */
    uint8_t mode;                  /* 当前控制模式，现阶段固定为 APP_CARRIER_SYNC_MODE_FREQ。 */
    uint32_t update_count;         /* 实际写 DAC 的总次数。 */
    uint32_t hold_count;           /* 因失锁或死区保持不动的次数。 */
    uint32_t freq_update_count;    /* 频率闭环实际调节 DAC 的次数。 */
    int32_t residual_freq_millihz; /* 当前残余频偏，单位 mHz。 */
    int32_t control_uv;            /* 当前 VRFE 控制电压，单位 uV。 */
    uint16_t dac_code;             /* 当前写入 DAC1 的 12-bit code。 */
    int32_t last_error;            /* 最近一次 DAC 操作结果，0 表示成功。 */

    uint8_t phase_valid;           /* 当前 block 的逐点相位圆均值是否可靠。 */
    uint32_t phase_used_count;     /* 本次逐点相位平均实际使用的点数。 */
    uint32_t phase_resultant_pm;   /* 相位向量集中度，单位千分比。 */
    int32_t phase_error_urad;      /* 相对 0 度目标的相位误差，单位微弧度。 */
    int32_t phase_error_mdeg;      /* 相对 0 度目标的相位误差，单位毫度。 */
    int32_t phase_abs_mdeg;        /* 当前绝对相位，单位毫度，0 度表示 +I 轴。 */

    int16_t dac_code_delta;        /* 最近一次写 DAC 时 code 的变化量。 */
    uint8_t dac_code_changed;      /* 最近一次写 DAC 时 code 是否真的变化。 */
} app_carrier_sync_status_t;

/* 调试观察用快照；STLINK 变量窗口可直接读取。 */
extern app_carrier_sync_status_t g_carrier_sync_status_dbg;

/* 初始化载波同步状态，并把 VRFE 输出到中心电压。 */
void app_carrier_sync_init(void);

/* 失锁或重扫时回到中心电压，并清空本轮观测状态。 */
void app_carrier_sync_reset_to_center(void);

/* 兼容旧入口；仅依据 residual_freq_millihz 执行频率闭环。 */
void app_carrier_sync_update(uint8_t locked_gate, const app_iq_preproc_result_t *iq_result);

/* 锁定后刷新相位观测，同时执行频率闭环。 */
void app_carrier_sync_update_iq(uint8_t locked_gate,
                                const uint16_t *i_buf,
                                const uint16_t *q_buf,
                                uint32_t sample_cnt,
                                uint16_t adc_mid,
                                const app_iq_preproc_result_t *iq_result);

/* 复制当前载波同步状态，供日志和调试读取。 */
void app_carrier_sync_get_status(app_carrier_sync_status_t *status_out);

/* 失锁路径入口：停止沿用锁定后的输入，并保持当前频率闭环策略。 */
void app_carrier_sync_on_unlock(void);

/* 锁定路径入口：处理锁定后的 IQ block，并执行频率闭环与相位观测。 */
void app_carrier_sync_process_locked_block(const uint16_t *i_buf,
                                           const uint16_t *q_buf,
                                           uint32_t sample_cnt,
                                           uint16_t adc_mid,
                                           const app_iq_preproc_result_t *iq_result);

#ifdef __cplusplus
}
#endif

#endif /* APP_CARRIER_SYNC_H */
