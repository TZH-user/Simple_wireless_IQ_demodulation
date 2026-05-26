#ifndef __SI5351_PRO_H
#define __SI5351_PRO_H

#include "../common/si5351_legacy_common.h"

void SI5351C_Init(void);
void SI5351_SetFrequency(uint8_t channel, uint32_t freq_hz);
void SI5351_SetFrequency_CHOESE_PLL(uint8_t channel, uint32_t a, uint32_t p2, int force_pll);

#endif /* __SI5351_PRO_H */
