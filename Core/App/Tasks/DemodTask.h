#ifndef DEMOD_TASK_H
#define DEMOD_TASK_H

#include <stdint.h>
#include "Analyze.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    DEMOD_STATE_IDLE = 0,
    DEMOD_STATE_RUNNING
} demod_state_t;

typedef struct
{
    demod_state_t  state;
    analyze_mode_t mode;
    uint32_t       center_hz;
    uint32_t       mod_hz;
    uint32_t       depth_pm;
    int32_t        low_if_hz;
    uint32_t       block_cnt;
    uint32_t       sample_cnt;
} demod_task_stats_t;

/* 由 AdcTask 调用，将 ADC 数据块指针发布给解调任务。 */
void demod_task_publish_block(const uint16_t *i_buf,
                              const uint16_t *q_buf,
                              uint32_t        sample_cnt);

/* ModDetectTask 在 Analyze 完成后调用，传入分析结果启动解调。 */
void demod_task_start_with_result(const analyze_result_t *result);

/* UI 或其他任务手动停止解调。 */
void demod_task_stop(void);

/* 读取解调任务当前统计信息，供 UI 轮询。 */
void demod_task_get_stats(demod_task_stats_t *stats_out);

/* 查询解调是否正在运行。 */
uint8_t demod_task_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* DEMOD_TASK_H */
