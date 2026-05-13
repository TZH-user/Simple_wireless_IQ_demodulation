/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_CARRIER_SYNC_H
/* 宏定义说明：APP_CARRIER_SYNC_H = (无显式值)；app_carrier_sync.h 的头文件保护宏，避免重复 include。 */
#define APP_CARRIER_SYNC_H

#include <stdint.h>
#include "app_iq_preproc.h"

/* 条件编译判断：如果该宏已经定义，则编译下面代码块。 */
#ifdef __cplusplus
extern "C" {
#endif

/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_CARRIER_SYNC_ENABLE
/* 宏定义说明：APP_CARRIER_SYNC_ENABLE = 1U；载波同步总开关，1 表示编译并运行 DAC 调压同步逻辑。 */
#define APP_CARRIER_SYNC_ENABLE 1U
#endif

/* 宏定义说明：APP_CARRIER_SYNC_MODE_FREQ = 0U；闭环当前由残余频偏控制 VRFE。 */
#define APP_CARRIER_SYNC_MODE_FREQ        0U
/* 宏定义说明：APP_CARRIER_SYNC_MODE_PHASE_WAIT = 1U；频率已锁定，正在累计相位接管前的稳定计数。 */
#define APP_CARRIER_SYNC_MODE_PHASE_WAIT  1U
/* 宏定义说明：APP_CARRIER_SYNC_MODE_PHASE_LOCK = 2U；相位闭环已经接管 VRFE 微调。 */
#define APP_CARRIER_SYNC_MODE_PHASE_LOCK  2U

/*
 * OCXO VRFE 慢速闭环状态。
 * 说明：
 *   - control_uv 是当前希望输出到 VRFE 的电压，单位 uV。
 *   - dac_code 是对应的 12-bit DAC 写入值。
 *   - residual_freq_millihz 来自 IQ 预处理模块，只在锁定门控打开后更新。
 *   - last_error 为 0 表示最近一次 DAC 操作成功，负值表示 HAL_DAC 调用失败。
 */
typedef struct
{
    uint8_t enabled;                 /* 闭环模块是否启用。 */
    uint8_t locked_gate;             /* 当前是否处于允许闭环调节的锁定状态。 */
    uint8_t dac_started;             /* DAC1 通道是否已经启动。 */
    uint8_t mode;                    /* 当前闭环模式：0 频率闭环，1 等待相位接管，2 相位闭环。 */
    uint32_t update_count;           /* 实际调整 VRFE 电压的次数。 */
    uint32_t hold_count;             /* 因未锁定或死区内保持不动的次数。 */
    int32_t residual_freq_millihz;   /* 最近一次参与闭环的残余频偏，单位 mHz。 */
    int32_t control_uv;              /* 当前 VRFE 控制电压，单位 uV。 */
    uint16_t dac_code;               /* 当前写入 DAC1 的 12-bit code。 */
    int32_t last_error;              /* 最近一次 DAC 操作结果。 */
    uint8_t phase_lock_enabled;       /* 相位闭环编译/运行开关状态，1 表示允许频稳后接管。 */
    uint8_t phase_valid;              /* 当前逐点相位圆均值是否可靠。 */
    uint8_t phase_takeover;           /* 1 表示相位闭环已经接管 DAC，频率环不再叠加调节。 */
    uint8_t phase_allow_update;       /* 1 表示本次相位误差超过死区并允许更新 DAC。 */
    uint32_t freq_update_count;       /* 频率闭环实际调节 VRFE 的次数。 */
    uint32_t phase_update_count;      /* 相位闭环实际调节 VRFE 的次数。 */
    uint32_t phase_stable_count;      /* 残余频偏连续满足稳定门限的 block 计数。 */
    uint32_t phase_used_count;        /* 本次逐点相位圆均值实际使用的采样点数。 */
    uint32_t phase_resultant_pm;      /* 逐点相位圆均值一致性，单位千分比，1000 表示相位高度集中。 */
    int32_t phase_error_urad;         /* 当前相位误差，目标由 phase_target_mdeg 指定，单位微弧度。 */
    int32_t phase_error_mdeg;         /* 当前相位误差，单位毫度，便于串口直接读。 */
    int32_t phase_delta_mdeg;         /* 相邻有效 block 的相位误差变化量，单位毫度，用于判断是否正在越过目标。 */
    int32_t phase_control_mdeg;       /* 相位 DPLL 的合成控制量，等于相位误差叠加相位变化率阻尼后的结果。 */
    int32_t phase_delta_uv;           /* 最近一次相位 DPLL 输出到 VRFE 的电压增量，单位 uV。 */
    int32_t phase_target_mdeg;        /* 相位目标角，当前默认 90 度，单位毫度。 */
    int32_t phase_freq_lpf_millihz;   /* 相位接管后 residual_freq_millihz 的低通结果，单位 mHz，用于判断频率是否仍在慢漂。 */
    uint32_t phase_slew_limit_uv;      /* 当前相位 DPLL 实际采用的单次最大步进，单位 uV，近目标时会自动减小。 */
    uint32_t phase_brake_count;        /* 相位误差跨越目标后触发内部 trim 衰减的次数，用于确认是否进入减速保护。 */
    int32_t phase_abs_mdeg;           /* 当前逐点圆均值得到的绝对相位，单位毫度；0 度表示 +I 轴。 */
    int32_t phase_target_residual_millihz; /* 相位环希望制造的目标残余频偏，单位 mHz；用于让相位慢速回到目标角。 */
    int32_t phase_residual_error_millihz;  /* residual 低通值减去目标 residual 后的误差，单位 mHz；用于驱动 DAC 追踪目标频偏。 */
    uint8_t phase_far_zone;            /* 1 表示当前相位处于 ±180 度远区，方向采用保持策略防止跳变抖动。 */
    int8_t phase_direction_hold;        /* 远区方向保持值，+1 表示希望相位增加，-1 表示希望相位减小。 */
    int16_t dac_code_delta;             /* 最近一次写 DAC 时 code 的变化量；0 表示电压变量变化尚未跨过 DAC LSB。 */
    uint8_t dac_code_changed;           /* 最近一次写 DAC 时 code 是否实际变化，便于区分 uV 累计和硬件真实输出。 */
} app_carrier_sync_status_t;

/* 调试观察用快照；变量窗口可直接查看，不影响闭环内部状态。 */
extern app_carrier_sync_status_t g_carrier_sync_status_dbg;

/* 初始化 DAC 输出和闭环状态；默认输出配置的中心电压。 */
/* 函数跳转：调用 app_carrier_sync_init()，初始化载波同步状态并输出中心 DAC 电压。 */
void app_carrier_sync_init(void);

/* 失锁或重扫时回到中心电压，并清空本轮闭环动态状态。 */
/* 函数跳转：调用 app_carrier_sync_reset_to_center()，把载波同步控制电压复位到中心值。 */
void app_carrier_sync_reset_to_center(void);

/* 根据锁定门控和 IQ 残余频偏结果，按低速积分方式更新 VRFE。 */
/* 函数跳转：调用 app_carrier_sync_update()，根据 IQ 预处理得到的残余频偏更新 DAC 控制量。 */
void app_carrier_sync_update(uint8_t locked_gate, const app_iq_preproc_result_t *iq_result);

/* 根据原始 IQ block 的逐点相位圆均值，在频率稳定后切换到相位闭环。 */
void app_carrier_sync_update_iq(uint8_t locked_gate,
                                const uint16_t *i_buf,
                                const uint16_t *q_buf,
                                uint32_t sample_cnt,
                                uint16_t adc_mid,
                                const app_iq_preproc_result_t *iq_result);

/* 获取闭环状态快照，供调试变量窗口、日志或 UI 读取。 */
/* 函数跳转：调用 app_carrier_sync_get_status()，复制载波同步状态，供界面或调试读取。 */
void app_carrier_sync_get_status(app_carrier_sync_status_t *status_out);

/* 条件编译判断：如果该宏已经定义，则编译下面代码块。 */
#ifdef __cplusplus
}
#endif

#endif /* APP_CARRIER_SYNC_H */
