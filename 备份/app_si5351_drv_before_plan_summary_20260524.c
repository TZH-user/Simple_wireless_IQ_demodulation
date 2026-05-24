#include "app_si5351_drv.h"

#include "../Config/app_si5351_variant.h"
#include "../Legacy/common/si5351_legacy_common.h"
#include "../Port/si5351_port.h"

#if APP_SI5351_SELECTED_VARIANT == APP_SI5351_VARIANT_BASIC
#include "../Legacy/basic/SI5351.h"
#elif APP_SI5351_SELECTED_VARIANT == APP_SI5351_VARIANT_PRO
#include "../Legacy/pro/SI5351PRO.h"
#else
#include "../Legacy/promax/SI5351PROMAX.h"
#endif

#include "cmsis_os2.h"

#include <stdio.h>
#include <string.h>

#define APP_SI5351_STATUS_SYS_INIT 0x80U
#define APP_SI5351_STATUS_LOL_B    0x40U
#define APP_SI5351_STATUS_LOL_A    0x20U
#define APP_SI5351_STATUS_LOS_CLKIN 0x10U
#define APP_SI5351_STATUS_LOL_MASK (APP_SI5351_STATUS_LOL_A | APP_SI5351_STATUS_LOL_B)
#define APP_SI5351_REG_STATUS0     0U
#define APP_SI5351_SYS_INIT_TIMEOUT_MS 100U

static volatile uint8_t g_app_si5351_clock_ready = 0U;
static uint16_t g_app_si5351_device_addr = 0U;
static HAL_StatusTypeDef g_app_si5351_last_hal_status = HAL_OK;
static char g_app_si5351_plan_summary[96] = "none";
static uint8_t g_app_si5351_enabled_mask = 0U;

/* 包装层在这里集中描述变体能力，避免上层业务散落条件编译。 */
static bool app_si5351_variant_supports_force_pll(void)
{
    return (APP_SI5351_SELECTED_VARIANT != APP_SI5351_VARIANT_BASIC);
}

static bool app_si5351_variant_supports_drive(void)
{
    return (APP_SI5351_SELECTED_VARIANT == APP_SI5351_VARIANT_PROMAX);
}

static HAL_StatusTypeDef app_si5351_wait_for_sys_init_clear(void)
{
    uint32_t elapsed_ms = 0U;
    uint8_t status = 0U;
    HAL_StatusTypeDef hal_status;

    /* 上电或软复位后先等 SYS_INIT 清零，再写后续参数，避免与芯片自初始化冲突。 */
    while (elapsed_ms < APP_SI5351_SYS_INIT_TIMEOUT_MS)
    {
        hal_status = app_si5351_port_read_reg(g_app_si5351_device_addr, APP_SI5351_REG_STATUS0, &status);
        if (hal_status != HAL_OK)
        {
            return hal_status;
        }

        if ((status & APP_SI5351_STATUS_SYS_INIT) == 0U)
        {
            return HAL_OK;
        }

        osDelay(5U);
        elapsed_ms += 5U;
    }

    return HAL_TIMEOUT;
}

static void app_si5351_build_plan_summary(const app_si5351_output_cfg_t *cfgs, uint8_t count)
{
    if ((cfgs == NULL) || (count == 0U))
    {
        (void)snprintf(g_app_si5351_plan_summary, sizeof(g_app_si5351_plan_summary), "none");
        return;
    }

    if (count == 1U)
    {
        (void)snprintf(g_app_si5351_plan_summary,
                       sizeof(g_app_si5351_plan_summary),
                       "clk%u=%luHz",
                       cfgs[0].channel,
                       (unsigned long)cfgs[0].freq_hz);
        return;
    }

    (void)snprintf(g_app_si5351_plan_summary,
                   sizeof(g_app_si5351_plan_summary),
                   "clk%u=%luHz clk%u=%luHz",
                   cfgs[0].channel,
                   (unsigned long)cfgs[0].freq_hz,
                   cfgs[1].channel,
                   (unsigned long)cfgs[1].freq_hz);
}

