#ifndef RX_DEMOD_H
#define RX_DEMOD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    RX_MODE_LOOPBACK = 0,
    RX_MODE_AM,
    RX_MODE_ASK,
    RX_MODE_FM,
    RX_MODE_FSK,
    RX_MODE_PSK
} RxMode;

void RxDemod_Init(uint32_t sample_rate_hz);
void RxDemod_SetMode(RxMode mode);

void RxDemod_FSK_ProcessBlock(const uint16_t *i_adc,
                              const uint16_t *q_adc,
                              uint32_t n,
                              uint16_t *dac_out);

void RxDemod_AM_ProcessBlock(const uint16_t *i_adc,
                          const uint16_t *q_adc,
                          uint32_t n,
                          uint16_t *dac_out);


void RxDemod_FM_ProcessBlock(const uint16_t *i_adc,
                             const uint16_t *q_adc,
                             uint32_t n,
                             uint16_t *dac_out);
  

void RxDemod_FM_CMSIS_ProcessBlock(const uint16_t *i_adc,
                                   const uint16_t *q_adc,
                                   uint32_t n,
                                   uint16_t *dac_out);

void RxDemod_PSK_ProcessBlock(const uint16_t *i_adc,
                              const uint16_t *q_adc,
                              uint32_t n,
                              uint16_t *dac_out);
#ifdef __cplusplus
}
#endif

#endif /* RX_DEMOD_H */
