#ifndef RX_DEMOD_H
#define RX_DEMOD_H

#include <stdint.h>

#define RX_DEMOD_MAX_BLOCK_SAMPLES 4096U /* 单次解调最多处理的采样点数；当前与 ADC_BLOCK_N 保持一致。 */

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
void RxDemod_Reset(void);
void RxDemod_SetMode(RxMode mode);
void RxDemod_ConfigureSignal(uint32_t symbol_rate_hz,
                             int32_t low_if_hz,
                             uint32_t fsk_separation_hz);

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
