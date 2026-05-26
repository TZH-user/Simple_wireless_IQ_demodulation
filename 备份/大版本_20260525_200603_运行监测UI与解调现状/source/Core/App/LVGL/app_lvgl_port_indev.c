#include "app_lvgl_port_indev.h"

#include "app_touch_gt9xx.h"

#include "lvgl.h"

static lv_indev_t *g_lvgl_indev = NULL;

static void App_LvglPortIndevRead(lv_indev_t *indev, lv_indev_data_t *data);

void App_LvglPortIndevInit(void)
{
  lv_display_t *disp = lv_display_get_default();

  /* LVGL9 新接口：创建独立输入设备对象并指定为 pointer 类型。 */
  g_lvgl_indev = lv_indev_create();
  lv_indev_set_type(g_lvgl_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(g_lvgl_indev, App_LvglPortIndevRead);

  /* 显式绑定默认显示，避免多显示场景下输入设备挂错目标。 */
  if(disp != NULL) {
    lv_indev_set_display(g_lvgl_indev, disp);
  }
}

static void App_LvglPortIndevRead(lv_indev_t *indev, lv_indev_data_t *data)
{
  uint16_t x = 0U;
  uint16_t y = 0U;

  (void)indev;

  /*
   * 约束：read_cb 必须非阻塞。
   * 这里只读取 TouchTask 维护的缓存状态，不在该回调里执行 IIC 事务。
   */
  if(App_TouchGetPoint(&x, &y)) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = (lv_coord_t)x;
    data->point.y = (lv_coord_t)y;
  }
  else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}
