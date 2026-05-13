#ifndef APP_SIGNAL_DETECT_H
#define APP_SIGNAL_DETECT_H

#include <stdint.h>

#include "app_signal_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 兼容层说明：
 *   - 当前活动主线已经迁移到 app_signal_pipeline.c/.h。
 *   - 本头文件继续保留旧接口声明，避免外部引用一次性断开。
 */

/* 初始化兼容层；内部转发到 app_signal_pipeline_init()。 */
void app_signal_detect_init(void);

/* 请求重新扫频；内部转发到 app_signal_pipeline_request_rescan()。 */
void app_signal_detect_request_rescan(void);

/* 兼容旧 block 入口；内部转发到 app_signal_pipeline_process_block()。 */
void app_signal_detect_process_block(const uint16_t *i_buf,
                                     const uint16_t *q_buf,
                                     uint32_t sample_cnt);

/* 兼容旧状态读取接口；内部转发到 app_signal_pipeline_get_status()。 */
void app_signal_detect_get_status(app_signal_detect_status_t *status_out);

#ifdef __cplusplus
}
#endif

#endif /* APP_SIGNAL_DETECT_H */
