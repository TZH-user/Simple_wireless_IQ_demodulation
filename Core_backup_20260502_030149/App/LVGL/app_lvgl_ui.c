#include "app_lvgl_ui.h"

#include <stdio.h>

#include "cmsis_os2.h"
#include "app_signal_detect.h"
#include "lvgl.h"

#define UI_REFRESH_PERIOD_MS 200U

typedef struct
{
  lv_obj_t *freq_value;
  lv_obj_t *scan_status;
  lv_obj_t *quality_line;
  lv_obj_t *range_line;
  lv_obj_t *mod_value;
  lv_obj_t *demod_value;
  lv_obj_t *spectrum_value;
  uint32_t last_refresh_tick;
} app_lvgl_ui_ctx_t;

static app_lvgl_ui_ctx_t g_ui;

static lv_obj_t *App_LvglUiCreateCard(lv_obj_t *parent,
                                      lv_coord_t x,
                                      lv_coord_t y,
                                      lv_coord_t w,
                                      lv_coord_t h,
                                      lv_color_t bg,
                                      lv_color_t border);
static lv_obj_t *App_LvglUiCreateCardTitle(lv_obj_t *parent, const char *title);
static void App_LvglUiFormatFreq(char *out, uint32_t out_len, uint32_t hz);

void App_LvglUiInit(void)
{
  lv_obj_t *screen = lv_screen_active();
  lv_obj_t *main_card;
  lv_obj_t *side_top_card;
  lv_obj_t *side_mid_card;
  lv_obj_t *bottom_card;
  lv_obj_t *label;

  lv_obj_clean(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B1220), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  label = lv_label_create(screen);
  lv_label_set_text(label, "Simple Wireless Auto Receiver");
  lv_obj_set_style_text_color(label, lv_color_hex(0xD8E6FF), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 16, 10);

  g_ui.range_line = lv_label_create(screen);
  lv_label_set_text_fmt(g_ui.range_line,
                        "Scan Range: %lu.%03lu-%lu.%03lu MHz  Step: %lu kHz",
                        (unsigned long)(APP_SIGDET_SCAN_START_HZ / 1000000UL),
                        (unsigned long)((APP_SIGDET_SCAN_START_HZ % 1000000UL) / 1000UL),
                        (unsigned long)(APP_SIGDET_SCAN_STOP_HZ / 1000000UL),
                        (unsigned long)((APP_SIGDET_SCAN_STOP_HZ % 1000000UL) / 1000UL),
                        (unsigned long)(APP_SIGDET_SCAN_STEP_HZ / 1000UL));
  lv_obj_set_style_text_color(g_ui.range_line, lv_color_hex(0x8AA6D1), 0);
  lv_obj_align(g_ui.range_line, LV_ALIGN_TOP_LEFT, 16, 40);

  main_card = App_LvglUiCreateCard(screen, 12, 68, 500, 250, lv_color_hex(0x152238), lv_color_hex(0x365C91));
  (void)App_LvglUiCreateCardTitle(main_card, "Carrier Frequency");

  g_ui.freq_value = lv_label_create(main_card);
  lv_label_set_text(g_ui.freq_value, "--.--- MHz");
  lv_obj_set_style_text_color(g_ui.freq_value, lv_color_hex(0xF5FAFF), 0);
  lv_obj_set_style_text_font(g_ui.freq_value, &lv_font_montserrat_14, 0);
  lv_obj_align(g_ui.freq_value, LV_ALIGN_TOP_LEFT, 16, 56);

  g_ui.scan_status = lv_label_create(main_card);
  lv_label_set_text(g_ui.scan_status, "Status: INIT");
  lv_obj_set_style_text_color(g_ui.scan_status, lv_color_hex(0x9CD0FF), 0);
  lv_obj_align(g_ui.scan_status, LV_ALIGN_TOP_LEFT, 16, 150);

  g_ui.quality_line = lv_label_create(main_card);
  lv_label_set_text(g_ui.quality_line, "Metric: best=0 noise=0");
  lv_obj_set_style_text_color(g_ui.quality_line, lv_color_hex(0xBFD3EF), 0);
  lv_obj_align(g_ui.quality_line, LV_ALIGN_TOP_LEFT, 16, 182);

  side_top_card = App_LvglUiCreateCard(screen, 524, 68, 264, 118, lv_color_hex(0x132A23), lv_color_hex(0x1E7A62));
  (void)App_LvglUiCreateCardTitle(side_top_card, "Modulation Detect (Reserved)");
  g_ui.mod_value = lv_label_create(side_top_card);
  lv_label_set_text(g_ui.mod_value, "Mode: --");
  lv_obj_set_style_text_color(g_ui.mod_value, lv_color_hex(0xD8FFF4), 0);
  lv_obj_align(g_ui.mod_value, LV_ALIGN_TOP_LEFT, 12, 46);

  side_mid_card = App_LvglUiCreateCard(screen, 524, 200, 264, 118, lv_color_hex(0x2A1F12), lv_color_hex(0xB1791E));
  (void)App_LvglUiCreateCardTitle(side_mid_card, "Demod Output (Reserved)");
  g_ui.demod_value = lv_label_create(side_mid_card);
  lv_label_set_text(g_ui.demod_value, "Audio/Data: --");
  lv_obj_set_style_text_color(g_ui.demod_value, lv_color_hex(0xFFE8CC), 0);
  lv_obj_align(g_ui.demod_value, LV_ALIGN_TOP_LEFT, 12, 46);

  bottom_card = App_LvglUiCreateCard(screen, 12, 332, 776, 136, lv_color_hex(0x1E1728), lv_color_hex(0x6B4D9C));
  (void)App_LvglUiCreateCardTitle(bottom_card, "Spectrum/FFT Area (Reserved)");
  g_ui.spectrum_value = lv_label_create(bottom_card);
  lv_label_set_text(g_ui.spectrum_value, "Waiting for FFT trace...\n(Reserve this area for frequency-domain visualization)");
  lv_obj_set_style_text_color(g_ui.spectrum_value, lv_color_hex(0xDECFFF), 0);
  lv_obj_align(g_ui.spectrum_value, LV_ALIGN_TOP_LEFT, 12, 42);

  g_ui.last_refresh_tick = 0U;
}

