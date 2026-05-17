#ifndef APP_SWEEP_H
#define APP_SWEEP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APP_SWEEP_DEFAULT_START_HZ
#define APP_SWEEP_DEFAULT_START_HZ 29000000UL
#endif

#ifndef APP_SWEEP_DEFAULT_STOP_HZ
#define APP_SWEEP_DEFAULT_STOP_HZ 51000000UL
#endif

#ifndef APP_SWEEP_DEFAULT_STEP_HZ
#define APP_SWEEP_DEFAULT_STEP_HZ 300000UL
#endif

uint32_t app_sweep_find_center_hz(uint32_t start_hz,
                                  uint32_t stop_hz,
                                  uint32_t step_hz,
                                  const uint16_t *i_buf,
                                  const uint16_t *q_buf,
                                  uint32_t sample_cnt);
void app_sweep_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SWEEP_H */