app_si5351_result_t app_si5351_init_device(void)
{
    HAL_StatusTypeDef hal_status;

    /* 每次重新初始化都先清 ready，防止上层误把旧状态当成当前仍可用。 */
    g_app_si5351_clock_ready = 0U;
    g_app_si5351_last_hal_status = HAL_OK;

    hal_status = app_si5351_port_probe(&g_app_si5351_device_addr);
    if (hal_status != HAL_OK)
    {
        g_app_si5351_last_hal_status = hal_status;
        return APP_SI5351_RESULT_DEVICE_NOT_FOUND;
    }

    SI5351C.Slv_Address = (uint8_t)g_app_si5351_device_addr;

    hal_status = app_si5351_wait_for_sys_init_clear();
    if (hal_status != HAL_OK)
    {
        g_app_si5351_last_hal_status = hal_status;
        return (hal_status == HAL_TIMEOUT) ? APP_SI5351_RESULT_SYS_INIT_TIMEOUT : APP_SI5351_RESULT_I2C_READ;
    }

    app_si5351_port_clear_error();
    SI5351C_Init();
    hal_status = app_si5351_port_get_error();
    g_app_si5351_last_hal_status = hal_status;

    if (hal_status != HAL_OK)
    {
        return APP_SI5351_RESULT_I2C_WRITE;
    }

    return APP_SI5351_RESULT_OK;
}

app_si5351_result_t app_si5351_apply_output_plan(const app_si5351_output_cfg_t *cfgs, uint8_t count)
{
    uint8_t idx;

    if ((cfgs == NULL) || (count == 0U))
    {
        return APP_SI5351_RESULT_UNSUPPORTED_CONFIG;
    }

    g_app_si5351_enabled_mask = 0U;
    /* 先全关再套新计划，避免旧计划残留的通道继续输出。 */
    for (idx = 0U; idx < 8U; ++idx)
    {
        si5351_legacy_enable_channel_common(idx, false);
    }

    app_si5351_port_clear_error();

    for (idx = 0U; idx < count; ++idx)
    {
        const app_si5351_output_cfg_t *cfg = &cfgs[idx];

        if (cfg->channel > 7U)
        {
            return APP_SI5351_RESULT_UNSUPPORTED_CONFIG;
        }

        if ((cfg->pll != APP_SI5351_PLL_AUTO) && !app_si5351_variant_supports_force_pll())
        {
            return APP_SI5351_RESULT_UNSUPPORTED_CONFIG;
        }

        /* 驱动强度只在 promax 生效；其他变体直接明确报不支持，不做静默降级。 */
        if ((cfg->drive != APP_SI5351_DRIVE_DEFAULT) && !app_si5351_variant_supports_drive())
        {
            return APP_SI5351_RESULT_UNSUPPORTED_CONFIG;
        }

        if (cfg->freq_hz != 0U)
        {
            SI5351_SetFrequency(cfg->channel, cfg->freq_hz);
        }

        if (cfg->pll == APP_SI5351_PLL_PLLA)
        {
            si5351_legacy_force_channel_pll_common(cfg->channel, 0);
        }
        else if (cfg->pll == APP_SI5351_PLL_PLLB)
        {
            si5351_legacy_force_channel_pll_common(cfg->channel, 1);
        }

        if (cfg->drive != APP_SI5351_DRIVE_DEFAULT)
        {
#if APP_SI5351_SELECTED_VARIANT == APP_SI5351_VARIANT_PROMAX
            SI5351_SetDriveStrength(cfg->channel, (SI5351_Drive_t)cfg->drive);
#endif
        }

        si5351_legacy_enable_channel_common(cfg->channel, cfg->enable);
        if (cfg->enable)
        {
            g_app_si5351_enabled_mask |= (uint8_t)(1U << cfg->channel);
        }
    }

    g_app_si5351_last_hal_status = app_si5351_port_get_error();
    if (g_app_si5351_last_hal_status != HAL_OK)
    {
        return APP_SI5351_RESULT_I2C_WRITE;
    }

    app_si5351_build_plan_summary(cfgs, count);
    return APP_SI5351_RESULT_OK;
}

