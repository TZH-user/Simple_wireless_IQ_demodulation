#include "app_buzzer.h"

#include "app_ocxo_cal.h"
#include "cmsis_os2.h"
#include "gpio.h"

#define APP_BUZZER_GPIO_PORT GPIOA
#define APP_BUZZER_GPIO_PIN GPIO_PIN_8
#define APP_BUZZER_ACTIVE_LEVEL GPIO_PIN_SET
#define APP_BUZZER_INACTIVE_LEVEL GPIO_PIN_RESET

static uint8_t g_buzzer_initialized = 0U;
static uint8_t g_buzzer_active = 0U;
static uint32_t g_buzzer_stop_tick = 0UL;

/* 初始化 PA8 有源蜂鸣器输出；模块自管脚初始化，避免依赖 CubeMX 是否生成 PA8。 */
void app_buzzer_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (g_buzzer_initialized != 0U)
    {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN, APP_BUZZER_INACTIVE_LEVEL);

    GPIO_InitStruct.Pin = APP_BUZZER_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(APP_BUZZER_GPIO_PORT, &GPIO_InitStruct);

    g_buzzer_initialized = 1U;
}

/* 5ms 显示任务里轮询，按到期时间关闭蜂鸣器，不阻塞 UI 或采样任务。 */
void app_buzzer_process(void)
{
    uint32_t now_tick;

    if (g_buzzer_active == 0U)
    {
        return;
    }

    now_tick = osKernelGetTickCount();
    if ((int32_t)(now_tick - g_buzzer_stop_tick) >= 0)
    {
        HAL_GPIO_WritePin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN, APP_BUZZER_INACTIVE_LEVEL);
        g_buzzer_active = 0U;
    }
}

void app_buzzer_beep_ms(uint32_t duration_ms)
{
    if (duration_ms == 0U)
    {
        return;
    }

    app_buzzer_init();
    g_buzzer_stop_tick = osKernelGetTickCount() + duration_ms;
    g_buzzer_active = 1U;
    HAL_GPIO_WritePin(APP_BUZZER_GPIO_PORT, APP_BUZZER_GPIO_PIN, APP_BUZZER_ACTIVE_LEVEL);
}

void app_buzzer_notify_ui_action(void)
{
    if (app_ocxo_cal_get_beep_ui_enable() != 0U)
    {
        app_buzzer_beep_ms(APP_BUZZER_UI_BEEP_MS);
    }
}

void app_buzzer_notify_sweep_lock(void)
{
    if (app_ocxo_cal_get_beep_sweep_lock_enable() != 0U)
    {
        app_buzzer_beep_ms(APP_BUZZER_EVENT_BEEP_MS);
    }
}

void app_buzzer_notify_analyze_done(void)
{
    if (app_ocxo_cal_get_beep_analyze_done_enable() != 0U)
    {
        app_buzzer_beep_ms(APP_BUZZER_EVENT_BEEP_MS);
    }
}

void app_buzzer_notify_demod_start(void)
{
    if (app_ocxo_cal_get_beep_demod_start_enable() != 0U)
    {
        app_buzzer_beep_ms(APP_BUZZER_EVENT_BEEP_MS);
    }
}

void app_buzzer_notify_cal_done(void)
{
    app_buzzer_beep_ms(APP_BUZZER_CAL_DONE_BEEP_MS);
}
