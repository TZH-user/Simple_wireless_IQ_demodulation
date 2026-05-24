#include "app_boot_anim.h"

#include "../app_memory_map.h"
#include "../Touch/app_touch_gt9xx.h"
#include "cmsis_os2.h"
#include "ltdc.h"
#include "main.h"

#include <stdint.h>
#include <string.h>

#define BOOT_W                 APP_LCD_HOR_RES
#define BOOT_H                 APP_LCD_VER_RES
#define BOOT_FRAME_MS          ((1000U + APP_BOOT_ANIM_TARGET_FPS - 1U) / APP_BOOT_ANIM_TARGET_FPS)
#define BOOT_TOTAL_FRAMES      ((APP_BOOT_ANIM_DURATION_MS * APP_BOOT_ANIM_TARGET_FPS) / 1000U)
#define BOOT_LCD_LAYER         0U

#define C_BLACK                0x0000U
#define C_BG0                  0x0002U
#define C_BG1                  0x0008U
#define C_GRID                 0x1084U
#define C_DIM                  0x39E7U
#define C_WHITE                0xEFFFU
#define C_CYAN                 0x07FFU
#define C_CYAN_DARK            0x0390U
#define C_MAGENTA              0xF81FU
#define C_MAGENTA_DARK         0x8010U
#define C_AMBER                0xFEA0U
#define C_BLUE                 0x041FU

#define BOOT_PARTICLE_COUNT    34U

typedef struct {
  uint16_t seed;
  int16_t yoff;
  uint8_t speed;
} boot_particle_t;

static const int16_t s_sin_q15_0_90[91] = {
  0, 572, 1144, 1715, 2286, 2856, 3425, 3993, 4560, 5126, 5690, 6252, 6813, 7371, 7927, 8481, 9032, 9580, 10126, 10668, 11207, 11743, 12275, 12803, 13328, 13848, 14364, 14876, 15383, 15886, 16383, 16876, 17364, 17846, 18323, 18794, 19260, 19720, 20173, 20621, 21062, 21497, 21925, 22347, 22762, 23170, 23571, 23964, 24351, 24730, 25101, 25465, 25821, 26169, 26509, 26841, 27165, 27481, 27788, 28087, 28377, 28659, 28932, 29196, 29451, 29697, 29934, 30162, 30381, 30591, 30791, 30982, 31163, 31335, 31498, 31650, 31794, 31927, 32051, 32165, 32269, 32364, 32448, 32523, 32587, 32642, 32687, 32722, 32747, 32762, 32767
};

static const boot_particle_t s_particles[BOOT_PARTICLE_COUNT] = {
  {  17U, -44,  7U}, {  91U,  28, 10U}, { 149U, -18,  8U}, { 213U,  42, 11U},
  { 287U, -26,  9U}, { 341U,  14,  7U}, { 409U, -52, 12U}, { 463U,  36, 10U},
  { 521U, -10,  8U}, { 587U,  50, 11U}, { 641U, -34,  9U}, { 701U,  20,  7U},
  { 769U, -46, 10U}, { 823U,  32,  8U}, { 887U, -22, 12U}, { 941U,  44,  9U},
  {1009U, -30,  7U}, {1063U,  12, 10U}, {1129U, -54,  8U}, {1187U,  38, 11U},
  {1249U, -16,  9U}, {1301U,  48,  7U}, {1367U, -36, 10U}, {1423U,  24,  8U},
  {1481U, -48, 12U}, {1543U,  30,  9U}, {1601U, -20,  7U}, {1667U,  40, 10U},
  {1721U, -38,  8U}, {1789U,  18, 11U}, {1847U, -50,  9U}, {1901U,  34,  7U},
  {1973U, -24, 10U}, {2039U,  46,  8U}
};

static uint32_t g_boot_last_display_addr = APP_FB_ADDR;
static uint32_t g_boot_missed_frames = 0U;
static uint32_t g_boot_max_render_ms = 0U;
static uint32_t g_boot_start_tick = 0U;

