#ifndef __APP_MEMORY_MAP_H
#define __APP_MEMORY_MAP_H

#include <stdint.h>

/*
 * 当前阶段先用一个集中头文件登记外部 SDRAM 关键地址。
 * 这样做的目的不是引入复杂抽象，而是避免地址继续散落在多个 .c 文件里。
 *
 * 规则：
 * 1. 先登记，再使用
 * 2. 后续每新增一块外部 SDRAM 区域，优先先改这里
 * 3. 真正需要 section / .ld 工程化时，再升级到链接脚本管理
 */

#define APP_SDRAM_BASE_ADDR            0xC0000000UL

#define APP_LCD_HOR_RES                800U
#define APP_LCD_VER_RES                480U
#define APP_LCD_PIXEL_SIZE_BYTES       2U

#define APP_FB_ADDR                    0xC0000000UL
#define APP_FB_SIZE_BYTES              (APP_LCD_HOR_RES * APP_LCD_VER_RES * APP_LCD_PIXEL_SIZE_BYTES)
#define APP_FB_END_ADDR                (APP_FB_ADDR + APP_FB_SIZE_BYTES)

#define APP_SDRAM_TEST_ADDR            0xC0100000UL
#define APP_SDRAM_TEST_SIZE_BYTES      (256U * 1024U)
#define APP_SDRAM_TEST_END_ADDR        (APP_SDRAM_TEST_ADDR + APP_SDRAM_TEST_SIZE_BYTES)

#define APP_LVGL_DRAW_BUF_ADDR         0xC0200000UL
#define APP_LVGL_DRAW_BUF_LINES        40U
#define APP_LVGL_DRAW_BUF_SIZE_BYTES   (APP_LCD_HOR_RES * APP_LVGL_DRAW_BUF_LINES * APP_LCD_PIXEL_SIZE_BYTES)
#define APP_LVGL_DRAW_BUF_END_ADDR     (APP_LVGL_DRAW_BUF_ADDR + APP_LVGL_DRAW_BUF_SIZE_BYTES)

#define APP_LVGL_HEAP_ADDR             0xC0400000UL
#define APP_LVGL_HEAP_SIZE_BYTES       (64U * 1024U)
#define APP_LVGL_HEAP_END_ADDR         (APP_LVGL_HEAP_ADDR + APP_LVGL_HEAP_SIZE_BYTES)

#endif /* __APP_MEMORY_MAP_H */
