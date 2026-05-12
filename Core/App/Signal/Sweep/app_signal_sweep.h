/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_SIGNAL_SWEEP_H
/* 宏定义说明：APP_SIGNAL_SWEEP_H = (无显式值)；app_signal_sweep.h 的头文件保护宏，避免重复 include 导致类型/声明重复。 */
#define APP_SIGNAL_SWEEP_H

#include <stdint.h>
#include "app_signal_detect.h"

/* 条件编译判断：如果该宏已经定义，则编译下面代码块。 */
#ifdef __cplusplus
extern "C" {
#endif

/*
 * 扫频模块职责：
 *   1. 管理粗扫、细扫、锁定三个阶段。
 *   2. 按 ADC block 统计 I/Q 峰峰值，并用 Vpp 曲线估计载波频率。
 *   3. 通过 DDS 控制层更新本振 LO 频率。
 *
 * 设计边界：
 *   - 本模块不直接启动 ADC/DMA。
 *   - 本模块不做 IQ 残余频偏分析。
 *   - 本模块对外继续使用 app_signal_detect_status_t，避免影响 UI 和日志读取。
 */

/* 初始化扫频状态机，并立即从粗扫阶段开始。 */
/* 函数跳转：调用 app_signal_sweep_init()，初始化扫频模块并启动首次粗扫。 */
void app_signal_sweep_init(void);

/* 请求重新扫频；通常用于失锁、用户手动重扫或参数重置。 */
/* 函数跳转：调用 app_signal_sweep_request_rescan()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
void app_signal_sweep_request_rescan(void);

/* 输入一个 ADC I/Q block，扫频状态机按 dwell 计数决定是否切换下一个 LO 频点。 */
/* 函数跳转：调用 app_signal_sweep_process_block()，把 ADC block 送入扫频状态机，累计 dwell 并推进频点。 */
void app_signal_sweep_process_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt);

/* 获取当前扫频/锁定状态快照；调用方传入的结构体会被完整覆盖。 */
/* 函数跳转：调用 app_signal_sweep_get_status()，读取扫频状态快照给外部模块/界面使用。 */
void app_signal_sweep_get_status(app_signal_detect_status_t *status_out);

/* 条件编译判断：如果该宏已经定义，则编译下面代码块。 */
#ifdef __cplusplus
}
#endif

#endif /* APP_SIGNAL_SWEEP_H */
