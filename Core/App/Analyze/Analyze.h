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
    ANALYZE_MODE_ASK
} analyze_mode_t;

void analyze_start(uint32_t center_hz);
uint8_t analyze_process_block(const uint16_t *i_buf,
                              const uint16_t *q_buf,
                              uint32_t sample_cnt);

uint8_t analyze_is_done(void);
uint8_t analyze_is_active(void);
uint8_t analyze_log_is_busy(void);
void analyze_log_flush_step(uint8_t max_lines);

#ifdef __cplusplus
}
#endif

#endif /* APP_ANALYZE_H */
