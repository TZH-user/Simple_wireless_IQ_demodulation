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
#include "app_ocxo_cal.h"
#include "RtosTypes.h"
#include "lvgl.h"

#define UI_REFRESH_PERIOD_MS 200U
#define UI_STATUS_TAG_WIDTH 170
#define UI_STATUS_TAG_HEIGHT 40
#define UI_STATUS_TAG_RADIUS 8
#define UI_STATUS_TAG_TEXT_Y_PAD 11
#define UI_HW_TAG_WIDTH 104
#define UI_HW_TAG_HEIGHT 24
#define UI_CAL_TAG_WIDTH 118
#define UI_OCXO_TAG_WIDTH 118
#define UI_HW_TAG_GAP 8
#define UI_MODE_BUTTON_WIDTH 220
#define UI_MODE_BUTTON_HEIGHT 70
#define UI_DASHBOARD_X 12
#define UI_DASHBOARD_Y 68
#define UI_DASHBOARD_W 776
#define UI_DASHBOARD_H 358
#define UI_ACTION_X 520
#define UI_CAL_SAVE_OK_BLINK_MS 3000U
#define UI_CAL_SAVE_OK_BLINK_PERIOD_MS 400U

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
  APP_UI_STATUS_OCXO_CAL,
  APP_UI_STATUS_SWEEP,
  APP_UI_STATUS_UNLOCKED,
  APP_UI_STATUS_ANALYZING,
  APP_UI_STATUS_DEMOD,
  APP_UI_STATUS_DONE
} app_ui_status_t;

typedef enum
{
  APP_UI_OCXO_STEP = 0,
  APP_UI_OCXO_DEC,
  APP_UI_OCXO_INC,
  APP_UI_OCXO_SAVE
} app_ui_ocxo_action_t;

