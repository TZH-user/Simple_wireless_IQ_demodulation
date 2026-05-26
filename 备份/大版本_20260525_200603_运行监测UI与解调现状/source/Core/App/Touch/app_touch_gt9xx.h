#ifndef APP_TOUCH_GT9XX_H
#define APP_TOUCH_GT9XX_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool App_TouchInit(void);
/* 兼容旧调用路径：主动轮询一次 GT9xx 并更新缓存触摸状态。 */
void App_TouchPoll(void);
/* LVGL indev 读取入口：返回最近一次采样缓存，不在这里做 IIC 通信。 */
bool App_TouchGetPoint(uint16_t *x, uint16_t *y);
uint8_t App_TouchGetPointCount(void);
/* 返回触摸芯片是否已经完成初始化。 */
bool App_TouchIsInitialized(void);
/* 返回 TouchTask 已经从 GT9xx 读到的有效触摸状态。 */
bool App_TouchHasValidTouch(void);
/* 清除缓存坐标、有效触摸和裸中断锁存，用于屏蔽上电毛刺。 */
void App_TouchClearState(void);
/* 读取并清除触摸中断锁存标志；用于开机动画等非 LVGL 阶段快速打断。 */
bool App_TouchConsumeInterruptFlag(void);
/* EXTI ISR 调用：仅做任务通知，避免在中断里执行位操作 IIC。 */
void App_TouchNotifyFromISR(uint16_t gpio_pin);
/* TouchTask 主循环：等待中断通知后在任务上下文读取 GT9xx。 */
void App_TouchTaskRun(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_TOUCH_GT9XX_H */
