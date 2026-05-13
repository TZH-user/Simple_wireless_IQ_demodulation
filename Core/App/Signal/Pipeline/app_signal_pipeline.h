#ifndef APP_SIGNAL_PIPELINE_H
#define APP_SIGNAL_PIPELINE_H

#include <stdint.h>

#include "app_signal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 Signal 算法总调度层。 */
void app_signal_pipeline_init(void);

/* 请求重新扫频，并复位锁定后子模块状态。 */
void app_signal_pipeline_request_rescan(void);

/* Signal 算法总入口：先扫频，再按锁定门控调度后续模块。 */
void app_signal_pipeline_process_block(const uint16_t *i_buf,
                                       const uint16_t *q_buf,
                                       uint32_t sample_cnt);

/* 获取当前扫频/锁定状态快照。 */
void app_signal_pipeline_get_status(app_signal_detect_status_t *status_out);

#ifdef __cplusplus
}
#endif

#endif /* APP_SIGNAL_PIPELINE_H */