app_si5351_result_t app_si5351_enable_outputs(bool enable)
{
    uint8_t channel;

    app_si5351_port_clear_error();
    for (channel = 0U; channel < 8U; ++channel)
    {
        /* 只恢复当前计划里真正启用的通道，未登记通道继续保持关闭。 */
        bool channel_enable = enable && ((g_app_si5351_enabled_mask & (uint8_t)(1U << channel)) != 0U);
        si5351_legacy_enable_channel_common(channel, channel_enable);
    }

    g_app_si5351_last_hal_status = app_si5351_port_get_error();
    if (g_app_si5351_last_hal_status != HAL_OK)
    {
        g_app_si5351_clock_ready = 0U;
        return APP_SI5351_RESULT_I2C_WRITE;
    }

    g_app_si5351_clock_ready = enable ? 1U : 0U;
    return APP_SI5351_RESULT_OK;
}

app_si5351_result_t app_si5351_check_ref_status(void)
{
    uint8_t status = 0U;
    HAL_StatusTypeDef hal_status = app_si5351_port_read_reg(g_app_si5351_device_addr, APP_SI5351_REG_STATUS0, &status);

    g_app_si5351_last_hal_status = hal_status;
    if (hal_status != HAL_OK)
    {
        return APP_SI5351_RESULT_I2C_READ;
    }

    /* 当前库默认依赖外部 CLKIN，所以 LOS_CLKIN 必须视为 ready 失效。 */
    if ((status & APP_SI5351_STATUS_LOS_CLKIN) != 0U)
    {
        return APP_SI5351_RESULT_CLKIN_LOST;
    }

    if ((status & APP_SI5351_STATUS_LOL_MASK) != 0U)
    {
        return APP_SI5351_RESULT_PLL_UNLOCKED;
    }

    return APP_SI5351_RESULT_OK;
}

bool app_si5351_is_clock_ready(void)
{
    return (g_app_si5351_clock_ready != 0U);
}

const char *app_si5351_variant_name(void)
{
#if APP_SI5351_SELECTED_VARIANT == APP_SI5351_VARIANT_BASIC
    return "basic";
#elif APP_SI5351_SELECTED_VARIANT == APP_SI5351_VARIANT_PRO
    return "pro";
#else
    return "promax";
#endif
}

const char *app_si5351_port_name(void)
{
    return app_si5351_port_mode_name();
}

const char *app_si5351_result_name(app_si5351_result_t result)
{
    switch (result)
    {
    case APP_SI5351_RESULT_OK:
        return "ok";
    case APP_SI5351_RESULT_DEVICE_NOT_FOUND:
        return "device_not_found";
    case APP_SI5351_RESULT_SYS_INIT_TIMEOUT:
        return "sys_init_timeout";
    case APP_SI5351_RESULT_I2C_WRITE:
        return "i2c_write";
    case APP_SI5351_RESULT_I2C_READ:
        return "i2c_read";
    case APP_SI5351_RESULT_CLKIN_LOST:
        return "clkin_lost";
    case APP_SI5351_RESULT_PLL_UNLOCKED:
        return "pll_unlock";
    case APP_SI5351_RESULT_UNSUPPORTED_CONFIG:
        return "unsupported_config";
    default:
        return "unknown";
    }
}

const char *app_si5351_last_plan_summary(void)
{
    return g_app_si5351_plan_summary;
}

HAL_StatusTypeDef app_si5351_last_hal_status(void)
{
    return g_app_si5351_last_hal_status;
}

uint16_t app_si5351_device_addr(void)
{
    return g_app_si5351_device_addr;
}
