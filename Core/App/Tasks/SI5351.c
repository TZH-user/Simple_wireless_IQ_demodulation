#include "SI5351.h"

#include "../SI5351/Config/app_si5351_variant.h"
#include "../SI5351/Wrapper/app_si5351_drv.h"
#include "AppDebugConfig.h"
#include "app_dds_ctrl.h"
#include "RtosTypes.h"
#include "cmsis_os2.h"

#include <stdio.h>

#define APP_SI5351_STARTUP_RETRY_MS  200U
#define APP_SI5351_MONITOR_PERIOD_MS 1000U
#define APP_SI5351_LOCK_TIMEOUT_MS   100U
#define APP_SI5351_RECOVERY_REINIT_DDS_ENABLE 1U /* SI5351 恢复成功后同步重新初始化 AD9959，避免参考时钟异常后 DDS 状态残留。 */
#define APP_SI5351_MONITOR_FAIL_LIMIT 5U /* ready 后连续多少次状态异常才处理；I2C 读失败只告警，不直接关闭已配置输出。 */
#define SI5351_LOG_ENBLE 1 /* 是否启用 SI5351 相关日志输出 */

/* 默认输出计划集中放在任务层，后续切版本或切板级频点时只改这里即可。 */
static volatile uint8_t g_app_si5351_recovery_requested = 0U;

static const app_si5351_output_cfg_t g_app_si5351_default_plan[] = {
    /* CH1/CH3 当前不参与主链路，默认写入计划但不打开输出。 */
    {1U, 65000000UL, APP_SI5351_PLL_AUTO, APP_SI5351_DRIVE_DEFAULT, false},
    {0U, 25000000UL, APP_SI5351_PLL_AUTO, APP_SI5351_DRIVE_DEFAULT, true},
    {3U, 25000000UL, APP_SI5351_PLL_AUTO, APP_SI5351_DRIVE_DEFAULT, false},
#if APP_SI5351_SELECTED_VARIANT == APP_SI5351_VARIANT_BASIC
    /* basic 版本不支持强制绑 PLLB，这里必须退回 AUTO。 */
    {0U, 20480000UL, APP_SI5351_PLL_AUTO, APP_SI5351_DRIVE_DEFAULT, true},
#else
    /* pro/promax 版本允许把 20.48 MHz 独立挂到 PLLB；25/50 MHz 继续留在默认 PLLA 整数分频。 */
    {2U, 20480000UL, APP_SI5351_PLL_PLLB, APP_SI5351_DRIVE_DEFAULT, true},
#endif
};

static void app_si5351_log_fault(app_si5351_result_t result)
{
#if (SI5351_LOG_ENBLE != 0U)
    static char log_buf[128];

    (void)snprintf(log_buf,
                   sizeof(log_buf),
                   "si5351: fault=%s hal=%ld variant=%s port=%s\r\n",
                   app_si5351_result_name(result),
                   (long)app_si5351_last_hal_status(),
                   app_si5351_variant_name(),
                   app_si5351_port_name());
    print_queue_send(log_buf);
#else
    (void)result;
#endif
}

void app_si5351_request_recovery(void)
{
    g_app_si5351_recovery_requested = 1U;
}

static void app_si5351_request_dds_reinit(void)
{
#if (APP_SI5351_RECOVERY_REINIT_DDS_ENABLE != 0U)
    AppDdsCmd cmd = AppDDS_MakeInitCmd();

    (void)AppDDS_DispatchCmd(&cmd);
#endif
}

static app_si5351_result_t app_si5351_start_default_plan(void)
{
    app_si5351_result_t result;
    uint32_t elapsed_ms = 0U;

    result = app_si5351_init_device();
    if (result != APP_SI5351_RESULT_OK)
    {
        return result;
    }

    result = app_si5351_apply_output_plan(g_app_si5351_default_plan,
                                          (uint8_t)(sizeof(g_app_si5351_default_plan) / sizeof(g_app_si5351_default_plan[0])));
    if (result != APP_SI5351_RESULT_OK)
    {
        return result;
    }

    /* 输出计划写完后还要等参考状态稳定，避免刚配置完就提前开输出。 */
    while (elapsed_ms < APP_SI5351_LOCK_TIMEOUT_MS)
    {
        result = app_si5351_check_ref_status();
        if (result == APP_SI5351_RESULT_OK)
        {
            return app_si5351_enable_outputs(true);
        }

        if (result == APP_SI5351_RESULT_I2C_READ)
        {
            return result;
        }

        osDelay(5U);
        elapsed_ms += 5U;
    }

    /* 暂时无CLKIN_LOST判定，都归类为 PLL 失锁 */
    return APP_SI5351_RESULT_PLL_UNLOCKED;
}