static uint16_t app_boot_rgb565(uint8_t r, uint8_t g, uint8_t b);
static uint16_t app_boot_blend(uint16_t dst, uint16_t src, uint8_t alpha);
static int16_t app_boot_sin_q15(int32_t deg);
static int16_t app_boot_cos_q15(int32_t deg);
static void app_boot_clean_dcache(uint32_t addr, uint32_t size);
static void app_boot_set_ltdc_addr(uint32_t addr);
static void app_boot_put(uint16_t *fb, int32_t x, int32_t y, uint16_t color);
static void app_boot_put_alpha(uint16_t *fb, int32_t x, int32_t y, uint16_t color, uint8_t alpha);
static void app_boot_hline(uint16_t *fb, int32_t x0, int32_t x1, int32_t y, uint16_t color);
static void app_boot_vline(uint16_t *fb, int32_t x, int32_t y0, int32_t y1, uint16_t color);
static void app_boot_line(uint16_t *fb, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color);
static void app_boot_rect(uint16_t *fb, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
static void app_boot_circle(uint16_t *fb, int32_t cx, int32_t cy, int32_t r, uint16_t color);
static void app_boot_disc(uint16_t *fb, int32_t cx, int32_t cy, int32_t r, uint16_t color);
static void app_boot_arc(uint16_t *fb, int32_t cx, int32_t cy, int32_t r, int32_t a0, int32_t a1, uint16_t color, uint8_t thick);
static const uint8_t *app_boot_font_get(char c);
static void app_boot_text(uint16_t *fb, int32_t x, int32_t y, const char *s, uint16_t color, uint8_t scale);
static void app_boot_draw_static(uint16_t *fb, uint32_t frame);
static void app_boot_draw_corners(uint16_t *fb);
static void app_boot_draw_wave(uint16_t *fb, uint32_t frame);
static void app_boot_draw_core(uint16_t *fb, uint32_t frame, uint32_t progress);
static void app_boot_draw_panels(uint16_t *fb, uint32_t frame, uint32_t progress);
static void app_boot_draw_particles(uint16_t *fb, uint32_t frame);

static void app_boot_copy_final_to_lvgl_fb(void);
static void app_boot_clear_lvgl_fb(void);
static uint8_t app_boot_should_abort(void);
static uint8_t app_boot_wait_next_frame_or_abort(uint32_t next_tick);

void App_BootAnimPlay(void)
{
#if APP_BOOT_ANIM_ENABLE
  uint32_t frame;
  uint32_t next_tick;
  uint32_t draw_addr;
  uint32_t t0;
  uint32_t cost;
  uint32_t progress;
  uint16_t *fb;
  uint8_t aborted = 0U;

  g_boot_missed_frames = 0U;
  g_boot_max_render_ms = 0U;
  g_boot_start_tick = osKernelGetTickCount();
  next_tick = g_boot_start_tick;
  draw_addr = APP_BOOT_FB_ADDR;

#if APP_BOOT_ANIM_ABORT_ON_TOUCH_ENABLE
  /* 清掉上电/GT9xx 复位阶段可能产生的旧中断和旧坐标。 */
  (void)App_TouchConsumeInterruptFlag();
  App_TouchClearState();
#endif

  for(frame = 0U; frame < BOOT_TOTAL_FRAMES; frame++) {
    if(app_boot_should_abort() != 0U) {
      aborted = 1U;
      break;
    }

    t0 = osKernelGetTickCount();
    progress = (frame * 1000U) / (BOOT_TOTAL_FRAMES - 1U);
    fb = (uint16_t *)draw_addr;

    app_boot_draw_static(fb, frame);
    app_boot_draw_wave(fb, frame);
    app_boot_draw_core(fb, frame, progress);
    app_boot_draw_panels(fb, frame, progress);
    app_boot_draw_particles(fb, frame);

    app_boot_clean_dcache(draw_addr, APP_FB_SIZE_BYTES);
    app_boot_set_ltdc_addr(draw_addr);
    g_boot_last_display_addr = draw_addr;

    cost = osKernelGetTickCount() - t0;
    if(cost > g_boot_max_render_ms) {
      g_boot_max_render_ms = cost;
    }
    if(cost > BOOT_FRAME_MS) {
      g_boot_missed_frames++;
    }

    draw_addr = (draw_addr == APP_FB_ADDR) ? APP_BOOT_FB_ADDR : APP_FB_ADDR;
    next_tick += BOOT_FRAME_MS;
    if(app_boot_wait_next_frame_or_abort(next_tick) != 0U) {
      aborted = 1U;
      break;
    }
  }

  if(aborted != 0U) {
#if APP_BOOT_ANIM_CLEAR_ON_ABORT
    app_boot_clear_lvgl_fb();
#endif
    app_boot_set_ltdc_addr(APP_FB_ADDR);
  } else {
    app_boot_copy_final_to_lvgl_fb();
    app_boot_set_ltdc_addr(APP_FB_ADDR);
  }
#else
  (void)g_boot_last_display_addr;
#endif
}

static uint16_t app_boot_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
  return (uint16_t)((((uint16_t)r & 0xF8U) << 8) |
                    (((uint16_t)g & 0xFCU) << 3) |
                    (((uint16_t)b & 0xF8U) >> 3));
}

static uint16_t app_boot_blend(uint16_t dst, uint16_t src, uint8_t alpha)
{
  uint32_t sr;
  uint32_t sg;
  uint32_t sb;
  uint32_t dr;
  uint32_t dg;
  uint32_t db;
  uint32_t inv;

  if(alpha == 255U) {
    return src;
  }
  if(alpha == 0U) {
    return dst;
  }

  sr = (src >> 11) & 0x1FU;
  sg = (src >> 5) & 0x3FU;
  sb = src & 0x1FU;
  dr = (dst >> 11) & 0x1FU;
  dg = (dst >> 5) & 0x3FU;
  db = dst & 0x1FU;
  inv = 255U - alpha;

  dr = ((sr * alpha) + (dr * inv)) / 255U;
  dg = ((sg * alpha) + (dg * inv)) / 255U;
  db = ((sb * alpha) + (db * inv)) / 255U;

  return (uint16_t)((dr << 11) | (dg << 5) | db);
}

