#include "app_lvgl_ui.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "cmsis_os2.h"
#include "app_sweep.h"
#include "ModDetectTask.h"
#include "RtosTypes.h"
#include "lvgl.h"

#define UI_REFRESH_PERIOD_MS 200U

typedef enum
{
  APP_UI_PAGE_DETECT = 0,
  APP_UI_PAGE_DEMOD
} app_ui_page_t;

typedef struct
{
  lv_obj_t *detect_page;
  lv_obj_t *demod_page;

  lv_obj_t *freq_value;
  lv_obj_t *scan_status;
  lv_obj_t *quality_line;
  lv_obj_t *range_line;
  lv_obj_t *mod_value;
  lv_obj_t *demod_value;
  lv_obj_t *spectrum_value;
  lv_obj_t *enter_demod_btn;

  lv_obj_t *demod_title;
  lv_obj_t *demod_state;
  lv_obj_t *demod_info;
  lv_obj_t *demod_back_btn;
  lv_obj_t *demod_chart;
  lv_chart_series_t *demod_series;

  uint32_t last_refresh_tick;
  app_ui_page_t current_page;
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
static void App_LvglUiShowPage(app_ui_page_t page);

void App_LvglUiInit(void)
{
  lv_obj_t *screen = lv_screen_active();
  lv_obj_t *main_card;
  lv_obj_t *label;


  lv_obj_clean(screen);
  g_ui.detect_page = lv_obj_create(screen);
  lv_obj_set_size(g_ui.detect_page, 800, 480);
  lv_obj_set_pos(g_ui.detect_page, 0, 0);
  lv_obj_set_style_bg_opa(g_ui.detect_page, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_ui.detect_page, 0, 0);
  lv_obj_set_style_pad_all(g_ui.detect_page, 0, 0);
  lv_obj_set_scrollbar_mode(g_ui.detect_page, LV_SCROLLBAR_MODE_OFF);
  g_ui.demod_page = NULL;

  lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B1220), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  label = lv_label_create(g_ui.detect_page);
  lv_label_set_text(label, "Sweep Only Receiver");
  lv_obj_set_style_text_color(label, lv_color_hex(0xD8E6FF), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 16, 10);

  g_ui.range_line = lv_label_create(g_ui.detect_page);
  lv_label_set_text_fmt(g_ui.range_line,
                        "Scan: %lu.%03lu-%lu.%03lu MHz  Step:%luk",
                        (unsigned long)(APP_SWEEP_DEFAULT_START_HZ / 1000000UL),
                        (unsigned long)((APP_SWEEP_DEFAULT_START_HZ % 1000000UL) / 1000UL),
                        (unsigned long)(APP_SWEEP_DEFAULT_STOP_HZ / 1000000UL),
                        (unsigned long)((APP_SWEEP_DEFAULT_STOP_HZ % 1000000UL) / 1000UL),
                        (unsigned long)(APP_SWEEP_DEFAULT_STEP_HZ / 1000UL));
  lv_obj_set_style_text_color(g_ui.range_line, lv_color_hex(0x8AA6D1), 0);
  lv_obj_align(g_ui.range_line, LV_ALIGN_TOP_LEFT, 16, 40);

  main_card = App_LvglUiCreateCard(g_ui.detect_page, 12, 68, 500, 250, lv_color_hex(0x152238), lv_color_hex(0x365C91));
  (void)App_LvglUiCreateCardTitle(main_card, "Sweep Center Frequency");

  g_ui.freq_value = lv_label_create(main_card);
  lv_label_set_text(g_ui.freq_value, "--.--- MHz");
  lv_obj_set_style_text_color(g_ui.freq_value, lv_color_hex(0xF5FAFF), 0);
  lv_obj_set_style_text_font(g_ui.freq_value, &lv_font_montserrat_14, 0);
  lv_obj_align(g_ui.freq_value, LV_ALIGN_TOP_LEFT, 16, 56);

  g_ui.scan_status = lv_label_create(main_card);
  lv_label_set_text(g_ui.scan_status, "Status: WAIT ADC");
  lv_obj_set_style_text_color(g_ui.scan_status, lv_color_hex(0x9CD0FF), 0);
  lv_obj_align(g_ui.scan_status, LV_ALIGN_TOP_LEFT, 16, 150);

  g_ui.quality_line = lv_label_create(main_card);
  lv_label_set_text(g_ui.quality_line, "Blocks: 0  Drops: 0");
  lv_obj_set_style_text_color(g_ui.quality_line, lv_color_hex(0xBFD3EF), 0);
  lv_obj_align(g_ui.quality_line, LV_ALIGN_TOP_LEFT, 16, 182);

  App_LvglUiShowPage(APP_UI_PAGE_DETECT);
}

void App_LvglUiRefresh(void)
{
  moddetect_task_stats_t st;
  uint32_t now_tick = osKernelGetTickCount();
  char freq_buf[32];

  if ((uint32_t)(now_tick - g_ui.last_refresh_tick) < UI_REFRESH_PERIOD_MS)
  {
    return;
  }
  g_ui.last_refresh_tick = now_tick;

  moddetect_task_get_stats(&st);

  if (st.result_ready != 0U)
  {
    App_LvglUiFormatFreq(freq_buf, sizeof(freq_buf), st.center_hz);
    lv_label_set_text(g_ui.scan_status, "Status: CENTER FOUND");
  }
  else if (st.process_cnt != 0U)
  {
    snprintf(freq_buf, sizeof(freq_buf), "--.--- MHz");
    lv_label_set_text(g_ui.scan_status, "Status: SWEEPING");
  }
  else
  {
    snprintf(freq_buf, sizeof(freq_buf), "--.--- MHz");
    lv_label_set_text(g_ui.scan_status, "Status: WAIT ADC");
  }

  lv_label_set_text(g_ui.freq_value, freq_buf);
  lv_label_set_text_fmt(g_ui.quality_line,
                        "Blocks: %lu  Drops: %lu\nPublished: %lu  Seq: %lu",
                        (unsigned long)st.process_cnt,
                        (unsigned long)st.submit_drop_cnt,
                        (unsigned long)st.submit_ok_cnt,
                        (unsigned long)st.last_sequence);
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

static void App_LvglUiShowPage(app_ui_page_t page)
{
  g_ui.current_page = page;

  if (g_ui.detect_page != NULL)
  {
    if (page == APP_UI_PAGE_DETECT)
    {
      lv_obj_remove_flag(g_ui.detect_page, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
      lv_obj_add_flag(g_ui.detect_page, LV_OBJ_FLAG_HIDDEN);
    }
  }

}


