#ifndef MOD_DETECT_TASK_H
#define MOD_DETECT_TASK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t submit_ok_cnt;
    uint32_t submit_drop_cnt;
    uint32_t queue_full_drop_cnt;
    uint32_t process_cnt;
    uint32_t last_sequence;
} moddetect_task_stats_t;

uint32_t moddetect_task_submit_block(const uint16_t *i_buf,
                                     const uint16_t *q_buf,
                                     uint32_t sample_cnt,
                                     uint32_t demod_lo_hz,
                                     uint32_t carrier_hz,
                                     uint32_t tick_ms);

void moddetect_task_get_stats(moddetect_task_stats_t *stats_out);

#ifdef __cplusplus
}
#endif

#endif /* MOD_DETECT_TASK_H */
