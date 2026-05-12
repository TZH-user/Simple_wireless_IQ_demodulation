#include "app_demod.h"

#include <string.h>

static app_demod_status_t g_demod;
static app_demod_config_t g_demod_cfg;

void app_demod_init(const app_demod_config_t *config)
{
    memset(&g_demod, 0, sizeof(g_demod));
    memset(&g_demod_cfg, 0, sizeof(g_demod_cfg));

    g_demod.mode = APP_MODDET_MODE_AM;
    g_demod.sample_rate_hz = APP_DEMOD_DEFAULT_SAMPLE_RATE_HZ;

    g_demod_cfg.sample_rate_hz = APP_DEMOD_DEFAULT_SAMPLE_RATE_HZ;
    g_demod_cfg.min_block_samples = 64U;

    if (config != NULL)
    {
        if (config->sample_rate_hz != 0U)
        {
            g_demod_cfg.sample_rate_hz = config->sample_rate_hz;
            g_demod.sample_rate_hz = config->sample_rate_hz;
        }

        if (config->min_block_samples != 0U)
        {
            g_demod_cfg.min_block_samples = config->min_block_samples;
        }
    }
}

void app_demod_set_active(uint8_t active)
{
    g_demod.active = (active != 0U) ? 1U : 0U;
    g_demod.running = 0U;
    g_demod.output_valid = 0U;
}

void app_demod_set_mode(app_mod_detect_mode_t mode)
{
    g_demod.mode = mode;
}

void app_demod_reset_output(void)
{
    g_demod.running = 0U;
    g_demod.output_valid = 0U;
    g_demod.block_count = 0U;
    g_demod.demod_lo_hz = 0U;
    g_demod.carrier_hz = 0U;
    g_demod.last_update_tick_ms = 0U;
    g_demod.dc_estimate = 0;
    g_demod.peak_abs = 0U;
    g_demod.waveform_len = 0U;
    memset(g_demod.waveform, 0, sizeof(g_demod.waveform));
}

void app_demod_get_status(app_demod_status_t *status_out)
{
    if (status_out == NULL)
    {
        return;
    }

    memcpy(status_out, &g_demod, sizeof(*status_out));
}
