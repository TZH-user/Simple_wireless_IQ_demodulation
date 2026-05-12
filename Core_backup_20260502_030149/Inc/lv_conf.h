#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*
 * 当前工程的第一轮目标是：
 * 1. 在 H743 + FreeRTOS + RGB565 + 800x480 下先跑通 LVGL 9.5.0
 * 2. 先只接显示，不接触摸，不接 DMA2D
 * 3. 先让最小 UI 能稳定显示，再逐步扩展
 *
 * 因此这里先保留“最小可用配置”，其余选项交给 LVGL 默认值补齐。
 */

/*====================
 * Color settings
 *====================*/

/* 当前屏幕链路是 RGB565，因此 LVGL 也统一使用 16-bit 颜色深度。 */
#define LV_COLOR_DEPTH 16

/*=========================
 * Stdlib wrapper settings
 *=========================*/

/*
 * 第一轮先使用 LVGL 内建内存实现，避免一开始就把外部 SDRAM 内存池、
 * 自定义 malloc 和图形问题混在一起。
 */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING  LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

/*
 * 先给 LVGL 一块保守的内部堆，够跑最小界面和基础控件即可。
 * 后面如果开始跑复杂页面、图片或字体，再按实际情况调整。
 */
#define LV_MEM_SIZE (64U * 1024U)
#define LV_MEM_ADR  0xC0400000UL

/*====================
 * HAL settings
 *====================*/

/* 当前显示刷新目标先按约 30fps 的节奏配置。 */
#define LV_DEF_REFR_PERIOD 33

/* 5 寸 800x480 屏先按现有项目经验取 130 DPI。 */
#define LV_DPI_DEF 130

/*=================
 * Operating system
 *=================*/

/*
 * 当前工程已经接入 CMSIS-RTOS2，因此这里明确告诉 LVGL：
 * 它运行在 CMSIS-RTOS2 环境下。
 */
#define LV_USE_OS LV_OS_CMSIS_RTOS2

/*========================
 * Rendering configuration
 *========================*/

/* 第一轮先只用软件绘图，关闭并行绘制和额外加速。 */
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_DRAW_UNIT_CNT 1

/* 当前阶段先不启用 STM32 DMA2D，等最小显示跑通后再加速。 */
#define LV_USE_DRAW_DMA2D 0
#define LV_USE_GPU_STM32_DMA2D 0

/*=======================
 * Feature configuration
 *=======================*/

/* 第一轮建议打开日志，便于后续排查初始化和显示问题。 */
#define LV_USE_LOG 1
#if LV_USE_LOG
    #define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF 0
#endif

/* 这些断言对早期移植排错很有帮助，代价较低，建议先开。 */
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/*
 * 第一轮不跑官方 demo，也不依赖复杂资源系统。
 * 后面如果需要 benchmark/widgets，再按需打开。
 */
#define LV_USE_DEMO_WIDGETS   0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS    0
#define LV_USE_DEMO_MUSIC     0

/*==================
 * Font usage
 *==================*/

/*
 * 先保留一个最常用的小字体，足够显示最小 label。
 * 后面需要中文字体时，再单独接入自己的字库。
 */
#define LV_FONT_MONTSERRAT_14 1

#endif /* LV_CONF_H */