void StartSI5351(void *argument)
{
    app_si5351_result_t last_fault = APP_SI5351_RESULT_OK;
    uint8_t monitor_fail_cnt = 0U;
#if (SI5351_LOG_ENBLE != 0U)
    static char log_buf[192];
#endif

    (void)argument;

    for (;;)
    {
        app_si5351_result_t result;

        if (g_app_si5351_recovery_requested != 0U)
        {
            g_app_si5351_recovery_requested = 0U;
            (void)app_si5351_enable_outputs(false);
            result = app_si5351_start_default_plan();
            if (result == APP_SI5351_RESULT_OK)
            {
#if (SI5351_LOG_ENBLE != 0U)
                print_queue_send("si5351: recovered, dds reinit\r\n");
#endif
                app_si5351_request_dds_reinit();
                last_fault = APP_SI5351_RESULT_OK;
                monitor_fail_cnt = 0U;
                osDelay(APP_SI5351_MONITOR_PERIOD_MS);
                continue;
            }

            if (result != last_fault)
            {
                app_si5351_log_fault(result);
                last_fault = result;
            }
            g_app_si5351_recovery_requested = 1U;
            monitor_fail_cnt = 0U;
            osDelay(APP_SI5351_STARTUP_RETRY_MS);
            continue;
        }

        if (!app_si5351_is_clock_ready())
        {
            /* 未 ready 时持续重试“探测 -> 初始化 -> 应用计划”，直到外部参考与 PLL 都稳定。 */
            result = app_si5351_start_default_plan();
            if (result == APP_SI5351_RESULT_OK)
            {
#if (SI5351_LOG_ENBLE != 0U)
                (void)snprintf(log_buf,
                               sizeof(log_buf),
                               "si5351: ready addr=0x%02X variant=%s port=%s plan=%s\r\n",
                               (unsigned int)(app_si5351_device_addr() >> 1),
                               app_si5351_variant_name(),
                               app_si5351_port_name(),
                               app_si5351_last_plan_summary());
                print_queue_send(log_buf);
#endif
                last_fault = APP_SI5351_RESULT_OK;
                monitor_fail_cnt = 0U;
                osDelay(APP_SI5351_MONITOR_PERIOD_MS);
                continue;
            }

            if (result != last_fault)
            {
                app_si5351_log_fault(result);
                last_fault = result;
            }

            (void)app_si5351_enable_outputs(false);
            osDelay(APP_SI5351_STARTUP_RETRY_MS);
            continue;
        }

        /* ready 后继续轮询参考状态；连续异常达到门限后才关输出并重新走启动流程。 */
        result = app_si5351_check_ref_status();
        if (result != APP_SI5351_RESULT_OK)
        {
            if (monitor_fail_cnt < APP_SI5351_MONITOR_FAIL_LIMIT)
            {
                monitor_fail_cnt++;
            }

            if (monitor_fail_cnt < APP_SI5351_MONITOR_FAIL_LIMIT)
            {
                osDelay(APP_SI5351_MONITOR_PERIOD_MS);
                continue;
            }

            if (result != last_fault)
            {
                app_si5351_log_fault(result);
                last_fault = result;
            }
            app_si5351_request_recovery();
            monitor_fail_cnt = 0U;
            osDelay(APP_SI5351_STARTUP_RETRY_MS);
            continue;
        }

        monitor_fail_cnt = 0U;
        last_fault = APP_SI5351_RESULT_OK;
        osDelay(APP_SI5351_MONITOR_PERIOD_MS);
    }
}
