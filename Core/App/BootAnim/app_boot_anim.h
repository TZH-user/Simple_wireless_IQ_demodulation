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

/* 动画期间允许“已确认的真实触摸”打断动画。 */
#ifndef APP_BOOT_ANIM_ABORT_ON_TOUCH_ENABLE
#define APP_BOOT_ANIM_ABORT_ON_TOUCH_ENABLE   1U
#endif

/*
 * 默认禁止仅凭 TOUCH_INT 裸边沿打断动画。
 * 真实触摸应由 TouchTask 读取 GT9xx 状态后确认，避免上电毛刺误退出。
 */
#ifndef APP_BOOT_ANIM_ABORT_BY_RAW_IRQ_ENABLE
#define APP_BOOT_ANIM_ABORT_BY_RAW_IRQ_ENABLE 0U
#endif

/* 触摸打断后先清黑 APP_FB_ADDR，再交给 LVGL 初始化，避免残留半帧动画。 */
#ifndef APP_BOOT_ANIM_CLEAR_ON_ABORT
#define APP_BOOT_ANIM_CLEAR_ON_ABORT          1U
#endif

/* 打断检测轮询周期，单位 ms；数值越小，触摸响应越快。 */
#ifndef APP_BOOT_ANIM_ABORT_POLL_MS
#define APP_BOOT_ANIM_ABORT_POLL_MS           5U
#endif

/*
 * 动画启动后的触摸保护时间。
 * 保护期内只清除上电/复位/初始化毛刺，不允许触摸打断。
 */
#ifndef APP_BOOT_ANIM_TOUCH_ARM_MS
#define APP_BOOT_ANIM_TOUCH_ARM_MS            1200U
#endif

#define APP_BOOT_ANIM_TARGET_FPS              12U
#define APP_BOOT_ANIM_MIN_FPS                 10U
#define APP_BOOT_ANIM_DURATION_MS             10000U

void App_BootAnimPlay(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_BOOT_ANIM_H */
