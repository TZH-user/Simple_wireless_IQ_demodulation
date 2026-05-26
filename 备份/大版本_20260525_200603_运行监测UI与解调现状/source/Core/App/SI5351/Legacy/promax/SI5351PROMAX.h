#ifndef __SI5351_PROMAX_H
#define __SI5351_PROMAX_H

#include "../common/si5351_legacy_common.h"

void SI5351C_Init(void);
void SI5351_SetClockSource(uint8_t channel, SI5351_Source_t source);
void SI5351_SetPLLxSource(char pll_x, uint8_t use_clkin);
bool SI5351_IsExternalClockPresent(void);
void SI5351_SetFrequency(uint8_t channel, uint32_t freq_hz);
void SI5351_SetFrequency_CHOESE_PLL(uint8_t channel, uint32_t a, uint32_t p2, int force_pll);
void SI5351_SetPhaseOffset(uint8_t channel, double degrees);
uint8_t SI5351_GetPhaseOffsetRaw(uint8_t channel);
double SI5351_GetPhaseOffsetDegrees(uint8_t channel);
void SI5351_SetPhaseAndEnable(uint8_t channel, double degrees);
void SI5351_SetDriveStrength(uint8_t channel, SI5351_Drive_t strength);
uint8_t SI5351_GetDriveStrength(uint8_t channel);
void SI5351_ReadStatus(SI5351_Status *status);
void SI5351_SetInvert(uint8_t channel, bool invert);
bool SI5351_GetInvert(uint8_t channel);
void SI5351_EnableChannel(uint8_t channel, bool enable);
uint8_t SI5351_GetClockControlReg(uint8_t channel);

#endif /* __SI5351_PROMAX_H */
