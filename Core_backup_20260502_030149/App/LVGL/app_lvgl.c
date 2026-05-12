#include "app_lvgl.h"

#include "app_lvgl_port_disp.h"
#include "app_lvgl_port_indev.h"
#include "app_lvgl_ui.h"

#include "lvgl.h"
#include "cmsis_os2.h"
#include "main.h"

/*
 * 当前 ST 提供的 CMSIS-RTOS2 头文件声明了 osThreadDetach()，
 * 但这套 FreeRTOS 封装实现里并没有真正提供该符号。
 *
 * 对当前工程来说这是一个可接受的兼容收口：
 * 1. ST 这套实现默认线程就是 detached 模式
 * 2. 第一轮最小 LVGL 接入不会动态创建/回收 LVGL 工作线程
 * 3. 因此这里把 osThreadDetach() 实现成 no-op，语义上仍然成立
 *
 * 后面如果切换到更新的 CMSIS-RTOS2 适配层，或引入真正依赖 join/detach 的线程模型，
 * 这段兼容代码就应该删除，改回用底层正式实现。
 */
osStatus_t osThreadDetach(osThreadId_t thread_id)
{
  (void)thread_id;
  return osOK;
}

void App_LvglInit(void)
{
  /*
   * 第一轮最小接入只做 4 件事：
   * 1. 初始化 LVGL 内核
   * 2. 告诉 LVGL 当前系统毫秒 tick 从 HAL_GetTick() 获取
   * 3. 初始化显示端口
   * 4. 创建最小 UI
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
   * LVGL 在 RTOS 下的主循环非常简单：
   * 周期性调用 lv_timer_handler() 即可。
   * 具体 delay 节奏留给 DisplayTask 控制，避免把任务调度策略写死在 LVGL 包装层。
   */
  App_LvglUiRefresh();
  (void)lv_timer_handler();
}