static int16_t app_boot_sin_q15(int32_t deg)
{
  int32_t d;
  int32_t sign;

  d = deg % 360;
  if(d < 0) {
    d += 360;
  }

  sign = 1;
  if(d >= 180) {
    d -= 180;
    sign = -1;
  }
  if(d > 90) {
    d = 180 - d;
  }

  return (int16_t)(sign * s_sin_q15_0_90[d]);
}

static int16_t app_boot_cos_q15(int32_t deg)
{
  return app_boot_sin_q15(deg + 90);
}

static void app_boot_clean_dcache(uint32_t addr, uint32_t size)
{
  uint32_t aligned_addr;
  uint32_t aligned_size;

  aligned_addr = addr & ~31UL;
  aligned_size = ((addr + size + 31UL) & ~31UL) - aligned_addr;
  SCB_CleanDCache_by_Addr((uint32_t *)aligned_addr, (int32_t)aligned_size);
}

static void app_boot_set_ltdc_addr(uint32_t addr)
{
  (void)HAL_LTDC_SetAddress(&hltdc, addr, BOOT_LCD_LAYER);
}

static void app_boot_put(uint16_t *fb, int32_t x, int32_t y, uint16_t color)
{
  if((x < 0) || (y < 0) || (x >= (int32_t)BOOT_W) || (y >= (int32_t)BOOT_H)) {
    return;
  }
  fb[(uint32_t)y * BOOT_W + (uint32_t)x] = color;
}

static void app_boot_put_alpha(uint16_t *fb, int32_t x, int32_t y, uint16_t color, uint8_t alpha)
{
  uint32_t idx;

  if((x < 0) || (y < 0) || (x >= (int32_t)BOOT_W) || (y >= (int32_t)BOOT_H)) {
    return;
  }
  idx = (uint32_t)y * BOOT_W + (uint32_t)x;
  fb[idx] = app_boot_blend(fb[idx], color, alpha);
}

static void app_boot_hline(uint16_t *fb, int32_t x0, int32_t x1, int32_t y, uint16_t color)
{
  int32_t x;
  int32_t t;

  if((y < 0) || (y >= (int32_t)BOOT_H)) {
    return;
  }
  if(x0 > x1) {
    t = x0; x0 = x1; x1 = t;
  }
  if(x0 < 0) { x0 = 0; }
  if(x1 >= (int32_t)BOOT_W) { x1 = (int32_t)BOOT_W - 1; }
  for(x = x0; x <= x1; x++) {
    fb[(uint32_t)y * BOOT_W + (uint32_t)x] = color;
  }
}

static void app_boot_vline(uint16_t *fb, int32_t x, int32_t y0, int32_t y1, uint16_t color)
{
  int32_t y;
  int32_t t;

  if((x < 0) || (x >= (int32_t)BOOT_W)) {
    return;
  }
  if(y0 > y1) {
    t = y0; y0 = y1; y1 = t;
  }
  if(y0 < 0) { y0 = 0; }
  if(y1 >= (int32_t)BOOT_H) { y1 = (int32_t)BOOT_H - 1; }
  for(y = y0; y <= y1; y++) {
    fb[(uint32_t)y * BOOT_W + (uint32_t)x] = color;
  }
}

static void app_boot_line(uint16_t *fb, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color)
{
  int32_t dx;
  int32_t sx;
  int32_t dy;
  int32_t sy;
  int32_t err;
  int32_t e2;

  dx = (x0 < x1) ? (x1 - x0) : (x0 - x1);
  sx = (x0 < x1) ? 1 : -1;
  dy = (y0 < y1) ? (y0 - y1) : (y1 - y0);
  sy = (y0 < y1) ? 1 : -1;
  err = dx + dy;

  for(;;) {
    app_boot_put(fb, x0, y0, color);
    if((x0 == x1) && (y0 == y1)) {
      break;
    }
    e2 = 2 * err;
    if(e2 >= dy) { err += dy; x0 += sx; }
    if(e2 <= dx) { err += dx; y0 += sy; }
  }
}

static void app_boot_rect(uint16_t *fb, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color)
{
  app_boot_hline(fb, x, x + w - 1, y, color);
  app_boot_hline(fb, x, x + w - 1, y + h - 1, color);
  app_boot_vline(fb, x, y, y + h - 1, color);
  app_boot_vline(fb, x + w - 1, y, y + h - 1, color);
}

static void app_boot_circle(uint16_t *fb, int32_t cx, int32_t cy, int32_t r, uint16_t color)
{
  int32_t x;
  int32_t y;
  int32_t err;

  x = -r;
  y = 0;
  err = 2 - (2 * r);
  do {
    app_boot_put(fb, cx - x, cy + y, color);
    app_boot_put(fb, cx - y, cy - x, color);
    app_boot_put(fb, cx + x, cy - y, color);
    app_boot_put(fb, cx + y, cy + x, color);
    r = err;
    if(r <= y) { err += (++y * 2) + 1; }
    if((r > x) || (err > y)) { err += (++x * 2) + 1; }
  } while(x < 0);
}

