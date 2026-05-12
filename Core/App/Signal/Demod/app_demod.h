#ifndef APP_DEMOD_H
#define APP_DEMOD_H

#include <stdint.h>

#include "app_mod_detect.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_DEMOD_WAVE_POINTS            160U
#define APP_DEMOD_DEFAULT_SAMPLE_RATE_HZ 3200000UL

typedef struct
{
    uint32_t sample_rate_hz;
    uint16_t min_block_samples;
    uint16_t reserved;
} app_demod_config_t;

typedef struct
{
    uint8_t active;       /* UI requests demod page entry. */
    uint8_t running;      /* Idle shell keeps this cleared. */
    uint8_t output_valid; /* Cleared while demod is detached. */
    uint8_t reserved0;

    app_mod_detect_mode_t mode; /* Reserved display mode for the page shell. */
    uint8_t reserved1[3];

    uint32_t block_count;
    uint32_t sample_rate_hz;
    uint32_t demod_lo_hz;
    uint32_t carrier_hz;
    uint32_t last_update_tick_ms;

    int32_t dc_estimate;
    uint32_t peak_abs;

    uint16_t waveform_len;
    uint16_t reserved2;

    int16_t waveform[APP_DEMOD_WAVE_POINTS];
} app_demod_status_t;

void app_demod_init(const app_demod_config_t *config);
void app_demod_set_active(uint8_t active);
void app_demod_set_mode(app_mod_detect_mode_t mode);
void app_demod_reset_output(void);
void app_demod_get_status(app_demod_status_t *status_out);

#ifdef __cplusplus
}
#endif

#endif /* APP_DEMOD_H */
