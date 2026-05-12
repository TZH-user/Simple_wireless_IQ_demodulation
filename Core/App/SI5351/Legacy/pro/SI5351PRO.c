#include "../../Config/app_si5351_variant.h"
#include "SI5351PRO.h"

#if APP_SI5351_SELECTED_VARIANT == APP_SI5351_VARIANT_PRO
/* pro 在 basic 之上补了强制选 PLL 的接口，但仍不暴露驱动强度控制。 */
void SI5351C_Init(void)
{
    si5351_legacy_init_common();
}

void SI5351_SetFrequency(uint8_t channel, uint32_t freq_hz)
{
    si5351_legacy_set_frequency_common(channel, freq_hz);
}

void SI5351_SetFrequency_CHOESE_PLL(uint8_t channel, uint32_t a, uint32_t p2, int force_pll)
{
    si5351_legacy_set_frequency_choose_pll_common(channel, a, p2, force_pll);
}
#endif
