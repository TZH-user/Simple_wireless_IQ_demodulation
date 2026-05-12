#ifndef APP_MOD_DETECT_H
#define APP_MOD_DETECT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APP_MODDET_ADC_MID_CODE
#define APP_MODDET_ADC_MID_CODE              32768
#endif

#ifndef APP_MODDET_MIN_AC_POWER
#define APP_MODDET_MIN_AC_POWER              64U
#endif

#ifndef APP_MODDET_AM_ENV_PM_THRESHOLD
#define APP_MODDET_AM_ENV_PM_THRESHOLD       220U
#endif

#ifndef APP_MODDET_FREQ_ACTIVITY_THRESHOLD
#define APP_MODDET_FREQ_ACTIVITY_THRESHOLD   18U
#endif

#ifndef APP_MODDET_FSK_SIGN_MIN_PERCENT
#define APP_MODDET_FSK_SIGN_MIN_PERCENT      20U
#endif

#ifndef APP_MODDET_PSK_JUMP_PM_THRESHOLD
#define APP_MODDET_PSK_JUMP_PM_THRESHOLD     20U
#endif

typedef enum
{
  APP_MODDET_MODE_UNKNOWN = 0,
  APP_MODDET_MODE_NO_CARRIER,
  APP_MODDET_MODE_CW,
  APP_MODDET_MODE_AM,
  APP_MODDET_MODE_FM,
  APP_MODDET_MODE_FSK,
  APP_MODDET_MODE_PSK
} app_mod_detect_mode_t;

typedef struct
{
  uint32_t sample_rate_hz;
  uint16_t min_block_samples;
  uint16_t reserved;
} app_mod_detect_config_t;

typedef struct
{
  app_mod_detect_mode_t mode;
  uint32_t block_count;
  uint32_t ac_power;
  uint32_t env_variation_pm;
  uint32_t freq_activity;
  int32_t freq_bias;
  uint32_t fsk_balance_pm;
  uint32_t psk_jump_pm;
  uint16_t positive_freq_percent;
  uint16_t negative_freq_percent;
  uint8_t carrier_present;
  uint8_t reserved[3];
} app_mod_detect_status_t;

void app_mod_detect_init(const app_mod_detect_config_t *config);
void app_mod_detect_process_iq_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt);
void app_mod_detect_get_status(app_mod_detect_status_t *status_out);
const char *app_mod_detect_mode_name(app_mod_detect_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* APP_MOD_DETECT_H */
