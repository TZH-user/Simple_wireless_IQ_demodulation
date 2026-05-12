#ifndef APP_PHASE_ALIGN_H
#define APP_PHASE_ALIGN_H

#include <stdint.h>

#include "app_iq_preproc.h"
#include "app_mod_detect.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 宏定义说明：APP_PHASE_ALIGN_STAGE_IDLE 表示相位归零模块空闲或未锁定。 */
#define APP_PHASE_ALIGN_STAGE_IDLE             0U
/* 宏定义说明：APP_PHASE_ALIGN_STAGE_FREQ_STABLE_WAIT 表示等待频率闭环稳定。 */
#define APP_PHASE_ALIGN_STAGE_FREQ_STABLE_WAIT 1U
/* 宏定义说明：APP_PHASE_ALIGN_STAGE_MOD_PRECHECK 表示频率稳定后等待调制初识别结果。 */
#define APP_PHASE_ALIGN_STAGE_MOD_PRECHECK     2U
/* 宏定义说明：APP_PHASE_ALIGN_STAGE_PHASE_ALIGN 表示相位环已经接管 VRFE 微调。 */
#define APP_PHASE_ALIGN_STAGE_PHASE_ALIGN      3U
/* 宏定义说明：APP_PHASE_ALIGN_STAGE_PHASE_HOLD 表示相位已接近 0 度并进入保持状态。 */
#define APP_PHASE_ALIGN_STAGE_PHASE_HOLD       4U
/* 宏定义说明：APP_PHASE_ALIGN_STAGE_MOD_RECHECK 表示相位归零后等待调制复核结果。 */
#define APP_PHASE_ALIGN_STAGE_MOD_RECHECK      5U

typedef struct
{
    uint8_t stage;                         /* 当前相位归零状态机阶段。 */
    uint8_t centroid_valid;                /* 当前 IQ 重心是否可靠，0 表示冻结相位环。 */
    uint8_t vrfe_phase_takeover;           /* 1 表示相位环接管 VRFE，频率环只做保护。 */
    uint8_t allow_vrfe_update;             /* 1 表示本次允许按相位误差更新 DAC。 */
    app_mod_detect_mode_t mode;            /* 最近一次调制识别瞬时模式。 */
    app_mod_detect_mode_t stable_mode;     /* 最近一次调制识别稳定投票模式。 */
    uint16_t stable_confidence_percent;    /* 调制稳定投票置信度，单位百分比。 */
    int32_t phase_error_urad;              /* 当前相位误差，目标为 0，单位微弧度。 */
    uint32_t phase_rms_urad;               /* 频率稳定窗口内相对起点的相位波动 RMS，单位微弧度。 */
    uint32_t centroid_mag;                 /* 当前 block 去中心后重心幅度，用于判断相位是否可信。 */
    uint32_t stable_window_ms;             /* 当前频率稳定观测窗口累计时间，单位 ms。 */
    uint32_t freeze_count;                 /* 因重心或模式不可靠而冻结相位更新的次数。 */
    uint32_t update_count;                 /* 允许相位环更新 VRFE 的次数。 */
} app_phase_align_status_t;

/* 函数跳转：调用 app_phase_align_init()，初始化频率稳定后相位归零状态机。 */
void app_phase_align_init(void);

/* 函数跳转：调用 app_phase_align_reset()，失锁或重扫时清空相位归零状态。 */
void app_phase_align_reset(void);

/* 函数跳转：调用 app_phase_align_update()，根据当前 IQ、频偏和调制识别结果更新相位归零状态。 */
void app_phase_align_update(uint8_t locked_gate,
                            const uint16_t *i_buf,
                            const uint16_t *q_buf,
                            uint32_t sample_cnt,
                            uint16_t adc_mid,
                            const app_iq_preproc_result_t *iq_result,
                            const app_mod_detect_status_t *mod_status);

/* 函数跳转：调用 app_phase_align_get_status()，读取相位归零状态快照。 */
void app_phase_align_get_status(app_phase_align_status_t *status_out);

/* 函数跳转：调用 app_phase_align_phase_takeover_active()，判断相位环是否应接管 VRFE。 */
uint8_t app_phase_align_phase_takeover_active(void);

/* 函数跳转：调用 app_phase_align_allow_vrfe_update()，判断本次相位误差是否允许更新 DAC。 */
uint8_t app_phase_align_allow_vrfe_update(void);

/* 函数跳转：调用 app_phase_align_get_phase_error_urad()，读取当前相位误差。 */
int32_t app_phase_align_get_phase_error_urad(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_PHASE_ALIGN_H */
