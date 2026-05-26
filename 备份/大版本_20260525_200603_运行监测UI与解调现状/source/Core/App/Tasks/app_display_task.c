#include "cmsis_os2.h"

#include "../BootAnim/app_boot_anim.h"
#include "app_display.h"
#include "app_lvgl.h"
#include "app_ocxo_cal.h"
#include "app_touch_gt9xx.h"

/*
 * 这个文件用于覆盖 freertos.c 里 CubeMX 生成的 __weak StartDisplayTask()。
 * 这样显示启动逻辑就能留在用户文件里，后续再次 Generate Code 时更稳。
 */
void StartDisplayTask(void *argument)
{
  (void)argument;

  /*
   * 当前主线已经进入 LVGL 阶段，所以任务职责明确为：
   * 1. 完成最小显示 bring-up（背光、可选彩条）
   * 2. 初始化 LVGL
   * 3. 周期性驱动 LVGL 主循环
   */
  /* 显示主线只负责显示/LVGL，不承担触摸 IIC 访问，减少职责耦合。 */
  App_DisplayInit();
  App_DisplayRunPowerOnPattern();
  if (app_ocxo_cal_get_boot_anim_enable() != 0U)
  {
    App_BootAnimPlay();
  }
  App_LvglInit();

  /*
   * 这里使用固定 5ms 的任务节拍即可满足当前最小 UI。
   * 后面如果引入更复杂页面、动画或触摸输入，再根据实际负载调整。
   */
  for(;;) {
    /* LVGL 主循环保持轻量，避免在该任务里做阻塞式外设读写。 */
    App_LvglRun();
    osDelay(5);
  }
}

/*
 * Dedicated touch task:
 * 1. wait for EXTI notification from ISR
 * 2. perform software I2C register read in task context
 * 3. update shared touch state for LVGL indev read callback
 */
void StartTouchTask(void *argument)
{
  (void)argument;

  /* 触摸任务内部完成：初始化 -> 等待 EXTI -> 读取 GT9xx -> 更新共享状态。 */
  App_TouchTaskRun();
}