static void app_boot_disc(uint16_t *fb, int32_t cx, int32_t cy, int32_t r, uint16_t color)
{
  int32_t y;
  int32_t x;
  int32_t rr;

  rr = r * r;
  for(y = -r; y <= r; y++) {
    for(x = -r; x <= r; x++) {
      if(((x * x) + (y * y)) <= rr) {
        app_boot_put(fb, cx + x, cy + y, color);
      }
    }
  }
}

static void app_boot_arc(uint16_t *fb, int32_t cx, int32_t cy, int32_t r, int32_t a0, int32_t a1, uint16_t color, uint8_t thick)
{
  int32_t a;
  int32_t prev_x;
  int32_t prev_y;
  int32_t x;
  int32_t y;
  int32_t rr;
  uint8_t k;

  prev_x = cx + (r * app_boot_cos_q15(a0)) / 32767;
  prev_y = cy + (r * app_boot_sin_q15(a0)) / 32767;
  for(a = a0 + 3; a <= a1; a += 3) {
    x = cx + (r * app_boot_cos_q15(a)) / 32767;
    y = cy + (r * app_boot_sin_q15(a)) / 32767;
    for(k = 0U; k < thick; k++) {
      rr = r - (int32_t)k;
      x = cx + (rr * app_boot_cos_q15(a)) / 32767;
      y = cy + (rr * app_boot_sin_q15(a)) / 32767;
      app_boot_line(fb, prev_x, prev_y, x, y, color);
    }
    prev_x = cx + (r * app_boot_cos_q15(a)) / 32767;
    prev_y = cy + (r * app_boot_sin_q15(a)) / 32767;
  }
}

