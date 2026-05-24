#include "app_lvgl_ui.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "cmsis_os2.h"
#include "Analyze.h"
#include "app_sweep.h"
#include "ModDetectTask.h"
#include "DemodTask.h"
#include "SI5351.h"
#include "app_dds_ctrl.h"
#include "RtosTypes.h"
#include "lvgl.h"

#define UI_REFRESH_PERIOD_MS 200U
#define UI_STATUS_TAG_WIDTH 112
#define UI_STATUS_TAG_HEIGHT 28
#define UI_STATUS_TAG_RADIUS 8
#define UI_STATUS_TAG_TEXT_Y_PAD 5
#define UI_HW_TAG_WIDTH 104
#define UI_HW_TAG_HEIGHT 24
#define UI_HW_TAG_GAP 8
#define UI_MODE_BUTTON_WIDTH 148
#define UI_MODE_BUTTON_HEIGHT 54

typedef enum
{
  APP_UI_PAGE_DETECT = 0,
  APP_UI_PAGE_DEMOD
} app_ui_page_t;

typedef enum
{
  APP_UI_STATUS_IDLE = 0,
  APP_UI_STATUS_CALIBRATING,
  APP_UI_STATUS_CAL_DONE,
  APP_UI_STATUS_SWEEP,
  APP_UI_STATUS_UNLOCKED,
  APP_UI_STATUS_ANALYZING,
  APP_UI_STATUS_DEMOD,
  APP_UI_STATUS_DONE
} app_ui_status_t;

