#include "../../Config/app_si5351_variant.h"
#include "SI5351PROMAX.h"

#if APP_SI5351_SELECTED_VARIANT == APP_SI5351_VARIANT_PROMAX
/* promax 直接暴露相位、驱动、反相和状态读取能力，供包装层做完整配置。 */
void SI5351C_Init(void)
{
    si5351_legacy_init_common();
}

void SI5351_SetClockSource(uint8_t channel, SI5351_Source_t source)
{
    si5351_legacy_set_clock_source_common(channel, source);
}

void SI5351_SetPLLxSource(char pll_x, uint8_t use_clkin)
{
    si5351_legacy_set_pll_source_common(pll_x, use_clkin);
}

bool SI5351_IsExternalClockPresent(void)
{
    return si5351_legacy_is_external_clock_present_common();
}

void SI5351_SetFrequency(uint8_t channel, uint32_t freq_hz)
{
    si5351_legacy_set_frequency_common(channel, freq_hz);
}

void SI5351_SetFrequency_CHOESE_PLL(uint8_t channel, uint32_t a, uint32_t p2, int force_pll)
{
    si5351_legacy_set_frequency_choose_pll_common(channel, a, p2, force_pll);
}

void SI5351_SetPhaseOffset(uint8_t channel, double degrees)
{
    si5351_legacy_set_phase_offset_common(channel, degrees);
}

uint8_t SI5351_GetPhaseOffsetRaw(uint8_t channel)
{
    return si5351_legacy_get_phase_offset_raw_common(channel);
}

double SI5351_GetPhaseOffsetDegrees(uint8_t channel)
{
    return si5351_legacy_get_phase_offset_degrees_common(channel);
}

void SI5351_SetPhaseAndEnable(uint8_t channel, double degrees)
{
    si5351_legacy_set_phase_and_enable_common(channel, degrees);
}

void SI5351_SetDriveStrength(uint8_t channel, SI5351_Drive_t strength)
{
    si5351_legacy_set_drive_strength_common(channel, strength);
}

uint8_t SI5351_GetDriveStrength(uint8_t channel)
{
    return si5351_legacy_get_drive_strength_common(channel);
}

void SI5351_ReadStatus(SI5351_Status *status)
{
    si5351_legacy_read_status_common(status);
}

void SI5351_SetInvert(uint8_t channel, bool invert)
{
    si5351_legacy_set_invert_common(channel, invert);
}

bool SI5351_GetInvert(uint8_t channel)
{
    return si5351_legacy_get_invert_common(channel);
}

void SI5351_EnableChannel(uint8_t channel, bool enable)
{
    si5351_legacy_enable_channel_common(channel, enable);
}

uint8_t SI5351_GetClockControlReg(uint8_t channel)
{
    return si5351_legacy_get_clock_control_reg_common(channel);
}
#endif