static const uint8_t *app_boot_font_get(char c)
{
  if((c >= 'a') && (c <= 'z')) {
    c = (char)(c - ('a' - 'A'));
  }
  switch(c) {
    case ' ': { static const uint8_t g[7] = {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U}; return g; }
    case '%': { static const uint8_t g[7] = {0x18U, 0x19U, 0x02U, 0x04U, 0x08U, 0x13U, 0x03U}; return g; }
    case '+': { static const uint8_t g[7] = {0x00U, 0x04U, 0x04U, 0x1FU, 0x04U, 0x04U, 0x00U}; return g; }
    case '-': { static const uint8_t g[7] = {0x00U, 0x00U, 0x00U, 0x1FU, 0x00U, 0x00U, 0x00U}; return g; }
    case '.': { static const uint8_t g[7] = {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x0CU, 0x0CU}; return g; }
    case '/': { static const uint8_t g[7] = {0x01U, 0x02U, 0x02U, 0x04U, 0x08U, 0x08U, 0x10U}; return g; }
    case '0': { static const uint8_t g[7] = {0x0EU, 0x11U, 0x13U, 0x15U, 0x19U, 0x11U, 0x0EU}; return g; }
    case '1': { static const uint8_t g[7] = {0x04U, 0x0CU, 0x04U, 0x04U, 0x04U, 0x04U, 0x0EU}; return g; }
    case '2': { static const uint8_t g[7] = {0x0EU, 0x11U, 0x01U, 0x02U, 0x04U, 0x08U, 0x1FU}; return g; }
    case '3': { static const uint8_t g[7] = {0x1FU, 0x02U, 0x04U, 0x02U, 0x01U, 0x11U, 0x0EU}; return g; }
    case '4': { static const uint8_t g[7] = {0x02U, 0x06U, 0x0AU, 0x12U, 0x1FU, 0x02U, 0x02U}; return g; }
    case '5': { static const uint8_t g[7] = {0x1FU, 0x10U, 0x1EU, 0x01U, 0x01U, 0x11U, 0x0EU}; return g; }
    case '6': { static const uint8_t g[7] = {0x06U, 0x08U, 0x10U, 0x1EU, 0x11U, 0x11U, 0x0EU}; return g; }
    case '7': { static const uint8_t g[7] = {0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x08U, 0x08U}; return g; }
    case '8': { static const uint8_t g[7] = {0x0EU, 0x11U, 0x11U, 0x0EU, 0x11U, 0x11U, 0x0EU}; return g; }
    case '9': { static const uint8_t g[7] = {0x0EU, 0x11U, 0x11U, 0x0FU, 0x01U, 0x02U, 0x0CU}; return g; }
    case ':': { static const uint8_t g[7] = {0x00U, 0x04U, 0x04U, 0x00U, 0x04U, 0x04U, 0x00U}; return g; }
    case 'A': { static const uint8_t g[7] = {0x0EU, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U}; return g; }
    case 'B': { static const uint8_t g[7] = {0x1EU, 0x11U, 0x11U, 0x1EU, 0x11U, 0x11U, 0x1EU}; return g; }
    case 'C': { static const uint8_t g[7] = {0x0EU, 0x11U, 0x10U, 0x10U, 0x10U, 0x11U, 0x0EU}; return g; }
    case 'D': { static const uint8_t g[7] = {0x1EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x1EU}; return g; }
    case 'E': { static const uint8_t g[7] = {0x1FU, 0x10U, 0x10U, 0x1EU, 0x10U, 0x10U, 0x1FU}; return g; }
    case 'F': { static const uint8_t g[7] = {0x1FU, 0x10U, 0x10U, 0x1EU, 0x10U, 0x10U, 0x10U}; return g; }
    case 'G': { static const uint8_t g[7] = {0x0EU, 0x11U, 0x10U, 0x17U, 0x11U, 0x11U, 0x0FU}; return g; }
    case 'H': { static const uint8_t g[7] = {0x11U, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U}; return g; }
    case 'I': { static const uint8_t g[7] = {0x0EU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x0EU}; return g; }
    case 'J': { static const uint8_t g[7] = {0x07U, 0x02U, 0x02U, 0x02U, 0x12U, 0x12U, 0x0CU}; return g; }
    case 'K': { static const uint8_t g[7] = {0x11U, 0x12U, 0x14U, 0x18U, 0x14U, 0x12U, 0x11U}; return g; }
    case 'L': { static const uint8_t g[7] = {0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x1FU}; return g; }
    case 'M': { static const uint8_t g[7] = {0x11U, 0x1BU, 0x15U, 0x15U, 0x11U, 0x11U, 0x11U}; return g; }
    case 'N': { static const uint8_t g[7] = {0x11U, 0x19U, 0x15U, 0x13U, 0x11U, 0x11U, 0x11U}; return g; }
    case 'O': { static const uint8_t g[7] = {0x0EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU}; return g; }
    case 'P': { static const uint8_t g[7] = {0x1EU, 0x11U, 0x11U, 0x1EU, 0x10U, 0x10U, 0x10U}; return g; }
    case 'Q': { static const uint8_t g[7] = {0x0EU, 0x11U, 0x11U, 0x11U, 0x15U, 0x12U, 0x0DU}; return g; }
    case 'R': { static const uint8_t g[7] = {0x1EU, 0x11U, 0x11U, 0x1EU, 0x14U, 0x12U, 0x11U}; return g; }
    case 'S': { static const uint8_t g[7] = {0x0FU, 0x10U, 0x10U, 0x0EU, 0x01U, 0x01U, 0x1EU}; return g; }
    case 'T': { static const uint8_t g[7] = {0x1FU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U}; return g; }
    case 'U': { static const uint8_t g[7] = {0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU}; return g; }
    case 'V': { static const uint8_t g[7] = {0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0AU, 0x04U}; return g; }
    case 'W': { static const uint8_t g[7] = {0x11U, 0x11U, 0x11U, 0x15U, 0x15U, 0x15U, 0x0AU}; return g; }
    case 'X': { static const uint8_t g[7] = {0x11U, 0x11U, 0x0AU, 0x04U, 0x0AU, 0x11U, 0x11U}; return g; }
    case 'Y': { static const uint8_t g[7] = {0x11U, 0x11U, 0x0AU, 0x04U, 0x04U, 0x04U, 0x04U}; return g; }
    case 'Z': { static const uint8_t g[7] = {0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x1FU}; return g; }
    case '_': { static const uint8_t g[7] = {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x1FU}; return g; }
    default: { static const uint8_t g[7] = {0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U}; return g; }
  }
}

static void app_boot_text(uint16_t *fb, int32_t x, int32_t y, const char *s, uint16_t color, uint8_t scale)
{
  const uint8_t *g;
  uint32_t row;
  uint32_t col;
  uint8_t sx;
  uint8_t sy;
  int32_t ox;

  ox = x;
  while(*s != '\0') {
    if(*s == '\n') {
      y += (int32_t)(8U * scale);
      x = ox;
      s++;
      continue;
    }
    g = app_boot_font_get(*s);
    for(row = 0U; row < 7U; row++) {
      for(col = 0U; col < 5U; col++) {
        if((g[row] & (1U << (4U - col))) != 0U) {
          for(sy = 0U; sy < scale; sy++) {
            for(sx = 0U; sx < scale; sx++) {
              app_boot_put(fb,
                           x + ((int32_t)col * (int32_t)scale) + sx,
                           y + ((int32_t)row * (int32_t)scale) + sy,
                           color);
            }
          }
        }
      }
    }
    x += (int32_t)(6U * scale);
    s++;
  }
}