typedef struct
{
  lv_obj_t *detect_page;
  lv_obj_t *demod_page;

  lv_obj_t *freq_value;
  lv_obj_t *scan_status;
  lv_obj_t *si5351_status;
  lv_obj_t *ad9959_status;
  lv_obj_t *quality_line;
  lv_obj_t *range_line;
  lv_obj_t *mod_value;
  lv_obj_t *demod_value;
  lv_obj_t *spectrum_value;
  lv_obj_t *calibrate_btn;
  lv_obj_t *task_btn;
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
static lv_obj_t *App_LvglUiCreateModeButton(lv_obj_t *parent,
                                            const char *text,
                                            lv_coord_t x,
                                            lv_coord_t y,
                                            moddetect_run_mode_t mode);
static void App_LvglUiFormatFreq(char *out, uint32_t out_len, uint32_t hz);
static const char *App_LvglUiAnalyzeModeText(analyze_mode_t mode);
static void App_LvglUiFormatAnalyzeParam(char *out, uint32_t out_len, const analyze_result_t *result);
static void App_LvglUiSetStatus(app_ui_status_t status);
static void App_LvglUiSetHwStatus(lv_obj_t *status_label, const char *text, lv_color_t bg);
static void App_LvglUiRefreshHwStatus(void);
static void App_LvglUiModeButtonEventCb(lv_event_t *event);
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

  g_ui.ad9959_status = lv_label_create(g_ui.detect_page);
  lv_obj_set_size(g_ui.ad9959_status, UI_HW_TAG_WIDTH, UI_HW_TAG_HEIGHT);
  lv_label_set_long_mode(g_ui.ad9959_status, LV_LABEL_LONG_MODE_CLIP);
  lv_obj_set_style_radius(g_ui.ad9959_status, 8, 0);
  lv_obj_set_style_bg_opa(g_ui.ad9959_status, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(g_ui.ad9959_status, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(g_ui.ad9959_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(g_ui.ad9959_status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_pad_top(g_ui.ad9959_status, 3, 0);
  App_LvglUiSetHwStatus(g_ui.ad9959_status, "AD9959 --", lv_color_hex(0x64748B));
  lv_obj_align(g_ui.ad9959_status, LV_ALIGN_TOP_RIGHT, -16, 10);

  g_ui.si5351_status = lv_label_create(g_ui.detect_page);
  lv_obj_set_size(g_ui.si5351_status, UI_HW_TAG_WIDTH, UI_HW_TAG_HEIGHT);
  lv_label_set_long_mode(g_ui.si5351_status, LV_LABEL_LONG_MODE_CLIP);
  lv_obj_set_style_radius(g_ui.si5351_status, 8, 0);
  lv_obj_set_style_bg_opa(g_ui.si5351_status, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(g_ui.si5351_status, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(g_ui.si5351_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(g_ui.si5351_status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_pad_top(g_ui.si5351_status, 3, 0);
  App_LvglUiSetHwStatus(g_ui.si5351_status, "SI5351 --", lv_color_hex(0x64748B));
  lv_obj_align(g_ui.si5351_status, LV_ALIGN_TOP_RIGHT, -(16 + UI_HW_TAG_WIDTH + UI_HW_TAG_GAP), 10);

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
  lv_obj_set_size(g_ui.scan_status, UI_STATUS_TAG_WIDTH, UI_STATUS_TAG_HEIGHT);
  lv_label_set_long_mode(g_ui.scan_status, LV_LABEL_LONG_MODE_CLIP);
  lv_obj_set_style_radius(g_ui.scan_status, UI_STATUS_TAG_RADIUS, 0);
  lv_obj_set_style_bg_opa(g_ui.scan_status, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(g_ui.scan_status, lv_color_hex(0x111827), 0);
  lv_obj_set_style_text_align(g_ui.scan_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_left(g_ui.scan_status, 0, 0);
  lv_obj_set_style_pad_right(g_ui.scan_status, 0, 0);
  lv_obj_set_style_pad_top(g_ui.scan_status, UI_STATUS_TAG_TEXT_Y_PAD, 0);
  lv_obj_set_style_pad_bottom(g_ui.scan_status, 0, 0);
#if LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
  lv_obj_set_style_text_font(g_ui.scan_status, &lv_font_source_han_sans_sc_14_cjk, 0);
#else
  lv_obj_set_style_text_font(g_ui.scan_status, &lv_font_montserrat_14, 0);
#endif
  App_LvglUiSetStatus(APP_UI_STATUS_IDLE);
  lv_obj_align(g_ui.scan_status, LV_ALIGN_TOP_LEFT, 16, 150);

  g_ui.calibrate_btn = App_LvglUiCreateModeButton(main_card, "Calibrate", 336, 42, MODDETECT_RUN_CALIBRATION);
  g_ui.task_btn = App_LvglUiCreateModeButton(main_card, "Start/Restart", 336, 110, MODDETECT_RUN_TASK);

  g_ui.mod_value = lv_label_create(main_card);
  lv_label_set_text(g_ui.mod_value, "Mode: --");
  lv_obj_set_style_text_color(g_ui.mod_value, lv_color_hex(0xF5FAFF), 0);
  lv_obj_set_style_text_font(g_ui.mod_value, &lv_font_montserrat_14, 0);
  lv_obj_align(g_ui.mod_value, LV_ALIGN_TOP_LEFT, 16, 94);

  g_ui.demod_value = lv_label_create(main_card);
  lv_label_set_text(g_ui.demod_value, "Param: --");
  lv_obj_set_style_text_color(g_ui.demod_value, lv_color_hex(0xBFD3EF), 0);
  lv_obj_set_style_text_font(g_ui.demod_value, &lv_font_montserrat_14, 0);
  lv_obj_align(g_ui.demod_value, LV_ALIGN_TOP_LEFT, 16, 120);

  g_ui.quality_line = lv_label_create(main_card);
  lv_label_set_text(g_ui.quality_line, "Blocks: 0  Drops: 0");
  lv_obj_set_style_text_color(g_ui.quality_line, lv_color_hex(0xBFD3EF), 0);
  lv_obj_align(g_ui.quality_line, LV_ALIGN_TOP_LEFT, 16, 182);

  App_LvglUiShowPage(APP_UI_PAGE_DETECT);
}

void App_LvglUiRefresh(void)
{
  moddetect_task_stats_t st;
  demod_task_stats_t demod_st;
  analyze_result_t analyze_result;
  uint32_t now_tick = osKernelGetTickCount();
  char freq_buf[32];
  char param_buf[64];

  if ((uint32_t)(now_tick - g_ui.last_refresh_tick) < UI_REFRESH_PERIOD_MS)
  {
    return;
  }
  g_ui.last_refresh_tick = now_tick;

  moddetect_task_get_stats(&st);
  demod_task_get_stats(&demod_st);
  analyze_get_result(&analyze_result);
  App_LvglUiRefreshHwStatus();

  if ((st.result_ready != 0U) && (st.center_hz != 0UL))
  {
    App_LvglUiFormatFreq(freq_buf, sizeof(freq_buf), st.center_hz);
  }
  else
  {
    snprintf(freq_buf, sizeof(freq_buf), "--.--- MHz");
  }

  if (demod_st.state == DEMOD_STATE_RUNNING)
  {
    App_LvglUiSetStatus(APP_UI_STATUS_DEMOD);
  }
  else if (st.run_mode == MODDETECT_RUN_IDLE)
  {
    App_LvglUiSetStatus(APP_UI_STATUS_IDLE);
  }
  else if (st.run_mode == MODDETECT_RUN_CALIBRATION)
  {
    App_LvglUiSetStatus((st.cal_done != 0U) ? APP_UI_STATUS_CAL_DONE : APP_UI_STATUS_CALIBRATING);
  }
  else if ((st.result_ready != 0U) && (st.center_hz == 0UL))
  {
    App_LvglUiSetStatus(APP_UI_STATUS_UNLOCKED);
  }
  else if ((analyze_is_done() != 0U) && (st.result_ready != 0U) && (st.center_hz != 0UL))
  {
    App_LvglUiSetStatus(APP_UI_STATUS_DONE);
  }
  else if (analyze_is_active() != 0U)
  {
    App_LvglUiSetStatus(APP_UI_STATUS_ANALYZING);
  }
  else
  {
    App_LvglUiSetStatus(APP_UI_STATUS_SWEEP);
  }

  lv_label_set_text(g_ui.freq_value, freq_buf);
  if ((analyze_result.done != 0U) && (st.result_ready != 0U) && (st.center_hz != 0UL))
  {
    lv_label_set_text_fmt(g_ui.mod_value, "Mode: %s", App_LvglUiAnalyzeModeText(analyze_result.mode));
    App_LvglUiFormatAnalyzeParam(param_buf, sizeof(param_buf), &analyze_result);
    lv_label_set_text(g_ui.demod_value, param_buf);
  }
  else
  {
    lv_label_set_text(g_ui.mod_value, "Mode: --");
    lv_label_set_text(g_ui.demod_value, "Param: --");
  }

  lv_label_set_text_fmt(g_ui.quality_line,
                        "Blocks:%lu  Drops:%lu  Seq:%lu\nCal:%s  Clip:%lu",
                        (unsigned long)st.process_cnt,
                        (unsigned long)st.submit_drop_cnt,
                        (unsigned long)st.last_sequence,
                        (st.cal_valid != 0U) ? "OK" : ((st.cal_done != 0U) ? "FAIL" : "--"),
                        (unsigned long)st.cal_clip_cnt);
}

static void App_LvglUiSetStatus(app_ui_status_t status)
{
  const char *text = "IDLE";
  lv_color_t bg = lv_color_hex(0x93C5FD);

  if (g_ui.scan_status == NULL)
  {
    return;
  }

  switch (status)
  {
    case APP_UI_STATUS_DONE:
      text = "DONE";
      bg = lv_color_hex(0x22C55E);
      break;

    case APP_UI_STATUS_DEMOD:
      text = "DEMOD OUT";
      bg = lv_color_hex(0x22C55E);
      break;

    case APP_UI_STATUS_ANALYZING:
      text = "ANALYZING";
      bg = lv_color_hex(0xFB923C);
      break;

    case APP_UI_STATUS_UNLOCKED:
      text = "UNLOCK";
      bg = lv_color_hex(0xF87171);
      break;

    case APP_UI_STATUS_CAL_DONE:
      text = "CAL DONE";
      bg = lv_color_hex(0x38BDF8);
      break;

    case APP_UI_STATUS_CALIBRATING:
      text = "CAL";
      bg = lv_color_hex(0xFACC15);
      break;

    case APP_UI_STATUS_SWEEP:
      text = "SCANNING";
      bg = lv_color_hex(0xFACC15);
      break;

    case APP_UI_STATUS_IDLE:
    default:
      break;
  }

  lv_label_set_text(g_ui.scan_status, text);
  lv_obj_set_style_bg_color(g_ui.scan_status, bg, 0);
}

static void App_LvglUiSetHwStatus(lv_obj_t *status_label, const char *text, lv_color_t bg)
{
  if ((status_label == NULL) || (text == NULL))
  {
    return;
  }

  lv_label_set_text(status_label, text);
  lv_obj_set_style_bg_color(status_label, bg, 0);
}

/* 刷新右上角硬件自检状态：SI5351 看时钟 ready，AD9959 看 DDS 初始化结果。 */
static void App_LvglUiRefreshHwStatus(void)
{
  const AppDdsStatus *dds_status = AppDDS_GetStatus();

  if (app_si5351_is_clock_ready())
  {
    App_LvglUiSetHwStatus(g_ui.si5351_status, "SI5351 OK", lv_color_hex(0x16A34A));
  }
  else
  {
    App_LvglUiSetHwStatus(g_ui.si5351_status, "SI5351 WAIT", lv_color_hex(0xCA8A04));
  }

  if ((dds_status != NULL) && (dds_status->hw_ready != 0U) && (dds_status->last_err == 0))
  {
    App_LvglUiSetHwStatus(g_ui.ad9959_status, "AD9959 CFG", lv_color_hex(0x2563EB));
  }
  else if ((dds_status != NULL) && (dds_status->last_err != 0))
  {
    App_LvglUiSetHwStatus(g_ui.ad9959_status, "AD9959 ERR", lv_color_hex(0xDC2626));
  }
  else
  {
    App_LvglUiSetHwStatus(g_ui.ad9959_status, "AD9959 WAIT", lv_color_hex(0xCA8A04));
  }
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

/* 创建检测页模式按钮，按钮只发请求，不直接执行扫频或校准。 */
static lv_obj_t *App_LvglUiCreateModeButton(lv_obj_t *parent,
                                            const char *text,
                                            lv_coord_t x,
                                            lv_coord_t y,
                                            moddetect_run_mode_t mode)
{
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_t *label;

  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, UI_MODE_BUTTON_WIDTH, UI_MODE_BUTTON_HEIGHT);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x2563EB), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_add_event_cb(btn, App_LvglUiModeButtonEventCb, LV_EVENT_CLICKED, (void *)(uintptr_t)mode);

  label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
#if LV_FONT_SOURCE_HAN_SANS_SC_14_CJK
  lv_obj_set_style_text_font(label, &lv_font_source_han_sans_sc_14_cjk, 0);
#endif
  lv_obj_center(label);

  return btn;
}

/* 处理模式按钮点击，将用户选择转交给 ModDetectTask。 */
static void App_LvglUiModeButtonEventCb(lv_event_t *event)
{
  if (lv_event_get_code(event) == LV_EVENT_CLICKED)
  {
    moddetect_task_request_mode((moddetect_run_mode_t)(uintptr_t)lv_event_get_user_data(event));
  }
}

static void App_LvglUiFormatFreq(char *out, uint32_t out_len, uint32_t hz)
{
  snprintf(out,
           out_len,
           "%lu.%03lu MHz",
           (unsigned long)(hz / 1000000UL),
           (unsigned long)((hz % 1000000UL) / 1000UL));
}

static const char *App_LvglUiAnalyzeModeText(analyze_mode_t mode)
{
  switch (mode)
  {
    case ANALYZE_MODE_CW:
      return "CW";
    case ANALYZE_MODE_AM:
      return "AM";
    case ANALYZE_MODE_ASK:
      return "ASK";
    case ANALYZE_MODE_FM:
      return "FM";
    case ANALYZE_MODE_FSK:
      return "FSK";
    case ANALYZE_MODE_PSK:
      return "PSK";
    case ANALYZE_MODE_MIXED:
      return "MIXED";
    default:
      return "UNKNOWN";
  }
}

static void App_LvglUiFormatKhz(char *out, uint32_t out_len, const char *prefix, uint32_t hz)
{
  snprintf(out,
           out_len,
           "%s%lu.%03lu kHz",
           prefix,
           (unsigned long)(hz / 1000UL),
           (unsigned long)(hz % 1000UL));
}

/* 屏幕只显示最终调制类型相关参数，不显示谱评分、投票数等调试中间量。 */
static void App_LvglUiFormatAnalyzeParam(char *out, uint32_t out_len, const analyze_result_t *result)
{
  if ((out == NULL) || (out_len == 0U) || (result == NULL))
  {
    return;
  }

  switch (result->mode)
  {
    case ANALYZE_MODE_AM:
      snprintf(out,
               out_len,
               "Param: fm=%lu.%03lu kHz  depth=%lu.%lu%%",
               (unsigned long)(result->mod_hz / 1000UL),
               (unsigned long)(result->mod_hz % 1000UL),
               (unsigned long)(result->depth_pm / 100U),
               (unsigned long)((result->depth_pm % 100U) / 10U));
      break;

    case ANALYZE_MODE_ASK:
      snprintf(out,
               out_len,
               "Param: rate~%lu.%03lu kHz  depth=%lu.%lu%%",
               (unsigned long)(result->mod_hz / 1000UL),
               (unsigned long)(result->mod_hz % 1000UL),
               (unsigned long)(result->depth_pm / 100U),
               (unsigned long)((result->depth_pm % 100U) / 10U));
      break;

    case ANALYZE_MODE_FM:
      App_LvglUiFormatKhz(out, out_len, "Param: fm=", result->mod_hz);
      break;

    case ANALYZE_MODE_FSK:
      App_LvglUiFormatKhz(out, out_len, "Param: df~", result->mod_hz);
      break;

    case ANALYZE_MODE_MIXED:
      App_LvglUiFormatKhz(out, out_len, "Param: main~", result->mod_hz);
      break;

    case ANALYZE_MODE_CW:
    case ANALYZE_MODE_PSK:
    case ANALYZE_MODE_UNKNOWN:
    default:
      snprintf(out, out_len, "Param: --");
      break;
  }
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


