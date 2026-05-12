#include "app_display.h"

#include "cmsis_os2.h"
#include "gpio.h"
#include "main.h"

#include <stdint.h>

/*
 * 当前阶段继续固定使用 LTDC framebuffer = 0xC0000000。
 * 这块地址是外部 SDRAM，LTDC 会直接从这里读像素。
 */
#define LCD_FB_ADDR        0xC0000000UL
#define LCD_WIDTH          800U
#define LCD_HEIGHT         480U
#define LCD_PIXEL_BYTES    2U
#define LCD_FB_SIZE        (LCD_WIDTH * LCD_HEIGHT * LCD_PIXEL_BYTES)

#define RGB565_RED         0xF800U
#define RGB565_GREEN       0x07E0U
#define RGB565_BLUE        0x001FU
#define RGB565_WHITE       0xFFFFU

/*
 * 方案 1：保留上电彩条，但允许后续一键关闭。
 * 等显示基线完全稳定后，可以把它改成 0，直接进 LVGL。
 */
#define APP_DISPLAY_ENABLE_POWER_ON_PATTERN 0

static void App_DisplayBacklightOn(void);
static void App_DisplayCleanDCacheByAddr(uint32_t addr, uint32_t size);
static void App_DisplayFillColor(uint16_t color);

void App_DisplayInit(void)
{
  /*
   * 现在 LCD_BL 已经由 CubeMX 接管为普通 GPIO 输出脚，
   * 这里不再重复初始化 GPIO 模式，只负责把背光拉高。
   */
  App_DisplayBacklightOn();
}

void App_DisplayRunPowerOnPattern(void)
{
#if !APP_DISPLAY_ENABLE_POWER_ON_PATTERN
  return;
#endif

  /*
   * 在 RTOS 任务上下文里，彩条延时统一用 osDelay()，避免继续混用 HAL_Delay()。
   * 这样任务调度语义更清楚，也更符合后续全部迁入 RTOS 的方向。
   */
  App_DisplayFillColor(RGB565_RED);
  osDelay(300);
  App_DisplayFillColor(RGB565_GREEN);
  osDelay(300);
  App_DisplayFillColor(RGB565_BLUE);
  osDelay(300);
  App_DisplayFillColor(RGB565_WHITE);
}

static void App_DisplayBacklightOn(void)
{
  HAL_GPIO_WritePin(LCD_BL_GPIO_Port, LCD_BL_Pin, GPIO_PIN_SET);
}

static void App_DisplayCleanDCacheByAddr(uint32_t addr, uint32_t size)
{
  uint32_t aligned_addr = addr & ~31UL;
  uint32_t aligned_size = ((addr + size + 31UL) & ~31UL) - aligned_addr;

  /* LTDC 直接从 SDRAM 取像素，不清 DCache 就可能继续显示旧图像。 */
  SCB_CleanDCache_by_Addr((uint32_t *)aligned_addr, (int32_t)aligned_size);
}

static void App_DisplayFillColor(uint16_t color)
{
  uint32_t pixel_count = LCD_WIDTH * LCD_HEIGHT;
  uint32_t index = 0;
  volatile uint16_t *framebuffer = (volatile uint16_t *)LCD_FB_ADDR;

  for(index = 0; index < pixel_count; index++) {
    framebuffer[index] = color;
  }

  App_DisplayCleanDCacheByAddr(LCD_FB_ADDR, LCD_FB_SIZE);
}
