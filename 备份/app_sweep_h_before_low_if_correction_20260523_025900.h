#ifndef APP_SWEEP_H
#define APP_SWEEP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APP_SWEEP_DEFAULT_START_HZ
#define APP_SWEEP_DEFAULT_START_HZ 109000000UL
#endif

#ifndef APP_SWEEP_DEFAULT_STOP_HZ
#define APP_SWEEP_DEFAULT_STOP_HZ 131000000UL
#endif

#ifndef APP_SWEEP_DEFAULT_STEP_HZ
#define APP_SWEEP_DEFAULT_STEP_HZ 100000UL
#endif

typedef struct
{
    uint16_t point_count;
    uint32_t clip_count;
    uint32_t max_vpp;
    uint32_t min_vpp;
    uint8_t valid;
} app_sweep_calibration_stats_t;

uint32_t app_sweep_find_center_hz(uint32_t start_hz,
                                  uint32_t stop_hz,
                                  uint32_t step_hz,
                                  const uint16_t *i_buf,
                                  const uint16_t *q_buf,
                                  uint32_t sample_cnt);
uint8_t app_sweep_calibrate_baseline(uint32_t start_hz,
                                     uint32_t stop_hz,
                                     uint32_t step_hz,
                                     const uint16_t *i_buf,
                                     const uint16_t *q_buf,
                                     uint32_t sample_cnt);
void app_sweep_get_calibration_stats(app_sweep_calibration_stats_t *stats_out);
uint8_t app_sweep_is_done(void);
void app_sweep_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SWEEP_H */