static void app_boot_draw_static(uint16_t *fb, uint32_t frame)
{
  uint32_t y;
  uint32_t x;
  uint8_t blue;
  uint16_t color;

  for(y = 0U; y < BOOT_H; y++) {
    blue = (uint8_t)(4U + ((y * 18U) / BOOT_H));
    color = app_boot_rgb565(0U, (uint8_t)(2U + (y / 64U)), blue);
    for(x = 0U; x < BOOT_W; x++) {
      fb[y * BOOT_W + x] = color;
    }
  }

  for(x = 0U; x < BOOT_W; x += 32U) {
    app_boot_vline(fb, (int32_t)x, 310, 468, app_boot_blend(C_BG1, C_GRID, 75U));
  }
  for(y = 320U; y < 470U; y += 24U) {
    app_boot_hline(fb, 0, (int32_t)BOOT_W - 1, (int32_t)y, app_boot_blend(C_BG1, C_GRID, 70U));
  }

  app_boot_draw_corners(fb);
  app_boot_text(fb, 28, 26, "SYSTEM BOOT", C_AMBER, 1U);
  app_boot_text(fb, 28, 42, "INITIALIZING", C_DIM, 1U);
  for(x = 0U; x < 12U; x++) {
    app_boot_disc(fb, 95 + (int32_t)(x * 9U), 41, 2, (x < ((frame / 5U) % 13U)) ? C_AMBER : C_DIM);
  }

  app_boot_text(fb, 26, 132, "TIME-DOMAIN SIGNAL", C_WHITE, 1U);
  app_boot_hline(fb, 21, 28, 126, C_CYAN_DARK);
  app_boot_hline(fb, 105, 112, 126, C_CYAN_DARK);
  app_boot_text(fb, 28, 432, "IQ DEMODULATION ENGINE", C_WHITE, 1U);
  app_boot_text(fb, 652, 432, "STATUS: BOOTING", C_AMBER, 1U);
}

static void app_boot_draw_corners(uint16_t *fb)
{
  app_boot_hline(fb, 14, 70, 16, C_DIM);
  app_boot_vline(fb, 14, 16, 34, C_DIM);
  app_boot_line(fb, 70, 16, 132, 16, C_DIM);
  app_boot_hline(fb, 608, 782, 16, C_DIM);
  app_boot_vline(fb, 782, 16, 34, C_DIM);
  app_boot_hline(fb, 14, 250, 448, C_DIM);
  app_boot_vline(fb, 14, 428, 448, C_DIM);
  app_boot_hline(fb, 540, 782, 448, C_DIM);
  app_boot_vline(fb, 782, 428, 448, C_DIM);
}

static void app_boot_draw_wave(uint16_t *fb, uint32_t frame)
{
  int32_t x;
  int32_t y;
  int32_t y_prev;
  int32_t amp;
  int32_t ph;
  int32_t cx;
  int32_t cy;
  int32_t px;
  int32_t py;
  int32_t nx;
  int32_t ny;
  int32_t k;

  y_prev = 240;
  for(x = 0; x < 292; x += 3) {
    amp = 44 - (x / 9);
    if(amp < 6) { amp = 6; }
    ph = (x * 10) + ((int32_t)frame * 14);
    y = 240 + ((amp * app_boot_sin_q15(ph)) / 32767)
            + (((amp / 2) * app_boot_sin_q15((x * 27) - ((int32_t)frame * 9))) / 32767);
    if(x > 0) {
      app_boot_line(fb, x - 3, y_prev, x, y, C_CYAN);
      app_boot_put_alpha(fb, x, y - 1, C_WHITE, 120U);
      app_boot_put_alpha(fb, x, y + 1, C_CYAN, 150U);
    }
    y_prev = y;
  }

  cx = 394;
  cy = 240;
  px = 292;
  py = 240;
  for(k = 0; k <= 80; k += 4) {
    nx = 292 + ((cx - 292) * k) / 80;
    ny = cy - ((k * k) / 82) + ((6 * app_boot_sin_q15((int32_t)frame * 8 + k * 5)) / 32767);
    app_boot_line(fb, px, py, nx, ny, C_CYAN);
    px = nx; py = ny;
  }
  px = 292; py = 240;
  for(k = 0; k <= 80; k += 4) {
    nx = 292 + ((cx - 292) * k) / 80;
    ny = cy + ((k * k) / 82) + ((6 * app_boot_sin_q15((int32_t)frame * 8 + k * 5)) / 32767);
    app_boot_line(fb, px, py, nx, ny, C_MAGENTA);
    px = nx; py = ny;
  }
}

