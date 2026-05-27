#ifndef APP_BUZZER_H
#define APP_BUZZER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 屏幕按键短提示音时长，单位 ms；只在 SET 菜单打开 UI BEEP 后生效。 */
#define APP_BUZZER_UI_BEEP_MS 25U
/* 扫频锁定、识别完成、解调启动提示音时长，单位 ms；比按键提示更明显。 */
#define APP_BUZZER_EVENT_BEEP_MS 90U

void app_buzzer_init(void);
void app_buzzer_process(void);
void app_buzzer_beep_ms(uint32_t duration_ms);
void app_buzzer_notify_ui_action(void);
void app_buzzer_notify_sweep_lock(void);
void app_buzzer_notify_analyze_done(void);
void app_buzzer_notify_demod_start(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_BUZZER_H */
