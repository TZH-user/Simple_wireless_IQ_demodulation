# IQ Boot Animation

本模块用于 STM32H743XIH6 的 800×480 RGB565 LTDC 开机动画。

## 关键设计

- 板端不解码 MP4/GIF；MP4/GIF 只用于电脑端视觉验收。
- MCU 端采用“程序绘制 HUD 动画”的方式：背景渐变、IQ 圆环、I/Q 分离、频域谱图、粒子余晖都由 `app_boot_anim.c` 直接绘制。
- `Assets/boot_bg_800x480_preview.png` 只是干净背景参考图，不会在当前代码里直接显示，也不包含 `READY/COMPLETE/谱图` 等最终状态内容。
- 双 framebuffer：动画期间在 `APP_FB_ADDR` 与 `APP_BOOT_FB_ADDR` 之间切换，结束/打断后回到 `APP_FB_ADDR` 给 LVGL 使用。

## 宏开关

在 `app_boot_anim.h` 里可以直接改：

```c
#define APP_BOOT_ANIM_ENABLE                  1U  /* 0U = 不播放开机动画 */
#define APP_BOOT_ANIM_ABORT_ON_TOUCH_ENABLE   1U  /* 触摸立即打断动画 */
#define APP_BOOT_ANIM_CLEAR_ON_ABORT          1U  /* 打断后先清黑再进入 LVGL */
#define APP_BOOT_ANIM_ABORT_POLL_MS           5U  /* 动画等待期间触摸检测间隔 */
```

## 触摸打断

`App_TouchNotifyFromISR()` 会锁存触摸中断，即使 TouchTask 还没来得及读取 GT9xx 坐标，`App_BootAnimPlay()` 也能在最多约 `APP_BOOT_ANIM_ABORT_POLL_MS` 后退出动画。退出后会切回 `APP_FB_ADDR`，然后继续执行 `App_LvglInit()` 进入系统界面。

## 需要加入工程的源文件

请确认 IDE 工程里加入：

```text
App/BootAnim/app_boot_anim.c
```

同时这次修改了：

```text
App/Touch/app_touch_gt9xx.c
App/Touch/app_touch_gt9xx.h
App/Tasks/app_display_task.c
App/app_memory_map.h
```

当前不需要改 CubeMX；除非你希望动画在所有 RTOS 任务启动前独占播放，才需要调整任务启动顺序。