static void app_boot_draw_core(uint16_t *fb, uint32_t frame, uint32_t progress)
{
  int32_t cx;
  int32_t cy;
  int32_t r;
  int32_t a;
  int32_t sweep;
  int32_t ycur;

  cx = 398;
  cy = 240;
  r = 154;

  app_boot_circle(fb, cx, cy, 74, C_CYAN_DARK);
  app_boot_circle(fb, cx, cy, 112, C_DIM);
  app_boot_circle(fb, cx, cy, 138, C_CYAN_DARK);
  app_boot_circle(fb, cx, cy, 154, C_DIM);

  app_boot_hline(fb, cx - 160, cx + 160, cy, C_DIM);
  app_boot_vline(fb, cx, cy - 142, cy + 142, C_DIM);
  app_boot_vline(fb, cx, cy - 112, cy - 42, C_CYAN);
  app_boot_vline(fb, cx, cy + 42, cy + 112, C_MAGENTA);

  for(a = 0; a < 360; a += 12) {
    int32_t x0 = cx + ((r - 10) * app_boot_cos_q15(a)) / 32767;
    int32_t y0 = cy + ((r - 10) * app_boot_sin_q15(a)) / 32767;
    int32_t x1 = cx + ((r - 4) * app_boot_cos_q15(a)) / 32767;
    int32_t y1 = cy + ((r - 4) * app_boot_sin_q15(a)) / 32767;
    app_boot_line(fb, x0, y0, x1, y1, (a < 180) ? C_CYAN_DARK : C_MAGENTA_DARK);
  }

  if(progress < 210U) {
    sweep = -90 + (int32_t)((progress * 360U) / 210U);
    app_boot_arc(fb, cx, cy, r - 2, -90, sweep, C_CYAN, 4U);
  } else {
    app_boot_arc(fb, cx, cy, r - 2, -220, -20, C_CYAN, 3U);
    app_boot_arc(fb, cx, cy, r - 2, -20, 140, C_MAGENTA, 3U);
    sweep = -90 + (int32_t)((frame * 5U) % 360U);
    app_boot_arc(fb, cx, cy, r + 6, sweep - 22, sweep, C_WHITE, 2U);
  }

  ycur = cy - 102 + ((204 * (32767 + app_boot_sin_q15((int32_t)frame * 30))) / (2 * 32767));
  app_boot_disc(fb, cx, ycur, 4, C_CYAN);
  app_boot_put_alpha(fb, cx, ycur - 7, C_CYAN, 110U);
  app_boot_put_alpha(fb, cx, ycur + 7, C_CYAN, 110U);

  app_boot_disc(fb, cx, cy, 13, C_BG1);
  app_boot_circle(fb, cx, cy, 13, C_WHITE);
  app_boot_disc(fb, cx, cy, 4, C_WHITE);
  app_boot_text(fb, cx - 18, cy - 25, "IQ", C_WHITE, 2U);
  if(progress < 740U) {
    app_boot_text(fb, cx - 49, cy + 4, "DEMODULATING", C_WHITE, 1U);
  } else {
    app_boot_text(fb, cx - 51, cy + 4, "DEMODULATION", C_WHITE, 1U);
    app_boot_text(fb, cx - 25, cy + 20, "READY", C_CYAN, 1U);
  }

  app_boot_text(fb, cx - 4, cy - 122, "I", C_CYAN, 2U);
  app_boot_text(fb, cx - 8, cy + 110, "Q", C_MAGENTA, 2U);
  app_boot_text(fb, cx + 88, cy - 2, "+RE", C_WHITE, 1U);
  app_boot_text(fb, cx + 6, cy - 90, "+IM", C_CYAN, 1U);
}

static void app_boot_draw_panels(uint16_t *fb, uint32_t frame, uint32_t progress)
{
  int32_t x;
  int32_t prevx;
  int32_t prevy;
  int32_t y;
  int32_t i;
  int32_t level;
  int32_t qx;
  uint16_t col;

  (void)frame;
  app_boot_text(fb, 570, 91, "SPECTRUM", C_WHITE, 1U);
  app_boot_rect(fb, 560, 106, 210, 76, C_DIM);
  app_boot_text(fb, 570, 198, "I CHANNEL", C_CYAN, 1U);
  app_boot_rect(fb, 560, 213, 210, 62, C_DIM);
  app_boot_text(fb, 570, 295, "Q CHANNEL", C_MAGENTA, 1U);
  app_boot_rect(fb, 560, 310, 210, 62, C_DIM);

  for(x = 570; x < 760; x += 16) {
    app_boot_vline(fb, x, 112, 176, C_BG1);
    app_boot_vline(fb, x, 219, 269, C_BG1);
    app_boot_vline(fb, x, 316, 366, C_BG1);
  }
  for(y = 122; y < 176; y += 14) { app_boot_hline(fb, 566, 764, y, C_BG1); }
  for(y = 226; y < 269; y += 13) { app_boot_hline(fb, 566, 764, y, C_BG1); }
  for(y = 323; y < 366; y += 13) { app_boot_hline(fb, 566, 764, y, C_BG1); }

  level = (progress < 300U) ? 0 : (int32_t)((progress - 300U) * 1000U / 700U);
  if(level > 1000) { level = 1000; }

  prevx = 568;
  prevy = 171;
  for(i = 0; i <= 188; i += 4) {
    int32_t center = (i - 94);
    int32_t peak = 0;
    peak += 42 - ((center < 0 ? -center : center) / 2);
    if(peak < 0) { peak = 0; }
    if((i % 37) < 4) { peak += 18; }
    if((i % 61) < 4) { peak += 24; }
    peak = (peak * level) / 1000;
    x = 568 + i;
    y = 171 - peak;
    col = (i < 94) ? C_CYAN : C_MAGENTA;
    app_boot_line(fb, prevx, prevy, x, y, col);
    prevx = x; prevy = y;
  }

  prevx = 568; prevy = 263;
  for(i = 0; i <= 188; i += 4) {
    int32_t v = 6 + ((20 * (32767 + app_boot_sin_q15(i * 18 + (int32_t)frame * 19))) / 65534);
    if((i % 53) < 4) { v += 26; }
    v = (v * (400 + level)) / 1400;
    x = 568 + i; y = 263 - v;
    app_boot_line(fb, prevx, prevy, x, y, C_CYAN);
    prevx = x; prevy = y;
  }

  prevx = 568; prevy = 360;
  for(i = 0; i <= 188; i += 4) {
    int32_t v = 5 + ((18 * (32767 + app_boot_sin_q15(i * 23 - (int32_t)frame * 17))) / 65534);
    if((i % 47) < 4) { v += 25; }
    v = (v * (400 + level)) / 1400;
    x = 568 + i; y = 360 - v;
    app_boot_line(fb, prevx, prevy, x, y, C_MAGENTA);
    prevx = x; prevy = y;
  }

  qx = 568 + (int32_t)((188U * (uint32_t)(32767 + app_boot_sin_q15((int32_t)frame * 24))) / 65534U);
  app_boot_vline(fb, qx, 316, 366, C_MAGENTA);
  app_boot_vline(fb, qx - 1, 323, 360, C_MAGENTA_DARK);
}

