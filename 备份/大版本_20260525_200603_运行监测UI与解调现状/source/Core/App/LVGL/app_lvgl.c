#include "app_lvgl.h"

#include "app_lvgl_port_disp.h"
#include "app_lvgl_port_indev.h"
#include "app_lvgl_ui.h"

#include "lvgl.h"
#include "cmsis_os2.h"
#include "main.h"

/*
 * The STM32 CMSIS-RTOS2 headers declare osThreadDetach(), but the
 * FreeRTOS wrapper used by this project does not provide a real symbol.
 * Treat it as a no-op: current LVGL integration does not rely on join or
 * detach semantics and the generated tasks are already effectively detached.
 */
osStatus_t osThreadDetach(osThreadId_t thread_id)
{
  (void)thread_id;
  return osOK;
}

void App_LvglInit(void)
{
  /*
   * Minimal LVGL bring-up sequence:
   * 1. initialize the LVGL core
   * 2. bind the millisecond tick source to HAL_GetTick()
   * 3. initialize display and input ports
   * 4. create the initial UI objects
   */
  lv_init();
  lv_tick_set_cb(HAL_GetTick);

  App_LvglPortDispInit();
  App_LvglPortIndevInit();
  App_LvglUiInit();
}

void App_LvglRun(void)
{
  /*
   * Keep the display task lightweight: refresh app state, then let LVGL run
   * its own timer-driven work queue.
   */
  App_LvglUiRefresh();
  (void)lv_timer_handler();
}