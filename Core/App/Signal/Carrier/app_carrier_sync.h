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
    uint8_t reserved;
    uint32_t update_count;           /* 实际调整 VRFE 电压的次数。 */
    uint32_t hold_count;             /* 因未锁定或死区内保持不动的次数。 */
    int32_t residual_freq_millihz;   /* 最近一次参与闭环的残余频偏，单位 mHz。 */
    int32_t control_uv;              /* 当前 VRFE 控制电压，单位 uV。 */
    uint16_t dac_code;               /* 当前写入 DAC1 的 12-bit code。 */
    int32_t last_error;              /* 最近一次 DAC 操作结果。 */
} app_carrier_sync_status_t;

/* 初始化 DAC 输出和闭环状态；默认输出配置的中心电压。 */
/* 函数跳转：调用 app_carrier_sync_init()，初始化载波同步状态并输出中心 DAC 电压。 */
void app_carrier_sync_init(void);

/* 失锁或重扫时回到中心电压，并清空本轮闭环动态状态。 */
/* 函数跳转：调用 app_carrier_sync_reset_to_center()，把载波同步控制电压复位到中心值。 */
void app_carrier_sync_reset_to_center(void);

/* 根据锁定门控和 IQ 残余频偏结果，按低速积分方式更新 VRFE。 */
/* 函数跳转：调用 app_carrier_sync_update()，根据 IQ 预处理得到的残余频偏更新 DAC 控制量。 */
void app_carrier_sync_update(uint8_t locked_gate, const app_iq_preproc_result_t *iq_result);

/* 获取闭环状态快照，供调试变量窗口、日志或 UI 读取。 */
/* 函数跳转：调用 app_carrier_sync_get_status()，复制载波同步状态，供界面或调试读取。 */
void app_carrier_sync_get_status(app_carrier_sync_status_t *status_out);

/* 条件编译判断：如果该宏已经定义，则编译下面代码块。 */
#ifdef __cplusplus
}
#endif

#endif /* APP_CARRIER_SYNC_H */
