#ifndef APP_ANALYZE_H
#define APP_ANALYZE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    ANALYZE_MODE_UNKNOWN = 0,
    ANALYZE_MODE_CW,
    ANALYZE_MODE_AM,
    ANALYZE_MODE_ASK,
    ANALYZE_MODE_FM,
    ANALYZE_MODE_MIXED,
    ANALYZE_MODE_FSK,
    ANALYZE_MODE_PSK
} analyze_mode_t;

#define ANALYZE_PARAM_AM_DEPTH_VALID       (1U << 0)
#define ANALYZE_PARAM_ASK_DEPTH_VALID      (1U << 1)
#define ANALYZE_PARAM_FM_DEVIATION_VALID   (1U << 2)
#define ANALYZE_PARAM_FSK_SEPARATION_VALID (1U << 3)
#define ANALYZE_PARAM_SYMBOL_RATE_VALID    (1U << 4)

typedef struct
{
    analyze_mode_t mode;
    uint32_t center_hz;
    uint32_t mod_hz;   /* 兼容旧接口：AM/FM 为调制频率；FSK 暂保留频差。 */
    uint32_t depth_pm; /* 兼容旧接口：包络深度，单位千分比。 */
    int32_t low_if_hz;
    uint8_t done;
    uint32_t am_depth_pm;
    uint32_t ask_depth_pm;
    uint32_t fm_deviation_hz;
    uint32_t fsk_separation_hz;
    uint32_t symbol_rate_hz;
    uint16_t param_valid_mask;
    uint16_t param_confidence_pm;
} analyze_result_t;

void analyze_start(uint32_t center_hz);
uint8_t analyze_process_block(const uint16_t *i_buf,
                              const uint16_t *q_buf,
                              uint32_t sample_cnt);

void analyze_get_result(analyze_result_t *result_out);
uint8_t analyze_is_done(void);
uint8_t analyze_is_active(void);
uint8_t analyze_log_is_busy(void);
void analyze_log_flush_step(uint8_t max_lines);

#ifdef __cplusplus
}
#endif

#endif /* APP_ANALYZE_H */
