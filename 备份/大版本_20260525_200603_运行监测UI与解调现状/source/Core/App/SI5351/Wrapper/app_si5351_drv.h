#ifndef APP_SI5351_DRV_H
#define APP_SI5351_DRV_H

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/* AUTO 表示把 PLL 选择权交给当前变体默认行为。 */
typedef enum {
    APP_SI5351_PLL_AUTO = 0,
    APP_SI5351_PLL_PLLA,
    APP_SI5351_PLL_PLLB
} app_si5351_pll_t;

/* DEFAULT 代表不强制改驱动强度，由当前变体保留原始控制字。 */
typedef enum {
    APP_SI5351_DRIVE_DEFAULT = 0,
    APP_SI5351_DRIVE_2MA = 2,
    APP_SI5351_DRIVE_4MA = 4,
    APP_SI5351_DRIVE_6MA = 6,
    APP_SI5351_DRIVE_8MA = 8
} app_si5351_drive_t;

typedef enum {
    APP_SI5351_RESULT_OK = 0,
    APP_SI5351_RESULT_DEVICE_NOT_FOUND,
    APP_SI5351_RESULT_SYS_INIT_TIMEOUT,
    APP_SI5351_RESULT_I2C_WRITE,
    APP_SI5351_RESULT_I2C_READ,
    APP_SI5351_RESULT_CLKIN_LOST,
    APP_SI5351_RESULT_PLL_UNLOCKED,
    APP_SI5351_RESULT_UNSUPPORTED_CONFIG
} app_si5351_result_t;

typedef struct {
    uint8_t channel;
    uint32_t freq_hz;
    app_si5351_pll_t pll;
    app_si5351_drive_t drive;
    bool enable;
} app_si5351_output_cfg_t;

/* 一次性完成设备探测、地址锁定和 legacy 底层初始化。 */
app_si5351_result_t app_si5351_init_device(void);
/* 按输出计划统一配置频率、PLL 归属、驱动强度和使能状态。 */
app_si5351_result_t app_si5351_apply_output_plan(const app_si5351_output_cfg_t *cfgs, uint8_t count);
/* 只对当前计划中已登记的通道生效，不会无差别打开 8 路输出。 */
app_si5351_result_t app_si5351_enable_outputs(bool enable);
/* 轮询参考状态，主要用于检测 CLKIN 丢失和 PLL 失锁。 */
app_si5351_result_t app_si5351_check_ref_status(void);
bool app_si5351_is_clock_ready(void);
const char *app_si5351_variant_name(void);
const char *app_si5351_port_name(void);
const char *app_si5351_result_name(app_si5351_result_t result);
const char *app_si5351_last_plan_summary(void);
HAL_StatusTypeDef app_si5351_last_hal_status(void);
uint16_t app_si5351_device_addr(void);

#endif /* APP_SI5351_DRV_H */
