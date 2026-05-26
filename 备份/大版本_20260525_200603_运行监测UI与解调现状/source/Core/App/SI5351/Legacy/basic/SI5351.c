#include "../../Config/app_si5351_variant.h"
#include "SI5351.h"

#if APP_SI5351_SELECTED_VARIANT == APP_SI5351_VARIANT_BASIC
/* basic 只保留“初始化 + 设频”这组最小接口，便于兼容最老的调用方式。 */
void SI5351C_Init(void)
{
    si5351_legacy_init_common();
}

void SI5351_SetFrequency(uint8_t channel, uint32_t freq_hz)
{
    si5351_legacy_set_frequency_common(channel, freq_hz);
}
#endif
