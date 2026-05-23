#ifndef MOD_DETECT_TASK_H
#define MOD_DETECT_TASK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    MODDETECT_RUN_IDLE = 0,
    MODDETECT_RUN_CALIBRATION,
    MODDETECT_RUN_TASK
} moddetect_run_mode_t;

typedef enum
{
    MODDETECT_CAL_NONE = 0,
    MODDETECT_CAL_HISTORY,
    MODDETECT_CAL_RUNNING,
    MODDETECT_CAL_CURRENT,
    MODDETECT_CAL_SAVING
} moddetect_cal_state_t;

typedef struct
{
    uint32_t submit_ok_cnt;
    uint32_t submit_drop_cnt;
    uint32_t queue_full_drop_cnt;
    uint32_t process_cnt;
    uint32_t last_sequence;
    uint32_t center_hz;
    uint32_t cal_clip_cnt;
    uint32_t adc_last_tick;
    moddetect_run_mode_t run_mode;
    moddetect_cal_state_t cal_state;
    uint8_t result_ready;
    uint8_t cal_valid;
    uint8_t cal_done;
    uint8_t adc_ref_ok;
} moddetect_task_stats_t;

void sweep_task_publish_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt);

void moddetect_task_request_mode(moddetect_run_mode_t mode);
moddetect_run_mode_t moddetect_task_get_mode(void);
void moddetect_task_get_stats(moddetect_task_stats_t *stats_out);

#ifdef __cplusplus
}
#endif

#endif /* MOD_DETECT_TASK_H */
