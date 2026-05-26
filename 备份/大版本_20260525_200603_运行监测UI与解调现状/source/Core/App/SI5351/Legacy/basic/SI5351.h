#ifndef __SI5351_BASIC_H
#define __SI5351_BASIC_H

#include "../common/si5351_legacy_common.h"

void SI5351C_Init(void);
void SI5351_SetFrequency(uint8_t channel, uint32_t freq_hz);

#endif /* __SI5351_BASIC_H */
