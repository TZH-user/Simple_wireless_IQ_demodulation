#ifndef APP_BOOT_ANIM_H
#define APP_BOOT_ANIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 开机动画总开关：
 *   1U = 启用 10s IQ 开机动画
 *   0U = 完全跳过动画，直接进入 LVGL 系统界面
 *
 * 建议后续统一在工程预定义宏或本文件里改，不需要改 CubeMX 生成区。
 */
#ifndef APP_BOOT_ANIM_ENABLE
#define APP_BOOT_ANIM_ENABLE                  1U
#endif

/* 动画期间收到触摸中断/触摸缓存按下后，立即退出动画进入系统。 */
#ifndef APP_BOOT_ANIM_ABORT_ON_TOUCH_ENABLE
#define APP_BOOT_ANIM_ABORT_ON_TOUCH_ENABLE   1U
#endif

/* 触摸打断后先清黑 APP_FB_ADDR，再交给 LVGL 初始化，避免残留半帧动画。 */
#ifndef APP_BOOT_ANIM_CLEAR_ON_ABORT
#define APP_BOOT_ANIM_CLEAR_ON_ABORT          1U
#endif

/* 打断检测轮询周期，单位 ms；数值越小，触摸响应越快。 */
#ifndef APP_BOOT_ANIM_ABORT_POLL_MS
#define APP_BOOT_ANIM_ABORT_POLL_MS           5U
#endif

#define APP_BOOT_ANIM_TARGET_FPS              12U
#define APP_BOOT_ANIM_MIN_FPS                 10U
#define APP_BOOT_ANIM_DURATION_MS             10000U

void App_BootAnimPlay(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_BOOT_ANIM_H */