static void app_boot_draw_particles(uint16_t *fb, uint32_t frame)
{
  uint32_t i;
  int32_t x;
  int32_t y;
  int32_t p;
  int32_t env;
  const boot_particle_t *pt;

  for(i = 0U; i < BOOT_PARTICLE_COUNT; i++) {
    pt = &s_particles[i];
    p = (int32_t)((pt->seed + (frame * pt->speed * 3U)) % 1000U);
    x = (p * 390) / 1000;
    env = (32767 + app_boot_sin_q15((p * 180) / 1000)) / 2;
    y = 240 + ((pt->yoff * env) / 32767);
    app_boot_put_alpha(fb, x, y, (p < 820) ? C_CYAN : C_MAGENTA, 190U);
    app_boot_put_alpha(fb, x + 1, y, (p < 820) ? C_CYAN : C_MAGENTA, 95U);
  }
}

static void app_boot_clear_lvgl_fb(void)
{
  uint32_t i;
  uint16_t *fb = (uint16_t *)APP_FB_ADDR;

  for(i = 0U; i < (APP_FB_SIZE_BYTES / APP_LCD_PIXEL_SIZE_BYTES); i++) {
    fb[i] = C_BLACK;
  }
  app_boot_clean_dcache(APP_FB_ADDR, APP_FB_SIZE_BYTES);
}

static uint8_t app_boot_should_abort(void)
{
#if APP_BOOT_ANIM_ABORT_ON_TOUCH_ENABLE
  uint32_t elapsed;

  elapsed = osKernelGetTickCount() - g_boot_start_tick;

  /*
   * 上电初期 GT9xx 复位和 INT 脚模式切换容易产生假边沿。
   * 保护期内只清状态，不允许触摸打断动画。
   */
  if(elapsed < APP_BOOT_ANIM_TOUCH_ARM_MS) {
    (void)App_TouchConsumeInterruptFlag();
    App_TouchClearState();
    return 0U;
  }

  if(App_TouchIsInitialized() == false) {
    (void)App_TouchConsumeInterruptFlag();
    App_TouchClearState();
    return 0U;
  }

  if(App_TouchHasValidTouch() != false) {
    return 1U;
  }

#if APP_BOOT_ANIM_ABORT_BY_RAW_IRQ_ENABLE
  if(App_TouchConsumeInterruptFlag()) {
    return 1U;
  }
#else
  /* 默认只消费裸 INT，不把它直接当成有效触摸。 */
  (void)App_TouchConsumeInterruptFlag();
#endif
#endif
  return 0U;
}

static uint8_t app_boot_wait_next_frame_or_abort(uint32_t next_tick)
{
  uint32_t now;
  uint32_t remain;

  for(;;) {
    if(app_boot_should_abort() != 0U) {
      return 1U;
    }

    now = osKernelGetTickCount();
    if((int32_t)(next_tick - now) <= 0) {
      break;
    }

    remain = next_tick - now;
    if(remain > APP_BOOT_ANIM_ABORT_POLL_MS) {
      remain = APP_BOOT_ANIM_ABORT_POLL_MS;
    }
    (void)osDelay(remain);
  }

  return 0U;
}

static void app_boot_copy_final_to_lvgl_fb(void)
{
  if(g_boot_last_display_addr != APP_FB_ADDR) {
    memcpy((void *)APP_FB_ADDR, (const void *)g_boot_last_display_addr, APP_FB_SIZE_BYTES);
    app_boot_clean_dcache(APP_FB_ADDR, APP_FB_SIZE_BYTES);
  } else {
    app_boot_clean_dcache(APP_FB_ADDR, APP_FB_SIZE_BYTES);
  }
}
