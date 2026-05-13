#include "app_signal_detect.h"

#include "app_signal_pipeline.h"

/* 初始化兼容层；实际初始化工作由 Pipeline 总调度层完成。 */
void app_signal_detect_init(void)
{
    /* 跳转到新的 Signal Pipeline 初始化入口。 */
    app_signal_pipeline_init();
}

/* 请求重新扫频；实际复位工作由 Pipeline 总调度层完成。 */
void app_signal_detect_request_rescan(void)
{
    /* 跳转到新的 Signal Pipeline 重扫入口。 */
    app_signal_pipeline_request_rescan();
}

/* 兼容旧 block 入口；实际总调度工作由 Pipeline 完成。 */
void app_signal_detect_process_block(const uint16_t *i_buf,
                                     const uint16_t *q_buf,
                                     uint32_t sample_cnt)
{
    /* 跳转到新的 Signal Pipeline block 入口。 */
    app_signal_pipeline_process_block(i_buf, q_buf, sample_cnt);
}

/* 兼容旧状态读取接口；当前状态快照仍来自 Sweep。 */
void app_signal_detect_get_status(app_signal_detect_status_t *status_out)
{
    /* 跳转到新的 Signal Pipeline 状态读取入口。 */
    app_signal_pipeline_get_status(status_out);
}