typedef struct
{
  lv_obj_t *detect_page;
  lv_obj_t *demod_page;

  lv_obj_t *freq_value;
  lv_obj_t *scan_status;
  lv_obj_t *cal_status;
  lv_obj_t *ocxo_status;
  lv_obj_t *adc_ref_status;
  lv_obj_t *si5351_status;
  lv_obj_t *ad9959_status;
  lv_obj_t *quality_line;
  lv_obj_t *range_line;
  lv_obj_t *mod_value;
  lv_obj_t *demod_value;
  lv_obj_t *spectrum_value;
  lv_obj_t *overall_status;
  lv_obj_t *ocxo_value;
  lv_obj_t *calibrate_btn;
  lv_obj_t *task_btn;
  lv_obj_t *ocxo_btn;
  lv_obj_t *ocxo_step_btn;
  lv_obj_t *ocxo_dec_btn;
  lv_obj_t *ocxo_inc_btn;
  lv_obj_t *ocxo_save_btn;
  lv_obj_t *enter_demod_btn;

  lv_obj_t *demod_title;
  lv_obj_t *demod_state;
  lv_obj_t *demod_info;
  lv_obj_t *demod_back_btn;
  lv_obj_t *demod_chart;
  lv_chart_series_t *demod_series;

  uint32_t last_refresh_tick;
  uint32_t cal_save_ok_tick;
  app_ui_page_t current_page;
  moddetect_cal_state_t last_cal_state;
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
static lv_obj_t *App_LvglUiCreateOcxoButton(lv_obj_t *parent,
                                            const char *text,
                                            lv_coord_t x,
                                            lv_coord_t y,
                                            lv_coord_t w,
                                            app_ui_ocxo_action_t action);
static void App_LvglUiFormatFreq(char *out, uint32_t out_len, uint32_t hz);
static const char *App_LvglUiAnalyzeModeText(analyze_mode_t mode);
static void App_LvglUiFormatAnalyzeParam(char *out, uint32_t out_len, const analyze_result_t *result);
static void App_LvglUiSetStatus(app_ui_status_t status);
static void App_LvglUiSetHwStatus(lv_obj_t *status_label, const char *text, lv_color_t bg);
static void App_LvglUiSetCalBorder(uint8_t active);
static void App_LvglUiRefreshHwStatus(const moddetect_task_stats_t *stats);
static void App_LvglUiRefreshOverallStatus(const moddetect_task_stats_t *stats, const demod_task_stats_t *demod_stats);
static void App_LvglUiModeButtonEventCb(lv_event_t *event);
static void App_LvglUiOcxoButtonEventCb(lv_event_t *event);
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

  g_ui.adc_ref_status = lv_label_create(g_ui.detect_page);
  lv_obj_set_size(g_ui.adc_ref_status, UI_HW_TAG_WIDTH, UI_HW_TAG_HEIGHT);
  lv_label_set_long_mode(g_ui.adc_ref_status, LV_LABEL_LONG_MODE_CLIP);
  lv_obj_set_style_radius(g_ui.adc_ref_status, 8, 0);
  lv_obj_set_style_bg_opa(g_ui.adc_ref_status, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(g_ui.adc_ref_status, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(g_ui.adc_ref_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(g_ui.adc_ref_status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_pad_top(g_ui.adc_ref_status, 3, 0);
  App_LvglUiSetHwStatus(g_ui.adc_ref_status, "ADC --", lv_color_hex(0x64748B));
  lv_obj_align(g_ui.adc_ref_status,
               LV_ALIGN_TOP_RIGHT,
               -(16 + (2 * UI_HW_TAG_WIDTH) + (2 * UI_HW_TAG_GAP)),
               10);

  g_ui.cal_status = lv_label_create(g_ui.detect_page);
  lv_obj_set_size(g_ui.cal_status, UI_CAL_TAG_WIDTH, UI_HW_TAG_HEIGHT);
  lv_label_set_long_mode(g_ui.cal_status, LV_LABEL_LONG_MODE_CLIP);
  lv_obj_set_style_radius(g_ui.cal_status, 8, 0);
  lv_obj_set_style_bg_opa(g_ui.cal_status, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(g_ui.cal_status, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(g_ui.cal_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(g_ui.cal_status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_pad_top(g_ui.cal_status, 3, 0);
  App_LvglUiSetHwStatus(g_ui.cal_status, "CAL --", lv_color_hex(0x64748B));
  lv_obj_align(g_ui.cal_status,
               LV_ALIGN_TOP_RIGHT,
               -(16 + (3 * UI_HW_TAG_WIDTH) + (3 * UI_HW_TAG_GAP)),
               10);

  g_ui.ocxo_status = lv_label_create(g_ui.detect_page);
  lv_obj_set_size(g_ui.ocxo_status, UI_OCXO_TAG_WIDTH, UI_HW_TAG_HEIGHT);
  lv_label_set_long_mode(g_ui.ocxo_status, LV_LABEL_LONG_MODE_CLIP);
  lv_obj_set_style_radius(g_ui.ocxo_status, 8, 0);
  lv_obj_set_style_bg_opa(g_ui.ocxo_status, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(g_ui.ocxo_status, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(g_ui.ocxo_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(g_ui.ocxo_status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_pad_top(g_ui.ocxo_status, 3, 0);
  App_LvglUiSetHwStatus(g_ui.ocxo_status, "OCXO --", lv_color_hex(0x64748B));
  lv_obj_align(g_ui.ocxo_status,
               LV_ALIGN_TOP_RIGHT,
               -(16 + (3 * UI_HW_TAG_WIDTH) + (4 * UI_HW_TAG_GAP) + UI_CAL_TAG_WIDTH),
               10);

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

  main_card = App_LvglUiCreateCard(g_ui.detect_page,
                                   UI_DASHBOARD_X,
                                   UI_DASHBOARD_Y,
                                   UI_DASHBOARD_W,
                                   UI_DASHBOARD_H,
                                   lv_color_hex(0x152238),
                                   lv_color_hex(0x365C91));
  (void)App_LvglUiCreateCardTitle(main_card, "Signal Result");

  g_ui.freq_value = lv_label_create(main_card);
  lv_label_set_text(g_ui.freq_value, "--.--- MHz");
  lv_obj_set_style_text_color(g_ui.freq_value, lv_color_hex(0xF5FAFF), 0);
  lv_obj_set_style_text_font(g_ui.freq_value, &lv_font_montserrat_14, 0);
  lv_obj_align(g_ui.freq_value, LV_ALIGN_TOP_LEFT, 20, 62);

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
  lv_obj_align(g_ui.scan_status, LV_ALIGN_TOP_LEFT, 350, 228);

  label = lv_label_create(main_card);
  lv_label_set_text(label, "Actions");
  lv_obj_set_style_text_color(label, lv_color_hex(0xEAF2FF), 0);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, UI_ACTION_X, 34);

  g_ui.calibrate_btn = App_LvglUiCreateModeButton(main_card, "CALIBRATE", UI_ACTION_X, 70, MODDETECT_RUN_CALIBRATION);
  g_ui.task_btn = App_LvglUiCreateModeButton(main_card, "START TASK", UI_ACTION_X, 158, MODDETECT_RUN_TASK);
  g_ui.ocxo_btn = App_LvglUiCreateModeButton(main_card, "OCXO CAL", UI_ACTION_X, 246, MODDETECT_RUN_OCXO_CAL);

  label = lv_label_create(main_card);
  lv_label_set_text(label, "Readiness");
  lv_obj_set_style_text_color(label, lv_color_hex(0xEAF2FF), 0);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 20, 268);

  g_ui.overall_status = lv_label_create(main_card);
  lv_obj_set_size(g_ui.overall_status, 180, 40);
  lv_label_set_long_mode(g_ui.overall_status, LV_LABEL_LONG_MODE_CLIP);
  lv_obj_set_style_radius(g_ui.overall_status, 8, 0);
  lv_obj_set_style_bg_opa(g_ui.overall_status, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(g_ui.overall_status, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_align(g_ui.overall_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(g_ui.overall_status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_pad_top(g_ui.overall_status, 9, 0);
  App_LvglUiSetHwStatus(g_ui.overall_status, "NEED CAL", lv_color_hex(0xCA8A04));
  lv_obj_align(g_ui.overall_status, LV_ALIGN_TOP_LEFT, 20, 300);

  g_ui.mod_value = lv_label_create(main_card);
  lv_label_set_text(g_ui.mod_value, "Mode: --");
  lv_obj_set_style_text_color(g_ui.mod_value, lv_color_hex(0xF5FAFF), 0);
  lv_obj_set_style_text_font(g_ui.mod_value, &lv_font_montserrat_14, 0);
  lv_obj_align(g_ui.mod_value, LV_ALIGN_TOP_LEFT, 20, 126);

  g_ui.demod_value = lv_label_create(main_card);
  lv_label_set_text(g_ui.demod_value, "Param: --");
  lv_obj_set_style_text_color(g_ui.demod_value, lv_color_hex(0xBFD3EF), 0);
  lv_obj_set_style_text_font(g_ui.demod_value, &lv_font_montserrat_14, 0);
  lv_obj_align(g_ui.demod_value, LV_ALIGN_TOP_LEFT, 20, 164);

  g_ui.ocxo_value = lv_label_create(main_card);
  lv_label_set_text(g_ui.ocxo_value, "OCXO: 1400 mV  Step: 10 mV");
  lv_obj_set_style_text_color(g_ui.ocxo_value, lv_color_hex(0xBFD3EF), 0);
  lv_obj_set_style_text_font(g_ui.ocxo_value, &lv_font_montserrat_14, 0);
  lv_obj_align(g_ui.ocxo_value, LV_ALIGN_TOP_LEFT, 20, 202);

  g_ui.ocxo_step_btn = App_LvglUiCreateOcxoButton(main_card, "STEP", 20, 228, 88, APP_UI_OCXO_STEP);
  g_ui.ocxo_dec_btn = App_LvglUiCreateOcxoButton(main_card, "-", 118, 228, 56, APP_UI_OCXO_DEC);
  g_ui.ocxo_inc_btn = App_LvglUiCreateOcxoButton(main_card, "+", 184, 228, 56, APP_UI_OCXO_INC);
  g_ui.ocxo_save_btn = App_LvglUiCreateOcxoButton(main_card, "SAVE", 250, 228, 88, APP_UI_OCXO_SAVE);

  g_ui.quality_line = lv_label_create(g_ui.detect_page);
  lv_label_set_text(g_ui.quality_line, "Blocks: 0  Drops: 0");
  lv_obj_set_style_text_color(g_ui.quality_line, lv_color_hex(0xBFD3EF), 0);
  lv_obj_align(g_ui.quality_line, LV_ALIGN_BOTTOM_LEFT, 16, -12);

  App_LvglUiShowPage(APP_UI_PAGE_DETECT);
}

void App_LvglUiRefresh(void)
{
  moddetect_task_stats_t st;
  demod_task_stats_t demod_st;
  analyze_result_t analyze_result;
  app_ocxo_cal_status_t ocxo_status;
  uint32_t now_tick = osKernelGetTickCount();
  char freq_buf[32];
  char param_buf[96];

  if ((uint32_t)(now_tick - g_ui.last_refresh_tick) < UI_REFRESH_PERIOD_MS)
  {
    return;
  }
  g_ui.last_refresh_tick = now_tick;

  moddetect_task_get_stats(&st);
  demod_task_get_stats(&demod_st);
  analyze_get_result(&analyze_result);
  app_ocxo_cal_get_status(&ocxo_status);
  App_LvglUiRefreshHwStatus(&st);
  App_LvglUiRefreshOverallStatus(&st, &demod_st);

  if (g_ui.ocxo_value != NULL)
  {
    lv_label_set_text_fmt(g_ui.ocxo_value,
                          "OCXO: %lu mV  Step: %lu mV",
                          (unsigned long)ocxo_status.dac_mv,
                          (unsigned long)ocxo_status.step_mv);
  }

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
  else if (st.run_mode == MODDETECT_RUN_OCXO_CAL)
  {
    App_LvglUiSetStatus(APP_UI_STATUS_OCXO_CAL);
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
                        "Blocks:%lu  Drops:%lu  Seq:%lu",
                        (unsigned long)st.process_cnt,
                        (unsigned long)st.submit_drop_cnt,
                        (unsigned long)st.last_sequence);
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

    case APP_UI_STATUS_OCXO_CAL:
      text = "OCXO CAL";
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

static void App_LvglUiSetCalBorder(uint8_t active)
{
  if (g_ui.cal_status == NULL)
  {
    return;
  }

  lv_obj_set_style_border_width(g_ui.cal_status, active ? 3 : 0, 0);
  lv_obj_set_style_border_color(g_ui.cal_status, lv_color_hex(0xFFFFFF), 0);
}

/* 刷新右上角状态：硬件状态来自驱动状态，校准状态来自 ModDetectTask 的历史结果。 */
static void App_LvglUiRefreshHwStatus(const moddetect_task_stats_t *stats)
{
  const AppDdsStatus *dds_status = AppDDS_GetStatus();
  app_ocxo_cal_status_t ocxo_status;
  uint32_t now_tick = osKernelGetTickCount();
  uint8_t cal_border_on = 0U;

  app_ocxo_cal_get_status(&ocxo_status);

  if (stats != NULL)
  {
    if (stats->cal_state != g_ui.last_cal_state)
    {
      g_ui.last_cal_state = stats->cal_state;
      if (stats->cal_state == MODDETECT_CAL_SAVE_OK)
      {
        g_ui.cal_save_ok_tick = now_tick;
      }
    }

    if (stats->cal_state == MODDETECT_CAL_RUNNING)
    {
      App_LvglUiSetHwStatus(g_ui.cal_status, "CAL RUN", lv_color_hex(0xCA8A04));
    }
    else if (stats->cal_state == MODDETECT_CAL_SAVING)
    {
      App_LvglUiSetHwStatus(g_ui.cal_status, "CAL SAVE", lv_color_hex(0x2563EB));
    }
    else if (stats->cal_state == MODDETECT_CAL_SAVE_OK)
    {
      if ((uint32_t)(now_tick - g_ui.cal_save_ok_tick) < UI_CAL_SAVE_OK_BLINK_MS)
      {
        uint32_t phase = ((uint32_t)(now_tick - g_ui.cal_save_ok_tick) / UI_CAL_SAVE_OK_BLINK_PERIOD_MS) & 1UL;
        App_LvglUiSetHwStatus(g_ui.cal_status, "CAL SAVE OK", lv_color_hex(0x16A34A));
        cal_border_on = (phase == 0UL) ? 1U : 0U;
      }
      else
      {
        App_LvglUiSetHwStatus(g_ui.cal_status, "CAL RAM", lv_color_hex(0x16A34A));
      }
    }
    else if (stats->cal_state == MODDETECT_CAL_HISTORY)
    {
      App_LvglUiSetHwStatus(g_ui.cal_status, "CAL OLD", lv_color_hex(0x16A34A));
    }
    else if (stats->cal_state == MODDETECT_CAL_CURRENT)
    {
      App_LvglUiSetHwStatus(g_ui.cal_status, "CAL RAM", lv_color_hex(0x16A34A));
    }
    else if (stats->cal_done != 0U)
    {
      App_LvglUiSetHwStatus(g_ui.cal_status, "CAL FAIL", lv_color_hex(0xDC2626));
    }
    else
    {
      App_LvglUiSetHwStatus(g_ui.cal_status, "CAL --", lv_color_hex(0x64748B));
    }
  }
  App_LvglUiSetCalBorder(cal_border_on);

  if (ocxo_status.state == APP_OCXO_CAL_RUNNING)
  {
    App_LvglUiSetHwStatus(g_ui.ocxo_status, "OCXO RUN", lv_color_hex(0xCA8A04));
  }
  else if (ocxo_status.state == APP_OCXO_CAL_SAVING)
  {
    App_LvglUiSetHwStatus(g_ui.ocxo_status, "OCXO SAVE", lv_color_hex(0x2563EB));
  }
  else if (ocxo_status.state == APP_OCXO_CAL_SAVE_OK)
  {
    App_LvglUiSetHwStatus(g_ui.ocxo_status, "OCXO OK", lv_color_hex(0x16A34A));
  }
  else if (ocxo_status.state == APP_OCXO_CAL_HISTORY)
  {
    App_LvglUiSetHwStatus(g_ui.ocxo_status, "OCXO OLD", lv_color_hex(0x16A34A));
  }
  else if (ocxo_status.state == APP_OCXO_CAL_ERROR)
  {
    App_LvglUiSetHwStatus(g_ui.ocxo_status, "OCXO ERR", lv_color_hex(0xDC2626));
  }
  else
  {
    App_LvglUiSetHwStatus(g_ui.ocxo_status, "OCXO --", lv_color_hex(0x64748B));
  }

  if ((stats != NULL) && (stats->adc_ref_ok != 0U))
  {
    App_LvglUiSetHwStatus(g_ui.adc_ref_status, "ADC OK", lv_color_hex(0x16A34A));
  }
  else
  {
    App_LvglUiSetHwStatus(g_ui.adc_ref_status, "ADC WAIT", lv_color_hex(0xCA8A04));
  }

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

/* 汇总装置是否可以开始任务：硬件、校准、当前运行状态统一压缩成一个大状态。 */
static void App_LvglUiRefreshOverallStatus(const moddetect_task_stats_t *stats, const demod_task_stats_t *demod_stats)
{
  const AppDdsStatus *dds_status = AppDDS_GetStatus();
  uint8_t si_ready = app_si5351_is_clock_ready() ? 1U : 0U;
  uint8_t dds_cfg = ((dds_status != NULL) && (dds_status->hw_ready != 0U) && (dds_status->last_err == 0)) ? 1U : 0U;
  uint8_t adc_ok = ((stats != NULL) && (stats->adc_ref_ok != 0U)) ? 1U : 0U;

  if (g_ui.overall_status == NULL)
  {
    return;
  }

  if ((demod_stats != NULL) && (demod_stats->state == DEMOD_STATE_RUNNING))
  {
    App_LvglUiSetHwStatus(g_ui.overall_status, "DEMOD OUT", lv_color_hex(0x16A34A));
  }
  else if ((stats != NULL) && (stats->run_mode == MODDETECT_RUN_CALIBRATION) && (stats->cal_done == 0U))
  {
    App_LvglUiSetHwStatus(g_ui.overall_status, "CAL RUN", lv_color_hex(0xCA8A04));
  }
  else if ((stats != NULL) && (stats->run_mode == MODDETECT_RUN_OCXO_CAL))
  {
    App_LvglUiSetHwStatus(g_ui.overall_status, "OCXO CAL", lv_color_hex(0xCA8A04));
  }
  else if (((stats != NULL) && (stats->run_mode == MODDETECT_RUN_TASK) && (stats->result_ready == 0U)) ||
           (analyze_is_active() != 0U))
  {
    App_LvglUiSetHwStatus(g_ui.overall_status, "RUNNING", lv_color_hex(0xCA8A04));
  }
  else if (si_ready == 0U)
  {
    App_LvglUiSetHwStatus(g_ui.overall_status, "HW WAIT", lv_color_hex(0xCA8A04));
  }
  else if (adc_ok == 0U)
  {
    App_LvglUiSetHwStatus(g_ui.overall_status, "ADC WAIT", lv_color_hex(0xCA8A04));
  }
  else if ((dds_status != NULL) && (dds_status->last_err != 0))
  {
    App_LvglUiSetHwStatus(g_ui.overall_status, "DDS ERR", lv_color_hex(0xDC2626));
  }
  else if (dds_cfg == 0U)
  {
    App_LvglUiSetHwStatus(g_ui.overall_status, "DDS WAIT", lv_color_hex(0xCA8A04));
  }
  else if ((stats != NULL) && (stats->cal_valid != 0U))
  {
    App_LvglUiSetHwStatus(g_ui.overall_status, "READY", lv_color_hex(0x16A34A));
  }
  else
  {
    App_LvglUiSetHwStatus(g_ui.overall_status, "NEED CAL", lv_color_hex(0xCA8A04));
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

/* 创建 OCXO 电压校准按钮；按钮只在 OCXO 校准模式下执行调节和保存。 */
static lv_obj_t *App_LvglUiCreateOcxoButton(lv_obj_t *parent,
                                            const char *text,
                                            lv_coord_t x,
                                            lv_coord_t y,
                                            lv_coord_t w,
                                            app_ui_ocxo_action_t action)
{
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_t *label;

  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, w, 34);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x334155), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_add_event_cb(btn, App_LvglUiOcxoButtonEventCb, LV_EVENT_CLICKED, (void *)(uintptr_t)action);

  label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
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

/* 处理 OCXO 电压调节按钮。 */
static void App_LvglUiOcxoButtonEventCb(lv_event_t *event)
{
  app_ui_ocxo_action_t action;
  app_ocxo_cal_status_t status;

  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
  {
    return;
  }

  if (moddetect_task_get_mode() != MODDETECT_RUN_OCXO_CAL)
  {
    return;
  }

  action = (app_ui_ocxo_action_t)(uintptr_t)lv_event_get_user_data(event);
  app_ocxo_cal_get_status(&status);
  switch (action)
  {
    case APP_UI_OCXO_STEP:
      app_ocxo_cal_cycle_step();
      break;

    case APP_UI_OCXO_DEC:
      app_ocxo_cal_adjust(-(int32_t)status.step_mv);
      break;

    case APP_UI_OCXO_INC:
      app_ocxo_cal_adjust((int32_t)status.step_mv);
      break;

    case APP_UI_OCXO_SAVE:
      (void)app_ocxo_cal_save_to_flash();
      break;

    default:
      break;
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
    {
      uint32_t depth_pm = ((result->param_valid_mask & ANALYZE_PARAM_AM_DEPTH_VALID) != 0U) ?
                          result->am_depth_pm : result->depth_pm;
      snprintf(out,
               out_len,
               "Param: fm=%lu.%03lu kHz  depth=%lu.%lu%%",
               (unsigned long)(result->mod_hz / 1000UL),
               (unsigned long)(result->mod_hz % 1000UL),
               (unsigned long)(depth_pm / 10U),
               (unsigned long)(depth_pm % 10U));
      break;
    }

    case ANALYZE_MODE_ASK:
    {
      uint32_t depth_pm = ((result->param_valid_mask & ANALYZE_PARAM_ASK_DEPTH_VALID) != 0U) ?
                          result->ask_depth_pm : result->depth_pm;
      uint32_t rate_hz = ((result->param_valid_mask & ANALYZE_PARAM_SYMBOL_RATE_VALID) != 0U) ?
                         result->symbol_rate_hz : result->mod_hz;
      snprintf(out,
               out_len,
               "Param: rate~%lu.%03lu kHz  depth=%lu.%lu%%",
               (unsigned long)(rate_hz / 1000UL),
               (unsigned long)(rate_hz % 1000UL),
               (unsigned long)(depth_pm / 10U),
               (unsigned long)(depth_pm % 10U));
      break;
    }

    case ANALYZE_MODE_FM:
      if ((result->param_valid_mask & ANALYZE_PARAM_FM_DEVIATION_VALID) != 0U)
      {
        snprintf(out,
                 out_len,
                 "Param: fm=%lu.%03lu kHz  dev=%lu.%03lu kHz",
                 (unsigned long)(result->mod_hz / 1000UL),
                 (unsigned long)(result->mod_hz % 1000UL),
                 (unsigned long)(result->fm_deviation_hz / 1000UL),
                 (unsigned long)(result->fm_deviation_hz % 1000UL));
      }
      else
      {
        App_LvglUiFormatKhz(out, out_len, "Param: fm=", result->mod_hz);
      }
      break;

    case ANALYZE_MODE_FSK:
    {
      uint32_t sep_hz = ((result->param_valid_mask & ANALYZE_PARAM_FSK_SEPARATION_VALID) != 0U) ?
                        result->fsk_separation_hz : result->mod_hz;
      if ((result->param_valid_mask & ANALYZE_PARAM_SYMBOL_RATE_VALID) != 0U)
      {
        snprintf(out,
                 out_len,
                 "Param: rate~%lu.%03lu kHz  df~%lu.%03lu kHz",
                 (unsigned long)(result->symbol_rate_hz / 1000UL),
                 (unsigned long)(result->symbol_rate_hz % 1000UL),
                 (unsigned long)(sep_hz / 1000UL),
                 (unsigned long)(sep_hz % 1000UL));
      }
      else
      {
        App_LvglUiFormatKhz(out, out_len, "Param: df~", sep_hz);
      }
      break;
    }

    case ANALYZE_MODE_PSK:
      if ((result->param_valid_mask & ANALYZE_PARAM_SYMBOL_RATE_VALID) != 0U)
      {
        App_LvglUiFormatKhz(out, out_len, "Param: rate~", result->symbol_rate_hz);
      }
      else
      {
        snprintf(out, out_len, "Param: rate --");
      }
      break;

    case ANALYZE_MODE_MIXED:
      App_LvglUiFormatKhz(out, out_len, "Param: main~", result->mod_hz);
      break;

    case ANALYZE_MODE_CW:
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


