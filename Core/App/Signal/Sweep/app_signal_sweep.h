#ifndef APP_SIGNAL_SWEEP_H
#define APP_SIGNAL_SWEEP_H

#include <stdint.h>

#include "app_signal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sweep 模块职责：
 *   1. 管理粗扫、细扫、锁定三个阶段。
 *   2. 按 ADC block 统计 I/Q Vpp，并据此估计载波频率。
 *   3. 通过 DDS 控制层更新本振 LO 频率。
 *
 * 设计边界：
 *   - 本模块只负责扫频和扫频结果。
 *   - 本模块不负责 IQ 预处理、VRFE 闭环、调制识别或上层业务调度。
 */

/* 初始化扫频状态机，并立刻开始首次粗扫。 */
void app_signal_sweep_init(void);

/* 请求重新扫频。 */
void app_signal_sweep_request_rescan(void);

/* 输入一个 ADC I/Q block，并推进扫频状态机。 */
void app_signal_sweep_process_block(const uint16_t *i_buf,
                                    const uint16_t *q_buf,
                                    uint32_t sample_cnt);

/* 获取当前扫频/锁定状态快照。 */
void app_signal_sweep_get_status(app_signal_detect_status_t *status_out);

#ifdef __cplusplus
}
#endif

#endif /* APP_SIGNAL_SWEEP_H */
