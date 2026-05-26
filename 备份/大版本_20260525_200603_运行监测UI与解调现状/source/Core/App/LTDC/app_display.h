#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化显示测试阶段需要的背光等最小外设。 */
void App_DisplayInit(void);
/* 运行首轮点屏测试图案，用于验证 LTDC + SDRAM + 背光链路。 */
void App_DisplayRunPowerOnPattern(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_DISPLAY_H */