void App_LvglUiRefresh(void)
{
  app_signal_detect_status_t st;
  uint32_t now_tick = osKernelGetTickCount();
  char freq_buf[32];

  if ((uint32_t)(now_tick - g_ui.last_refresh_tick) < UI_REFRESH_PERIOD_MS)
  {
    return;
  }
  g_ui.last_refresh_tick = now_tick;

  app_signal_detect_get_status(&st);

  if (st.scanning != 0U)
  {
    App_LvglUiFormatFreq(freq_buf, sizeof(freq_buf), st.current_lo_hz);
    lv_label_set_text_fmt(g_ui.scan_status,
                          "Status: SCANNING (%u/%u)",
                          (unsigned)(st.step_index + 1U),
                          (unsigned)st.step_count);
  }
  else if (st.carrier_present != 0U)
  {
    App_LvglUiFormatFreq(freq_buf, sizeof(freq_buf), st.estimated_carrier_hz);
    lv_label_set_text(g_ui.scan_status, "Status: CARRIER LOCKED");
  }
  else
  {
    snprintf(freq_buf, sizeof(freq_buf), "--.--- MHz");
    lv_label_set_text(g_ui.scan_status, "Status: NO CARRIER");
  }

  lv_label_set_text(g_ui.freq_value, freq_buf);
  lv_label_set_text_fmt(g_ui.quality_line,
                        "Metric: best=%lu noise=%lu",
                        (unsigned long)st.best_metric,
                        (unsigned long)st.noise_metric);
}

void App_LvglUiSetModulationText(const char *text)
{
  if ((g_ui.mod_value != NULL) && (text != NULL))
  {
    lv_label_set_text(g_ui.mod_value, text);
  }
}

void App_LvglUiSetDemodText(const char *text)
{
  if ((g_ui.demod_value != NULL) && (text != NULL))
  {
    lv_label_set_text(g_ui.demod_value, text);
  }
}

void App_LvglUiSetSpectrumText(const char *text)
{
  if ((g_ui.spectrum_value != NULL) && (text != NULL))
  {
    lv_label_set_text(g_ui.spectrum_value, text);
  }
}

static lv_obj_t *App_LvglUiCreateCard(lv_obj_t *parent,
                                      lv_coord_t x,
                                      lv_coord_t y,
                                      lv_coord_t w,
                                      lv_coord_t h,
                                      lv_color_t bg,
                                      lv_color_t border)
{
  lv_obj_t *card = lv_obj_create(parent);

  lv_obj_set_pos(card, x, y);
  lv_obj_set_size(card, w, h);
  lv_obj_set_style_bg_color(card, bg, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, border, 0);
  lv_obj_set_style_border_width(card, 2, 0);
  lv_obj_set_style_radius(card, 14, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 0, 0);

  return card;
}

static lv_obj_t *App_LvglUiCreateCardTitle(lv_obj_t *parent, const char *title)
{
  lv_obj_t *title_label = lv_label_create(parent);

  lv_label_set_text(title_label, title);
  lv_obj_set_style_text_color(title_label, lv_color_hex(0xEAF2FF), 0);
  lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 12, 10);

  return title_label;
}

static void App_LvglUiFormatFreq(char *out, uint32_t out_len, uint32_t hz)
{
  snprintf(out,
           out_len,
           "%lu.%03lu MHz",
           (unsigned long)(hz / 1000000UL),
           (unsigned long)((hz % 1000000UL) / 1000UL));
}
