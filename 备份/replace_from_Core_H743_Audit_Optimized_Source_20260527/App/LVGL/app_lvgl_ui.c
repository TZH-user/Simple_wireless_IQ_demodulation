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
#include "app_buzzer.h"
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
#define UI_ACTION_X 450
#define UI_SIDE_MENU_X 688
#define UI_SIDE_MENU_W 68
#define UI_SIDE_MENU_H 46
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
  APP_UI_STATUS_DDS_CAL,
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
  APP_UI_OCXO_SAVE,
  APP_UI_AUTO_TASK_TOGGLE,
  APP_UI_BOOT_ANIM_TOGGLE,
  APP_UI_RUNTIME_MONITOR_TOGGLE,
  APP_UI_ASK_DEMOD_TOGGLE,
  APP_UI_FSK_DEMOD_TOGGLE,
  APP_UI_ASK_SQUARE_DC_CYCLE,
  APP_UI_ASK_SQUARE_THRESHOLD_CYCLE,
  APP_UI_BEEP_UI_TOGGLE,
  APP_UI_BEEP_SWEEP_LOCK_TOGGLE,
  APP_UI_BEEP_ANALYZE_DONE_TOGGLE,
  APP_UI_BEEP_DEMOD_START_TOGGLE,
  APP_UI_MIXED_RETRY_CYCLE
} app_ui_ocxo_action_t;

typedef enum
{
  APP_UI_MENU_CAL = 0,
  APP_UI_MENU_TASK,
  APP_UI_MENU_SETTINGS
} app_ui_menu_t;

typedef enum
{
  APP_UI_CMD_MODE = 0,
  APP_UI_CMD_OCXO_ACTION,
  APP_UI_CMD_MENU,
  APP_UI_CMD_SAVE_CONFIRMED
} app_ui_command_t;

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
  lv_obj_t *dds_cal_btn;
  lv_obj_t *ocxo_step_btn;
  lv_obj_t *ocxo_dec_btn;
  lv_obj_t *ocxo_inc_btn;
  lv_obj_t *ocxo_save_btn;
  lv_obj_t *auto_task_btn;
  lv_obj_t *auto_task_label;
  lv_obj_t *boot_anim_btn;
  lv_obj_t *boot_anim_label;
  lv_obj_t *runtime_monitor_btn;
  lv_obj_t *runtime_monitor_label;
  lv_obj_t *mixed_retry_btn;
  lv_obj_t *mixed_retry_label;
  lv_obj_t *demod_setting_title;
  lv_obj_t *ask_demod_btn;
  lv_obj_t *ask_demod_label;
  lv_obj_t *fsk_demod_btn;
  lv_obj_t *fsk_demod_label;
  lv_obj_t *ask_square_dc_btn;
  lv_obj_t *ask_square_dc_label;
  lv_obj_t *ask_square_threshold_btn;
  lv_obj_t *ask_square_threshold_label;
  lv_obj_t *beep_setting_title;
  lv_obj_t *beep_ui_btn;
  lv_obj_t *beep_ui_label;
  lv_obj_t *beep_lock_btn;
  lv_obj_t *beep_lock_label;
  lv_obj_t *beep_analyze_btn;
  lv_obj_t *beep_analyze_label;
  lv_obj_t *beep_demod_btn;
  lv_obj_t *beep_demod_label;
  lv_obj_t *menu_cal_btn;
  lv_obj_t *menu_task_btn;
  lv_obj_t *menu_settings_btn;
  lv_obj_t *enter_demod_btn;

  lv_obj_t *demod_title;
  lv_obj_t *demod_state;
  lv_obj_t *demod_info;
  lv_obj_t *demod_back_btn;
  lv_obj_t *demod_chart;
  lv_chart_series_t *demod_series;
  lv_obj_t *save_confirm_overlay;

  uint32_t last_refresh_tick;
  uint32_t cal_save_ok_tick;
  app_ui_page_t current_page;
  app_ui_menu_t active_menu;
  moddetect_cal_state_t last_cal_state;
  moddetect_run_mode_t pending_save_mode;
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
static lv_obj_t *App_LvglUiCreateMenuButton(lv_obj_t *parent,
                                            const char *text,
                                            lv_coord_t x,
                                            lv_coord_t y,
                                            app_ui_menu_t menu);
