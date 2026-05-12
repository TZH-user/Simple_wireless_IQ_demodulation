#ifndef APP_SIGNAL_DETECT_H
#define APP_SIGNAL_DETECT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Detection scan profile:
 *   0: lab profile, valid carrier 30~50 MHz, LO sweep 29~51 MHz
 *   1: contest profile, valid carrier 110~130 MHz, LO sweep 109~131 MHz
 */
#ifndef APP_SIGDET_SCAN_PROFILE
#define APP_SIGDET_SCAN_PROFILE               0U
#endif

#ifndef APP_SIGDET_DDS_CHANNEL
#define APP_SIGDET_DDS_CHANNEL                0U
#endif

#if !defined(APP_SIGDET_VALID_START_HZ) || !defined(APP_SIGDET_VALID_STOP_HZ) || \
    !defined(APP_SIGDET_SCAN_START_HZ) || !defined(APP_SIGDET_SCAN_STOP_HZ)
#if (APP_SIGDET_SCAN_PROFILE == 0U)
#define APP_SIGDET_VALID_START_HZ             30000000UL
#define APP_SIGDET_VALID_STOP_HZ              50000000UL
#define APP_SIGDET_SCAN_START_HZ              29000000UL
#define APP_SIGDET_SCAN_STOP_HZ               51000000UL
#elif (APP_SIGDET_SCAN_PROFILE == 1U)
#define APP_SIGDET_VALID_START_HZ             110000000UL
#define APP_SIGDET_VALID_STOP_HZ              130000000UL
#define APP_SIGDET_SCAN_START_HZ              109000000UL
#define APP_SIGDET_SCAN_STOP_HZ               131000000UL
#else
#error "Unsupported APP_SIGDET_SCAN_PROFILE"
#endif
#endif

/* Coarse Vpp sweep parameters. Each point uses half-DMA ADC blocks. */
#ifndef APP_SIGDET_SCAN_STEP_HZ
#define APP_SIGDET_SCAN_STEP_HZ               300000UL
#endif

/* Fine sweep around coarse estimate: [coarse - span, coarse + span]. */
#ifndef APP_SIGDET_FINE_SPAN_HZ
#define APP_SIGDET_FINE_SPAN_HZ               1200000UL
#endif

#ifndef APP_SIGDET_FINE_STEP_HZ
#define APP_SIGDET_FINE_STEP_HZ               50000UL
#endif

#ifndef APP_SIGDET_DWELL_BLOCKS_PER_STEP
#define APP_SIGDET_DWELL_BLOCKS_PER_STEP      2U
#endif

/* Minimum raw-ADC Vpp rise required to treat a sweep response as valid. */
#ifndef APP_SIGDET_VPP_MIN_RISE_RAW
#define APP_SIGDET_VPP_MIN_RISE_RAW           80UL
#endif

/* Active threshold = valley + max((peak - valley) / 4, APP_SIGDET_VPP_MIN_RISE_RAW). */
#ifndef APP_SIGDET_VPP_THRESHOLD_SHIFT
#define APP_SIGDET_VPP_THRESHOLD_SHIFT        2U
#endif

/* Temporary serial trace for plotting Vpp response:
 * vpp_stage,coarse|fine
 * vpp_begin,start_hz,stop_hz,step_hz,dwell_blocks
 * vpp,lo_hz,total_vpp,i_vpp,q_vpp
 */
#ifndef APP_SIGDET_VPP_UART_TRACE_ENABLE
#define APP_SIGDET_VPP_UART_TRACE_ENABLE      0U
#endif

#define APP_SIGDET_STAGE_VPP_COARSE_SCAN      0U
#define APP_SIGDET_STAGE_VPP_FINE_SCAN        1U
#define APP_SIGDET_STAGE_VPP_LOCKED           2U
#define APP_SIGDET_STAGE_VPP_SCAN             APP_SIGDET_STAGE_VPP_COARSE_SCAN

typedef struct
{
  uint8_t scanning;
  uint8_t carrier_present;
  uint8_t stage;
  uint8_t locked;
  uint32_t current_lo_hz;
  uint32_t estimated_carrier_hz;
  uint32_t current_vpp_raw;
  uint32_t current_i_vpp_raw;
  uint32_t current_q_vpp_raw;
  uint32_t peak_vpp_raw;
  uint32_t valley_vpp_raw;
  uint32_t threshold_vpp_raw;
  uint32_t left_edge_hz;
  uint32_t right_edge_hz;
  uint32_t scan_start_tick_ms;
  uint32_t scan_finish_tick_ms;
  uint16_t step_index;
  uint16_t step_count;
  uint16_t valley_step_index;
  uint16_t peak_step_index;
} app_signal_detect_status_t;

void app_signal_detect_init(void);
void app_signal_detect_request_rescan(void);
void app_signal_detect_process_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt);
void app_signal_detect_get_status(app_signal_detect_status_t *status_out);

#ifdef __cplusplus
}
#endif

#endif /* APP_SIGNAL_DETECT_H */

