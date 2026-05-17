#ifndef APP_ANALYZE_H
#define APP_ANALYZE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void analyze_start(uint32_t center_hz, const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt);

#ifdef __cplusplus
}
#endif

#endif /* APP_ANALYZE_H */