static void App_LvglUiFormatFreq(char *out, uint32_t out_len, uint32_t hz);
static const char *App_LvglUiAnalyzeModeText(analyze_mode_t mode);
static void App_LvglUiFormatAnalyzeParam(char *out, uint32_t out_len, const analyze_result_t *result);
static void App_LvglUiSetStatus(app_ui_status_t status);
static void App_LvglUiSetHwStatus(lv_obj_t *status_label, const char *text, lv_color_t bg);
static void App_LvglUiSetCalBorder(uint8_t active);
static void App_LvglUiSetOcxoControlsVisible(uint8_t visible);
static app_ui_status_t App_LvglUiResolveRunStatus(const moddetect_task_stats_t *stats, const demod_task_stats_t *demod_stats);
static uint8_t App_LvglUiShouldShowOcxoControls(const moddetect_task_stats_t *stats);
static void App_LvglUiRefreshAutoTaskButton(const app_ocxo_cal_status_t *ocxo_status);
static void App_LvglUiRefreshBootAnimButton(const app_ocxo_cal_status_t *ocxo_status);
static void App_LvglUiRefreshRuntimeMonitorButton(const app_ocxo_cal_status_t *ocxo_status);
static void App_LvglUiRefreshMixedRetryButton(const app_ocxo_cal_status_t *ocxo_status);
static void App_LvglUiRefreshDemodSettingButtons(const app_ocxo_cal_status_t *ocxo_status);
static void App_LvglUiRefreshBuzzerButtons(const app_ocxo_cal_status_t *ocxo_status);
static void App_LvglUiRefreshOcxoControls(const moddetect_task_stats_t *stats, const app_ocxo_cal_status_t *ocxo_status);
static void App_LvglUiRefreshMenuVisibility(const moddetect_task_stats_t *stats);
static void App_LvglUiRefreshMenuButtons(void);
static void App_LvglUiRefreshResult(const moddetect_task_stats_t *stats, const analyze_result_t *result);
static void App_LvglUiRefreshQuality(const moddetect_task_stats_t *stats);
static void App_LvglUiRefreshHwStatus(const moddetect_task_stats_t *stats);
static void App_LvglUiRefreshOverallStatus(const moddetect_task_stats_t *stats, const demod_task_stats_t *demod_stats);
static uint8_t App_LvglUiIsOcxoCalModeActive(void);
static uint8_t App_LvglUiIsDdsCalModeActive(void);
static void App_LvglUiLeaveOcxoCalIfActive(void);
static void App_LvglUiLeaveDdsCalIfActive(void);
static void App_LvglUiSwitchToTaskMenu(void);
static void App_LvglUiShowSaveConfirm(moddetect_run_mode_t mode);
static void App_LvglUiCloseSaveConfirm(void);
static void App_LvglUiExecuteConfirmedSave(moddetect_run_mode_t mode);
static void App_LvglUiDispatchCommand(app_ui_command_t command, uintptr_t value);
static void App_LvglUiModeButtonEventCb(lv_event_t *event);
static void App_LvglUiOcxoButtonEventCb(lv_event_t *event);
static void App_LvglUiMenuButtonEventCb(lv_event_t *event);
static void App_LvglUiSaveConfirmEventCb(lv_event_t *event);
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
  g_ui.active_menu = APP_UI_MENU_TASK;

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
  lv_label_set_text(label, "Menu");
  lv_obj_set_style_text_color(label, lv_color_hex(0xEAF2FF), 0);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, UI_SIDE_MENU_X, 34);

  g_ui.menu_cal_btn = App_LvglUiCreateMenuButton(main_card, "CAL", UI_SIDE_MENU_X, 62, APP_UI_MENU_CAL);
  g_ui.menu_task_btn = App_LvglUiCreateMenuButton(main_card, "TASK", UI_SIDE_MENU_X, 116, APP_UI_MENU_TASK);
  g_ui.menu_settings_btn = App_LvglUiCreateMenuButton(main_card, "SET", UI_SIDE_MENU_X, 170, APP_UI_MENU_SETTINGS);

  g_ui.calibrate_btn = App_LvglUiCreateModeButton(main_card, "FREQ CAL", UI_ACTION_X, 70, MODDETECT_RUN_CALIBRATION);
  g_ui.ocxo_btn = App_LvglUiCreateModeButton(main_card, "OCXO CAL", UI_ACTION_X, 158, MODDETECT_RUN_OCXO_CAL);
  g_ui.dds_cal_btn = App_LvglUiCreateModeButton(main_card, "DDS CAL", UI_ACTION_X, 246, MODDETECT_RUN_DDS_CAL);
  g_ui.task_btn = App_LvglUiCreateModeButton(main_card, "START TASK", UI_ACTION_X, 70, MODDETECT_RUN_TASK);

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

  g_ui.auto_task_btn = App_LvglUiCreateOcxoButton(main_card, "AUTO ON", UI_ACTION_X, 70, 124, APP_UI_AUTO_TASK_TOGGLE);
  g_ui.auto_task_label = lv_obj_get_child(g_ui.auto_task_btn, 0);
  g_ui.boot_anim_btn = App_LvglUiCreateOcxoButton(main_card, "ANIM ON", UI_ACTION_X + 120, 70, 110, APP_UI_BOOT_ANIM_TOGGLE);
  g_ui.boot_anim_label = lv_obj_get_child(g_ui.boot_anim_btn, 0);
  g_ui.runtime_monitor_btn = App_LvglUiCreateOcxoButton(main_card, "MON OFF", UI_ACTION_X, 116, 110, APP_UI_RUNTIME_MONITOR_TOGGLE);
  g_ui.runtime_monitor_label = lv_obj_get_child(g_ui.runtime_monitor_btn, 0);
  g_ui.mixed_retry_btn = App_LvglUiCreateOcxoButton(main_card, "MIX R3", UI_ACTION_X + 120, 116, 110, APP_UI_MIXED_RETRY_CYCLE);
  g_ui.mixed_retry_label = lv_obj_get_child(g_ui.mixed_retry_btn, 0);

  g_ui.demod_setting_title = lv_label_create(main_card);
  lv_label_set_text(g_ui.demod_setting_title, "DEMOD");
  lv_obj_set_style_text_color(g_ui.demod_setting_title, lv_color_hex(0xEAF2FF), 0);
  lv_obj_set_style_text_font(g_ui.demod_setting_title, &lv_font_montserrat_14, 0);
  lv_obj_align(g_ui.demod_setting_title, LV_ALIGN_TOP_LEFT, UI_ACTION_X, 158);
  g_ui.ask_demod_btn = App_LvglUiCreateOcxoButton(main_card, "ASK NORM", UI_ACTION_X, 180, 110, APP_UI_ASK_DEMOD_TOGGLE);
  g_ui.ask_demod_label = lv_obj_get_child(g_ui.ask_demod_btn, 0);
  g_ui.fsk_demod_btn = App_LvglUiCreateOcxoButton(main_card, "FSK NORM", UI_ACTION_X + 120, 180, 110, APP_UI_FSK_DEMOD_TOGGLE);
  g_ui.fsk_demod_label = lv_obj_get_child(g_ui.fsk_demod_btn, 0);
  g_ui.ask_square_dc_btn = App_LvglUiCreateOcxoButton(main_card, "ASK DC 6", UI_ACTION_X, 226, 110, APP_UI_ASK_SQUARE_DC_CYCLE);
  g_ui.ask_square_dc_label = lv_obj_get_child(g_ui.ask_square_dc_btn, 0);
  g_ui.ask_square_threshold_btn = App_LvglUiCreateOcxoButton(main_card, "ASK TH 40", UI_ACTION_X + 120, 226, 110, APP_UI_ASK_SQUARE_THRESHOLD_CYCLE);
  g_ui.ask_square_threshold_label = lv_obj_get_child(g_ui.ask_square_threshold_btn, 0);

  g_ui.beep_setting_title = lv_label_create(main_card);
  lv_label_set_text(g_ui.beep_setting_title, "BEEP");
  lv_obj_set_style_text_color(g_ui.beep_setting_title, lv_color_hex(0xEAF2FF), 0);
  lv_obj_set_style_text_font(g_ui.beep_setting_title, &lv_font_montserrat_14, 0);
  lv_obj_align(g_ui.beep_setting_title, LV_ALIGN_TOP_LEFT, UI_ACTION_X, 274);
  g_ui.beep_ui_btn = App_LvglUiCreateOcxoButton(main_card, "UI OFF", UI_ACTION_X, 296, 110, APP_UI_BEEP_UI_TOGGLE);
  g_ui.beep_ui_label = lv_obj_get_child(g_ui.beep_ui_btn, 0);
  g_ui.beep_lock_btn = App_LvglUiCreateOcxoButton(main_card, "LOCK OFF", UI_ACTION_X + 120, 296, 110, APP_UI_BEEP_SWEEP_LOCK_TOGGLE);
  g_ui.beep_lock_label = lv_obj_get_child(g_ui.beep_lock_btn, 0);
  g_ui.beep_analyze_btn = App_LvglUiCreateOcxoButton(main_card, "ANA OFF", UI_ACTION_X, 342, 110, APP_UI_BEEP_ANALYZE_DONE_TOGGLE);
  g_ui.beep_analyze_label = lv_obj_get_child(g_ui.beep_analyze_btn, 0);
  g_ui.beep_demod_btn = App_LvglUiCreateOcxoButton(main_card, "DEM OFF", UI_ACTION_X + 120, 342, 110, APP_UI_BEEP_DEMOD_START_TOGGLE);
  g_ui.beep_demod_label = lv_obj_get_child(g_ui.beep_demod_btn, 0);

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
  lv_label_set_text(g_ui.ocxo_value, "DDS: --.---MHz  OCXO: ----mV  Step: --mV");
  lv_obj_set_style_text_color(g_ui.ocxo_value, lv_color_hex(0xBFD3EF), 0);
  lv_obj_set_style_text_font(g_ui.ocxo_value, &lv_font_montserrat_14, 0);
  lv_obj_align(g_ui.ocxo_value, LV_ALIGN_TOP_LEFT, 20, 202);

  g_ui.ocxo_step_btn = App_LvglUiCreateOcxoButton(main_card, "STEP", 20, 228, 88, APP_UI_OCXO_STEP);
  g_ui.ocxo_dec_btn = App_LvglUiCreateOcxoButton(main_card, "-", 118, 228, 56, APP_UI_OCXO_DEC);
  g_ui.ocxo_inc_btn = App_LvglUiCreateOcxoButton(main_card, "+", 184, 228, 56, APP_UI_OCXO_INC);
  g_ui.ocxo_save_btn = App_LvglUiCreateOcxoButton(main_card, "SAVE", 250, 228, 88, APP_UI_OCXO_SAVE);
  App_LvglUiSetOcxoControlsVisible(0U);
  App_LvglUiRefreshMenuVisibility(NULL);

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
  App_LvglUiRefreshAutoTaskButton(&ocxo_status);
  App_LvglUiRefreshBootAnimButton(&ocxo_status);
  App_LvglUiRefreshRuntimeMonitorButton(&ocxo_status);
  App_LvglUiRefreshMixedRetryButton(&ocxo_status);
  App_LvglUiRefreshDemodSettingButtons(&ocxo_status);
  App_LvglUiRefreshBuzzerButtons(&ocxo_status);
  App_LvglUiRefreshOcxoControls(&st, &ocxo_status);
  App_LvglUiRefreshMenuVisibility(&st);
  App_LvglUiRefreshMenuButtons();
  App_LvglUiSetStatus(App_LvglUiResolveRunStatus(&st, &demod_st));
  App_LvglUiRefreshResult(&st, &analyze_result);
  App_LvglUiRefreshQuality(&st);
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
      text = "ANALYZE OK";
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

    case APP_UI_STATUS_DDS_CAL:
      text = "DDS CAL";
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

