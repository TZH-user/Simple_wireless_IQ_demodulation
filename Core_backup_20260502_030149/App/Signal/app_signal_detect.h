#ifndef APP_SIGNAL_DETECT_H
#define APP_SIGNAL_DETECT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Detection scan profile:
 *   0: lab profile, 30~50 MHz (current source limit <= 60 MHz)
 *   1: contest profile, 110~130 MHz
 *
 * For later range switching, only change APP_SIGDET_SCAN_PROFILE.
 */
#ifndef APP_SIGDET_SCAN_PROFILE
#define APP_SIGDET_SCAN_PROFILE          0U
#endif

#ifndef APP_SIGDET_DDS_CHANNEL
#define APP_SIGDET_DDS_CHANNEL           0U
#endif

#if !defined(APP_SIGDET_SCAN_START_HZ) || !defined(APP_SIGDET_SCAN_STOP_HZ)
#if (APP_SIGDET_SCAN_PROFILE == 0U)
#define APP_SIGDET_SCAN_START_HZ         30000000UL
#define APP_SIGDET_SCAN_STOP_HZ          50000000UL
#elif (APP_SIGDET_SCAN_PROFILE == 1U)
#define APP_SIGDET_SCAN_START_HZ         110000000UL
#define APP_SIGDET_SCAN_STOP_HZ          130000000UL
#else
#error "Unsupported APP_SIGDET_SCAN_PROFILE"
#endif
#endif

#ifndef APP_SIGDET_SCAN_STEP_HZ
#define APP_SIGDET_SCAN_STEP_HZ          50000UL
#endif

#ifndef APP_SIGDET_DWELL_BLOCKS_PER_STEP
#define APP_SIGDET_DWELL_BLOCKS_PER_STEP 2U
#endif

typedef struct
{
  uint8_t scanning;
  uint8_t carrier_present;
  uint16_t reserved;
  uint32_t current_lo_hz;
  uint32_t estimated_carrier_hz;
  uint32_t best_metric;
  uint32_t noise_metric;
  uint32_t scan_start_tick_ms;
  uint32_t scan_finish_tick_ms;
  uint16_t step_index;
  uint16_t step_count;
} app_signal_detect_status_t;

void app_signal_detect_init(void);
void app_signal_detect_request_rescan(void);
void app_signal_detect_process_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt);
void app_signal_detect_get_status(app_signal_detect_status_t *status_out);

#ifdef __cplusplus
}
#endif

#endif /* APP_SIGNAL_DETECT_H */
