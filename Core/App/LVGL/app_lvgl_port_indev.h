#ifndef APP_LVGL_PORT_INDEV_H
#define APP_LVGL_PORT_INDEV_H

#ifdef __cplusplus
extern "C" {
#endif

/* 注册 LVGL pointer 输入设备，并绑定到默认显示。 */
void App_LvglPortIndevInit(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_LVGL_PORT_INDEV_H */