static void App_LvglUiSetOneHidden(lv_obj_t *obj, uint8_t hidden)
{
  if (obj == NULL)
  {
    return;
  }

  if (hidden != 0U)
  {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
  else
  {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

/* 非手动校准模式隐藏调节控件，避免误触改变 OCXO 电压或 DDS 频偏。 */
static void App_LvglUiSetOcxoControlsVisible(uint8_t visible)
{
  uint8_t hidden = (visible == 0U) ? 1U : 0U;

  App_LvglUiSetOneHidden(g_ui.ocxo_value, hidden);
  App_LvglUiSetOneHidden(g_ui.ocxo_step_btn, hidden);
  App_LvglUiSetOneHidden(g_ui.ocxo_dec_btn, hidden);
  App_LvglUiSetOneHidden(g_ui.ocxo_inc_btn, hidden);
  App_LvglUiSetOneHidden(g_ui.ocxo_save_btn, hidden);
}

/* 把任务状态压缩成主状态标签，避免刷新函数里散落多段优先级判断。 */
static app_ui_status_t App_LvglUiResolveRunStatus(const moddetect_task_stats_t *stats, const demod_task_stats_t *demod_stats)
{
  if ((demod_stats != NULL) && (demod_stats->state == DEMOD_STATE_RUNNING))
  {
    return APP_UI_STATUS_DEMOD;
  }

  if ((stats == NULL) || (stats->run_mode == MODDETECT_RUN_IDLE))
  {
    return APP_UI_STATUS_IDLE;
  }

  if (stats->run_mode == MODDETECT_RUN_CALIBRATION)
  {
    return (stats->cal_done != 0U) ? APP_UI_STATUS_CAL_DONE : APP_UI_STATUS_CALIBRATING;
  }

  if (stats->run_mode == MODDETECT_RUN_OCXO_CAL)
  {
    return APP_UI_STATUS_OCXO_CAL;
  }

  if (stats->run_mode == MODDETECT_RUN_DDS_CAL)
  {
    return APP_UI_STATUS_DDS_CAL;
  }

  if ((stats->result_ready != 0U) && (stats->center_hz == 0UL))
  {
    return APP_UI_STATUS_UNLOCKED;
  }

  if ((analyze_is_done() != 0U) && (stats->result_ready != 0U) && (stats->center_hz != 0UL))
  {
    return APP_UI_STATUS_DONE;
  }

  if (analyze_is_active() != 0U)
  {
    return APP_UI_STATUS_ANALYZING;
  }

  return APP_UI_STATUS_SWEEP;
}

static uint8_t App_LvglUiShouldShowOcxoControls(const moddetect_task_stats_t *stats)
{
  return ((stats != NULL) &&
          ((stats->run_mode == MODDETECT_RUN_OCXO_CAL) ||
           (stats->run_mode == MODDETECT_RUN_DDS_CAL))) ? 1U : 0U;
}

static void App_LvglUiRefreshAutoTaskButton(const app_ocxo_cal_status_t *ocxo_status)
{
  if ((g_ui.auto_task_btn == NULL) || (g_ui.auto_task_label == NULL) || (ocxo_status == NULL))
  {
    return;
  }

  if (ocxo_status->auto_task_enable != 0U)
  {
    lv_label_set_text(g_ui.auto_task_label, "AUTO ON");
    lv_obj_set_style_bg_color(g_ui.auto_task_btn, lv_color_hex(0x16A34A), 0);
  }
  else
  {
    lv_label_set_text(g_ui.auto_task_label, "AUTO OFF");
    lv_obj_set_style_bg_color(g_ui.auto_task_btn, lv_color_hex(0x64748B), 0);
  }
}

static void App_LvglUiRefreshBootAnimButton(const app_ocxo_cal_status_t *ocxo_status)
{
  if ((g_ui.boot_anim_btn == NULL) || (g_ui.boot_anim_label == NULL) || (ocxo_status == NULL))
  {
    return;
  }

  if (ocxo_status->boot_anim_enable != 0U)
  {
    lv_label_set_text(g_ui.boot_anim_label, "ANIM ON");
    lv_obj_set_style_bg_color(g_ui.boot_anim_btn, lv_color_hex(0x16A34A), 0);
  }
  else
  {
    lv_label_set_text(g_ui.boot_anim_label, "ANIM OFF");
    lv_obj_set_style_bg_color(g_ui.boot_anim_btn, lv_color_hex(0x64748B), 0);
  }
}

static void App_LvglUiRefreshRuntimeMonitorButton(const app_ocxo_cal_status_t *ocxo_status)
{
  if ((g_ui.runtime_monitor_btn == NULL) || (g_ui.runtime_monitor_label == NULL) || (ocxo_status == NULL))
  {
    return;
  }

  if (ocxo_status->runtime_monitor_enable != 0U)
  {
    lv_label_set_text(g_ui.runtime_monitor_label, "MON ON");
    lv_obj_set_style_bg_color(g_ui.runtime_monitor_btn, lv_color_hex(0x16A34A), 0);
  }
  else
  {
    lv_label_set_text(g_ui.runtime_monitor_label, "MON OFF");
    lv_obj_set_style_bg_color(g_ui.runtime_monitor_btn, lv_color_hex(0x64748B), 0);
  }
}

static void App_LvglUiRefreshMixedRetryButton(const app_ocxo_cal_status_t *ocxo_status)
{
  if ((g_ui.mixed_retry_btn == NULL) || (g_ui.mixed_retry_label == NULL) || (ocxo_status == NULL))
  {
    return;
  }

  if (ocxo_status->mixed_retry_count == APP_OCXO_CAL_MIXED_RETRY_INFINITE)
  {
    lv_label_set_text(g_ui.mixed_retry_label, "MIX INF");
    lv_obj_set_style_bg_color(g_ui.mixed_retry_btn, lv_color_hex(0x16A34A), 0);
  }
  else
  {
    lv_label_set_text_fmt(g_ui.mixed_retry_label, "MIX R%u", (unsigned int)ocxo_status->mixed_retry_count);
    lv_obj_set_style_bg_color(g_ui.mixed_retry_btn,
                              (ocxo_status->mixed_retry_count != 0U) ? lv_color_hex(0x16A34A) : lv_color_hex(0x64748B),
                              0);
  }
}

static void App_LvglUiSetToggleButton(lv_obj_t *btn,
                                      lv_obj_t *label,
                                      uint8_t enabled,
                                      const char *on_text,
                                      const char *off_text)
{
  if ((btn == NULL) || (label == NULL) || (on_text == NULL) || (off_text == NULL))
  {
    return;
  }

  lv_label_set_text(label, (enabled != 0U) ? on_text : off_text);
  lv_obj_set_style_bg_color(btn,
                            (enabled != 0U) ? lv_color_hex(0x16A34A) : lv_color_hex(0x64748B),
                            0);
}

static void App_LvglUiRefreshDemodSettingButtons(const app_ocxo_cal_status_t *ocxo_status)
{
  if (ocxo_status == NULL)
  {
    return;
  }

  App_LvglUiSetToggleButton(g_ui.ask_demod_btn,
                            g_ui.ask_demod_label,
                            ocxo_status->ask_analog_demod_enable,
                            "ASK ENH",
                            "ASK NORM");
  App_LvglUiSetToggleButton(g_ui.fsk_demod_btn,
                            g_ui.fsk_demod_label,
                            ocxo_status->fsk_analog_demod_enable,
                            "FSK ENH",
                            "FSK NORM");
  if ((g_ui.ask_square_dc_btn != NULL) && (g_ui.ask_square_dc_label != NULL))
  {
    lv_label_set_text_fmt(g_ui.ask_square_dc_label,
                          "ASK DC %u",
                          (unsigned int)ocxo_status->ask_square_dc_shift);
    lv_obj_set_style_bg_color(g_ui.ask_square_dc_btn, lv_color_hex(0x334155), 0);
  }
  if ((g_ui.ask_square_threshold_btn != NULL) && (g_ui.ask_square_threshold_label != NULL))
  {
    lv_label_set_text_fmt(g_ui.ask_square_threshold_label,
                          "ASK TH %u",
                          (unsigned int)ocxo_status->ask_square_threshold_code);
    lv_obj_set_style_bg_color(g_ui.ask_square_threshold_btn, lv_color_hex(0x334155), 0);
  }
}

static void App_LvglUiRefreshBuzzerButtons(const app_ocxo_cal_status_t *ocxo_status)
{
  if (ocxo_status == NULL)
  {
    return;
  }

  App_LvglUiSetToggleButton(g_ui.beep_ui_btn,
                            g_ui.beep_ui_label,
                            ocxo_status->beep_ui_enable,
                            "UI ON",
                            "UI OFF");
  App_LvglUiSetToggleButton(g_ui.beep_lock_btn,
                            g_ui.beep_lock_label,
                            ocxo_status->beep_sweep_lock_enable,
                            "LOCK ON",
                            "LOCK OFF");
  App_LvglUiSetToggleButton(g_ui.beep_analyze_btn,
                            g_ui.beep_analyze_label,
                            ocxo_status->beep_analyze_done_enable,
                            "ANA ON",
                            "ANA OFF");
  App_LvglUiSetToggleButton(g_ui.beep_demod_btn,
                            g_ui.beep_demod_label,
                            ocxo_status->beep_demod_start_enable,
                            "DEM ON",
                            "DEM OFF");
}

static void App_LvglUiRefreshOcxoControls(const moddetect_task_stats_t *stats, const app_ocxo_cal_status_t *ocxo_status)
{
  uint8_t show_controls = App_LvglUiShouldShowOcxoControls(stats);
  uint8_t ocxo_active = ((stats != NULL) && (stats->run_mode == MODDETECT_RUN_OCXO_CAL)) ? 1U : 0U;
  uint8_t dds_active = ((stats != NULL) && (stats->run_mode == MODDETECT_RUN_DDS_CAL)) ? 1U : 0U;

  App_LvglUiSetOcxoControlsVisible(show_controls);

  if (g_ui.ocxo_btn != NULL)
  {
    lv_obj_t *ocxo_btn_label = lv_obj_get_child(g_ui.ocxo_btn, 0);

    if (ocxo_btn_label != NULL)
    {
      lv_label_set_text(ocxo_btn_label, (ocxo_active != 0U) ? "ESC" : "OCXO CAL");
    }
  }

  if (g_ui.dds_cal_btn != NULL)
  {
    lv_obj_t *dds_btn_label = lv_obj_get_child(g_ui.dds_cal_btn, 0);

    if (dds_btn_label != NULL)
    {
      lv_label_set_text(dds_btn_label, (dds_active != 0U) ? "ESC" : "DDS CAL");
    }
  }

  if ((g_ui.ocxo_value != NULL) && (ocxo_status != NULL))
  {
    if (dds_active != 0U)
    {
      lv_label_set_text_fmt(g_ui.ocxo_value,
                            "DDS: %lu.%03luMHz  Off:%ldHz  Step:%ldHz",
                            (unsigned long)(APP_OCXO_CAL_DDS_FREQ_HZ / 1000000UL),
                            (unsigned long)((APP_OCXO_CAL_DDS_FREQ_HZ % 1000000UL) / 1000UL),
                            (long)ocxo_status->dds_offset_hz,
                            (long)ocxo_status->dds_offset_step_hz);
    }
    else
    {
      lv_label_set_text_fmt(g_ui.ocxo_value,
                            "DDS: %lu.%03luMHz  OCXO: %lumV  Step: %lumV",
                            (unsigned long)(APP_OCXO_CAL_DDS_FREQ_HZ / 1000000UL),
                            (unsigned long)((APP_OCXO_CAL_DDS_FREQ_HZ % 1000000UL) / 1000UL),
                            (unsigned long)ocxo_status->dac_mv,
                            (unsigned long)ocxo_status->step_mv);
    }
  }
}

static void App_LvglUiRefreshMenuVisibility(const moddetect_task_stats_t *stats)
{
  uint8_t show_cal = (g_ui.active_menu == APP_UI_MENU_CAL) ? 1U : 0U;
  uint8_t show_task = (g_ui.active_menu == APP_UI_MENU_TASK) ? 1U : 0U;
  uint8_t show_settings = (g_ui.active_menu == APP_UI_MENU_SETTINGS) ? 1U : 0U;
  uint8_t show_ocxo_controls = ((show_cal != 0U) && (App_LvglUiShouldShowOcxoControls(stats) != 0U)) ? 1U : 0U;

  App_LvglUiSetOneHidden(g_ui.calibrate_btn, (show_cal == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.ocxo_btn, (show_cal == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.dds_cal_btn, (show_cal == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.task_btn, (show_task == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.scan_status, (show_settings != 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.auto_task_btn, (show_settings == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.boot_anim_btn, (show_settings == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.runtime_monitor_btn, (show_settings == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.mixed_retry_btn, (show_settings == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.demod_setting_title, (show_settings == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.ask_demod_btn, (show_settings == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.fsk_demod_btn, (show_settings == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.ask_square_dc_btn, (show_settings == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.ask_square_threshold_btn, (show_settings == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.beep_setting_title, (show_settings == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.beep_ui_btn, (show_settings == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.beep_lock_btn, (show_settings == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.beep_analyze_btn, (show_settings == 0U) ? 1U : 0U);
  App_LvglUiSetOneHidden(g_ui.beep_demod_btn, (show_settings == 0U) ? 1U : 0U);
  App_LvglUiSetOcxoControlsVisible(show_ocxo_controls);
}

static void App_LvglUiRefreshMenuButtons(void)
{
  lv_obj_t *buttons[3] = {g_ui.menu_cal_btn, g_ui.menu_task_btn, g_ui.menu_settings_btn};
  app_ui_menu_t menus[3] = {APP_UI_MENU_CAL, APP_UI_MENU_TASK, APP_UI_MENU_SETTINGS};
  uint32_t idx;

  for (idx = 0UL; idx < 3UL; idx++)
  {
    if (buttons[idx] == NULL)
    {
      continue;
    }

    lv_obj_set_style_bg_color(buttons[idx],
                              (g_ui.active_menu == menus[idx]) ? lv_color_hex(0x2563EB) : lv_color_hex(0x334155),
                              0);
  }
}

static void App_LvglUiRefreshResult(const moddetect_task_stats_t *stats, const analyze_result_t *result)
{
  char freq_buf[32];
  char param_buf[96];

  if ((stats != NULL) && (stats->result_ready != 0U) && (stats->center_hz != 0UL))
  {
    App_LvglUiFormatFreq(freq_buf, sizeof(freq_buf), stats->center_hz);
  }
  else
  {
    snprintf(freq_buf, sizeof(freq_buf), "--.--- MHz");
  }

  lv_label_set_text(g_ui.freq_value, freq_buf);
  if ((result != NULL) &&
      (result->done != 0U) &&
      (stats != NULL) &&
      (stats->result_ready != 0U) &&
      (stats->center_hz != 0UL))
  {
    lv_label_set_text_fmt(g_ui.mod_value, "Mode: %s", App_LvglUiAnalyzeModeText(result->mode));
    App_LvglUiFormatAnalyzeParam(param_buf, sizeof(param_buf), result);
    lv_label_set_text(g_ui.demod_value, param_buf);
  }
  else
  {
    lv_label_set_text(g_ui.mod_value, "Mode: --");
    lv_label_set_text(g_ui.demod_value, "Param: --");
  }
}

static void App_LvglUiRefreshQuality(const moddetect_task_stats_t *stats)
{
  if ((g_ui.quality_line == NULL) || (stats == NULL))
  {
    return;
  }

  lv_label_set_text_fmt(g_ui.quality_line,
                        "Blocks:%lu  Drops:%lu  Seq:%lu",
                        (unsigned long)stats->process_cnt,
                        (unsigned long)stats->submit_drop_cnt,
                        (unsigned long)stats->last_sequence);
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
        App_LvglUiSwitchToTaskMenu();
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
  else if (ocxo_status.state == APP_OCXO_CAL_CURRENT)
  {
    App_LvglUiSetHwStatus(g_ui.ocxo_status, "OCXO RAM", lv_color_hex(0x16A34A));
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
  else if ((stats != NULL) && (stats->run_mode == MODDETECT_RUN_DDS_CAL))
  {
    App_LvglUiSetHwStatus(g_ui.overall_status, "DDS CAL", lv_color_hex(0xCA8A04));
  }
  else if (((stats != NULL) && (stats->run_mode == MODDETECT_RUN_TASK) && (stats->result_ready == 0U)) ||
           (analyze_is_active() != 0U))
  {
    App_LvglUiSetHwStatus(g_ui.overall_status, "RUNNING", lv_color_hex(0xCA8A04));
  }
  else if ((stats != NULL) && (stats->run_mode == MODDETECT_RUN_TASK) &&
           (stats->result_ready != 0U) && (stats->center_hz != 0UL) &&
           (analyze_is_done() != 0U))
  {
    App_LvglUiSetHwStatus(g_ui.overall_status, "ANALYZE OK", lv_color_hex(0x16A34A));
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

static lv_obj_t *App_LvglUiCreateMenuButton(lv_obj_t *parent,
                                            const char *text,
                                            lv_coord_t x,
                                            lv_coord_t y,
                                            app_ui_menu_t menu)
{
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_t *label;

  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, UI_SIDE_MENU_W, UI_SIDE_MENU_H);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x334155), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_add_event_cb(btn, App_LvglUiMenuButtonEventCb, LV_EVENT_CLICKED, (void *)(uintptr_t)menu);

  label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_center(label);

  return btn;
}


/* 判断 OCXO 校准是否处于活动或已请求状态；同时覆盖“刚点 OCXO CAL 但任务还没消费请求”的短暂窗口。 */
static uint8_t App_LvglUiIsOcxoCalModeActive(void)
{
  moddetect_task_stats_t stats;

  moddetect_task_get_stats(&stats);
  if ((stats.run_mode == MODDETECT_RUN_OCXO_CAL) ||
      (moddetect_task_get_mode() == MODDETECT_RUN_OCXO_CAL))
  {
    return 1U;
  }

  return 0U;
}

static uint8_t App_LvglUiIsDdsCalModeActive(void)
{
  moddetect_task_stats_t stats;

  moddetect_task_get_stats(&stats);
  if ((stats.run_mode == MODDETECT_RUN_DDS_CAL) ||
      (moddetect_task_get_mode() == MODDETECT_RUN_DDS_CAL))
  {
    return 1U;
  }

  return 0U;
}

/* 离开 OCXO 校准上下文时自动退出 RUN 状态；未保存电压仍按 app_ocxo_cal_leave() 规则保持为 RAM/CURRENT。 */
static void App_LvglUiLeaveOcxoCalIfActive(void)
{
  if (App_LvglUiIsOcxoCalModeActive() == 0U)
  {
    return;
  }

  App_LvglUiCloseSaveConfirm();
  app_ocxo_cal_leave();
  moddetect_task_request_mode(MODDETECT_RUN_IDLE);
  App_LvglUiSetOcxoControlsVisible(0U);
}

static void App_LvglUiLeaveDdsCalIfActive(void)
{
  if (App_LvglUiIsDdsCalModeActive() == 0U)
  {
    return;
  }

  App_LvglUiCloseSaveConfirm();
  app_ocxo_cal_dds_offset_leave();
  moddetect_task_request_mode(MODDETECT_RUN_IDLE);
  App_LvglUiSetOcxoControlsVisible(0U);
}

static void App_LvglUiSwitchToTaskMenu(void)
{
  g_ui.active_menu = APP_UI_MENU_TASK;
  App_LvglUiSetOcxoControlsVisible(0U);
  App_LvglUiRefreshMenuButtons();
  App_LvglUiRefreshMenuVisibility(NULL);
}

/* 保存前二次确认，避免校准调节时误触把临时值写入 Flash。 */
static void App_LvglUiShowSaveConfirm(moddetect_run_mode_t mode)
{
  lv_obj_t *overlay;
  lv_obj_t *card;
  lv_obj_t *title;
  lv_obj_t *text;
  lv_obj_t *cancel_btn;
  lv_obj_t *save_btn;
  lv_obj_t *label;
  const char *message = "Save calibration value?";

  if (g_ui.save_confirm_overlay != NULL)
  {
    return;
  }

  if (mode == MODDETECT_RUN_OCXO_CAL)
  {
    message = "Save OCXO voltage?";
  }
  else if (mode == MODDETECT_RUN_DDS_CAL)
  {
    message = "Save DDS offset?";
  }

  g_ui.pending_save_mode = mode;

  overlay = lv_obj_create(lv_screen_active());
  g_ui.save_confirm_overlay = overlay;
  lv_obj_set_size(overlay, 800, 480);
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(0x020617), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
  lv_obj_set_style_border_width(overlay, 0, 0);
  lv_obj_set_style_pad_all(overlay, 0, 0);
  lv_obj_set_scrollbar_mode(overlay, LV_SCROLLBAR_MODE_OFF);

  card = lv_obj_create(overlay);
  lv_obj_set_size(card, 320, 172);
  lv_obj_center(card);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x0F172A), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x38BDF8), 0);
  lv_obj_set_style_border_width(card, 2, 0);
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

  title = lv_label_create(card);
  lv_label_set_text(title, "Confirm Save");
  lv_obj_set_style_text_color(title, lv_color_hex(0xE0F2FE), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

  text = lv_label_create(card);
  lv_label_set_text(text, message);
  lv_obj_set_style_text_color(text, lv_color_hex(0xCBD5E1), 0);
  lv_obj_set_style_text_font(text, &lv_font_montserrat_14, 0);
  lv_obj_align(text, LV_ALIGN_TOP_MID, 0, 58);

  cancel_btn = lv_button_create(card);
  lv_obj_set_size(cancel_btn, 118, 48);
  lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_LEFT, 30, -20);
  lv_obj_set_style_radius(cancel_btn, 8, 0);
  lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x334155), 0);
  lv_obj_set_style_bg_opa(cancel_btn, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(cancel_btn, 0, 0);
  lv_obj_add_event_cb(cancel_btn, App_LvglUiSaveConfirmEventCb, LV_EVENT_CLICKED, (void *)0U);
  label = lv_label_create(cancel_btn);
  lv_label_set_text(label, "CANCEL");
  lv_obj_center(label);

  save_btn = lv_button_create(card);
  lv_obj_set_size(save_btn, 118, 48);
  lv_obj_align(save_btn, LV_ALIGN_BOTTOM_RIGHT, -30, -20);
  lv_obj_set_style_radius(save_btn, 8, 0);
  lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x16A34A), 0);
  lv_obj_set_style_bg_opa(save_btn, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(save_btn, 0, 0);
  lv_obj_add_event_cb(save_btn, App_LvglUiSaveConfirmEventCb, LV_EVENT_CLICKED, (void *)1U);
  label = lv_label_create(save_btn);
  lv_label_set_text(label, "SAVE");
  lv_obj_center(label);
}

static void App_LvglUiCloseSaveConfirm(void)
{
  if (g_ui.save_confirm_overlay == NULL)
  {
    return;
  }

  lv_obj_delete(g_ui.save_confirm_overlay);
  g_ui.save_confirm_overlay = NULL;
}

static void App_LvglUiExecuteConfirmedSave(moddetect_run_mode_t mode)
{
  App_LvglUiCloseSaveConfirm();

  if (mode == MODDETECT_RUN_DDS_CAL)
  {
    if (app_ocxo_cal_save_dds_offset_to_flash() != 0U)
    {
      app_buzzer_notify_cal_done();
    }
    app_ocxo_cal_dds_offset_leave();
    moddetect_task_request_mode(MODDETECT_RUN_IDLE);
    App_LvglUiSwitchToTaskMenu();
    return;
  }

  if (mode == MODDETECT_RUN_OCXO_CAL)
  {
    if (app_ocxo_cal_save_to_flash() != 0U)
    {
      app_buzzer_notify_cal_done();
    }
    moddetect_task_request_mode(MODDETECT_RUN_IDLE);
    App_LvglUiSwitchToTaskMenu();
  }
}

/* UI 命令入口：事件回调只提交命令，具体状态变更集中在这里维护。 */
static void App_LvglUiDispatchCommand(app_ui_command_t command, uintptr_t value)
{
  app_buzzer_notify_ui_action();

  if (command == APP_UI_CMD_SAVE_CONFIRMED)
  {
    App_LvglUiExecuteConfirmedSave((moddetect_run_mode_t)value);
    return;
  }

  if (command == APP_UI_CMD_MENU)
  {
    if (value <= (uintptr_t)APP_UI_MENU_SETTINGS)
    {
      app_ui_menu_t next_menu = (app_ui_menu_t)value;

      if (next_menu != APP_UI_MENU_CAL)
      {
        App_LvglUiLeaveOcxoCalIfActive();
        App_LvglUiLeaveDdsCalIfActive();
      }

      g_ui.active_menu = next_menu;
      App_LvglUiRefreshMenuButtons();
    }
    return;
  }

  if (command == APP_UI_CMD_MODE)
  {
    moddetect_run_mode_t mode = (moddetect_run_mode_t)value;

    if ((mode == MODDETECT_RUN_OCXO_CAL) && (App_LvglUiIsOcxoCalModeActive() != 0U))
    {
      App_LvglUiLeaveOcxoCalIfActive();
      return;
    }

    if ((mode == MODDETECT_RUN_DDS_CAL) && (App_LvglUiIsDdsCalModeActive() != 0U))
    {
      App_LvglUiLeaveDdsCalIfActive();
      return;
    }

    if (mode != MODDETECT_RUN_OCXO_CAL)
    {
      App_LvglUiLeaveOcxoCalIfActive();
    }

    if (mode != MODDETECT_RUN_DDS_CAL)
    {
      App_LvglUiLeaveDdsCalIfActive();
    }

    moddetect_task_request_mode(mode);
    return;
  }

  if (command == APP_UI_CMD_OCXO_ACTION)
  {
    app_ui_ocxo_action_t action = (app_ui_ocxo_action_t)value;
    app_ocxo_cal_status_t status;

    if (action == APP_UI_AUTO_TASK_TOGGLE)
    {
      uint8_t next_enable = (app_ocxo_cal_get_auto_task_enable() == 0U) ? 1U : 0U;
      (void)app_ocxo_cal_set_auto_task_enable(next_enable);
      return;
    }

    if (action == APP_UI_BOOT_ANIM_TOGGLE)
    {
      uint8_t next_enable = (app_ocxo_cal_get_boot_anim_enable() == 0U) ? 1U : 0U;
      (void)app_ocxo_cal_set_boot_anim_enable(next_enable);
      return;
    }

    if (action == APP_UI_RUNTIME_MONITOR_TOGGLE)
    {
      uint8_t next_enable = (app_ocxo_cal_get_runtime_monitor_enable() == 0U) ? 1U : 0U;
      (void)app_ocxo_cal_set_runtime_monitor_enable(next_enable);
      return;
    }

    if (action == APP_UI_MIXED_RETRY_CYCLE)
    {
      (void)app_ocxo_cal_cycle_mixed_retry_count();
      return;
    }

    if (action == APP_UI_ASK_DEMOD_TOGGLE)
    {
      uint8_t next_enable = (app_ocxo_cal_get_ask_analog_demod_enable() == 0U) ? 1U : 0U;
      (void)app_ocxo_cal_set_ask_analog_demod_enable(next_enable);
      return;
    }

    if (action == APP_UI_FSK_DEMOD_TOGGLE)
    {
      uint8_t next_enable = (app_ocxo_cal_get_fsk_analog_demod_enable() == 0U) ? 1U : 0U;
      (void)app_ocxo_cal_set_fsk_analog_demod_enable(next_enable);
      return;
    }

    if (action == APP_UI_ASK_SQUARE_DC_CYCLE)
    {
      (void)app_ocxo_cal_cycle_ask_square_dc_shift();
      return;
    }

    if (action == APP_UI_ASK_SQUARE_THRESHOLD_CYCLE)
    {
      (void)app_ocxo_cal_cycle_ask_square_threshold_code();
      return;
    }

    if (action == APP_UI_BEEP_UI_TOGGLE)
    {
      uint8_t next_enable = (app_ocxo_cal_get_beep_ui_enable() == 0U) ? 1U : 0U;
      (void)app_ocxo_cal_set_beep_ui_enable(next_enable);
      return;
    }

    if (action == APP_UI_BEEP_SWEEP_LOCK_TOGGLE)
    {
      uint8_t next_enable = (app_ocxo_cal_get_beep_sweep_lock_enable() == 0U) ? 1U : 0U;
      (void)app_ocxo_cal_set_beep_sweep_lock_enable(next_enable);
      return;
    }

    if (action == APP_UI_BEEP_ANALYZE_DONE_TOGGLE)
    {
      uint8_t next_enable = (app_ocxo_cal_get_beep_analyze_done_enable() == 0U) ? 1U : 0U;
      (void)app_ocxo_cal_set_beep_analyze_done_enable(next_enable);
      return;
    }

    if (action == APP_UI_BEEP_DEMOD_START_TOGGLE)
    {
      uint8_t next_enable = (app_ocxo_cal_get_beep_demod_start_enable() == 0U) ? 1U : 0U;
      (void)app_ocxo_cal_set_beep_demod_start_enable(next_enable);
      return;
    }

    if (moddetect_task_get_mode() == MODDETECT_RUN_DDS_CAL)
    {
      app_ocxo_cal_get_status(&status);
      switch (action)
      {
        case APP_UI_OCXO_STEP:
          app_ocxo_cal_dds_offset_cycle_step();
          break;

        case APP_UI_OCXO_DEC:
          app_ocxo_cal_dds_offset_adjust(-status.dds_offset_step_hz);
          break;

        case APP_UI_OCXO_INC:
          app_ocxo_cal_dds_offset_adjust(status.dds_offset_step_hz);
          break;

        case APP_UI_OCXO_SAVE:
          App_LvglUiShowSaveConfirm(MODDETECT_RUN_DDS_CAL);
          break;

        default:
          break;
      }
      return;
    }

    if (moddetect_task_get_mode() != MODDETECT_RUN_OCXO_CAL)
    {
      return;
    }

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
        App_LvglUiShowSaveConfirm(MODDETECT_RUN_OCXO_CAL);
        break;

      default:
        break;
    }
  }
}

/* 处理模式按钮点击，将用户选择转交给 ModDetectTask。 */
static void App_LvglUiModeButtonEventCb(lv_event_t *event)
{
  if (lv_event_get_code(event) == LV_EVENT_CLICKED)
  {
    App_LvglUiDispatchCommand(APP_UI_CMD_MODE, (uintptr_t)lv_event_get_user_data(event));
  }
}

/* 处理 OCXO 电压调节按钮。 */
static void App_LvglUiOcxoButtonEventCb(lv_event_t *event)
{
  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
  {
    return;
  }

  App_LvglUiDispatchCommand(APP_UI_CMD_OCXO_ACTION, (uintptr_t)lv_event_get_user_data(event));
}

static void App_LvglUiMenuButtonEventCb(lv_event_t *event)
{
  if (lv_event_get_code(event) == LV_EVENT_CLICKED)
  {
    App_LvglUiDispatchCommand(APP_UI_CMD_MENU, (uintptr_t)lv_event_get_user_data(event));
  }
}

static void App_LvglUiSaveConfirmEventCb(lv_event_t *event)
{
  uintptr_t accepted;

  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
  {
    return;
  }

  accepted = (uintptr_t)lv_event_get_user_data(event);
  if (accepted == 0U)
  {
    App_LvglUiDispatchCommand(APP_UI_CMD_MENU, (uintptr_t)g_ui.active_menu);
    App_LvglUiCloseSaveConfirm();
    return;
  }

  App_LvglUiDispatchCommand(APP_UI_CMD_SAVE_CONFIRMED, (uintptr_t)g_ui.pending_save_mode);
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


