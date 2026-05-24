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

typedef struct
{
    analyze_mode_t mode;
    uint32_t center_hz;
    uint32_t mod_hz;
    uint32_t depth_pm;
    uint8_t done;
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
