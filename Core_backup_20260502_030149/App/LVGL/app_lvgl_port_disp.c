#include "app_lvgl_port_disp.h"

#include "../app_memory_map.h"
#include "lvgl.h"
#include "main.h"

#include <stdint.h>
#include <string.h>

#define APP_LCD_FB_STRIDE_BYTES     (APP_LCD_HOR_RES * APP_LCD_PIXEL_SIZE_BYTES)

static lv_display_t *g_lvgl_display = NULL;

static void App_LvglPortDispFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void App_LvglPortDispCleanDCacheByAddr(uint32_t addr, uint32_t size);

void App_LvglPortDispInit(void)
{
  void *draw_buf = (void *)APP_LVGL_DRAW_BUF_ADDR;

  g_lvgl_display = lv_display_create(APP_LCD_HOR_RES, APP_LCD_VER_RES);

  /*
   * 当前屏链路是 RGB565，因此这里必须明确告诉 LVGL 使用 RGB565。
   * 这样 draw buffer 和 framebuffer 对像素的解释才一致。
   */
  lv_display_set_color_format(g_lvgl_display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(g_lvgl_display,
                         draw_buf,
                         NULL,
                         APP_LVGL_DRAW_BUF_SIZE_BYTES,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(g_lvgl_display, App_LvglPortDispFlush);
}

static void App_LvglPortDispFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
  uint32_t y = 0U;
  uint32_t copy_width_bytes = 0U;
  uint32_t dst_addr = 0U;
  uint32_t flush_height = 0U;

  (void)disp;

  /*
   * px_map 指向的是 LVGL 已经渲染好的局部像素块。
   * 这里按行把它贴到 LTDC 使用的整屏 framebuffer 对应位置。
   * 之所以按行拷，而不是一把整块 memcpy，是因为当前刷新的通常是局部矩形区域。
   */
  copy_width_bytes = (uint32_t)(area->x2 - area->x1 + 1) * APP_LCD_PIXEL_SIZE_BYTES;
  flush_height = (uint32_t)(area->y2 - area->y1 + 1);

  for(y = 0U; y < flush_height; y++) {
    dst_addr = APP_FB_ADDR
             + ((uint32_t)(area->y1 + (int32_t)y) * APP_LCD_FB_STRIDE_BYTES)
             + ((uint32_t)area->x1 * APP_LCD_PIXEL_SIZE_BYTES);

    memcpy((void *)dst_addr,
           px_map + (y * copy_width_bytes),
           copy_width_bytes);
  }

  /*
   * LTDC 会直接从 SDRAM 读取 framebuffer。
   * 如果这里不清对应范围的 DCache，屏幕可能继续显示旧内容或局部不刷新。
   */
  App_LvglPortDispCleanDCacheByAddr(APP_FB_ADDR
                                    + ((uint32_t)area->y1 * APP_LCD_FB_STRIDE_BYTES)
                                    + ((uint32_t)area->x1 * APP_LCD_PIXEL_SIZE_BYTES),
                                    (flush_height - 1U) * APP_LCD_FB_STRIDE_BYTES + copy_width_bytes);

  lv_display_flush_ready(disp);
}

static void App_LvglPortDispCleanDCacheByAddr(uint32_t addr, uint32_t size)
{
  uint32_t aligned_addr = addr & ~31UL;
  uint32_t aligned_size = ((addr + size + 31UL) & ~31UL) - aligned_addr;

  SCB_CleanDCache_by_Addr((uint32_t *)aligned_addr, (int32_t)aligned_size);
}
