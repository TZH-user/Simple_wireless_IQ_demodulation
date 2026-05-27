#ifndef APP_OCXO_CAL_H
#define APP_OCXO_CAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 恒温晶振控制电压默认值，单位 mV；无历史校准数据时上电使用它。 */
#define APP_OCXO_CAL_DEFAULT_MV 1400U
/* 恒温晶振控制电压下限，避免误触把 PA4 推到过低。 */
#define APP_OCXO_CAL_MIN_MV 800U
/* 恒温晶振控制电压上限，避免误触把 PA4 推到过高。 */
#define APP_OCXO_CAL_MAX_MV 2200U
/* 细调步进，适合最后观察 IQ 矢量图慢速旋转时微调。 */
#define APP_OCXO_CAL_STEP_FINE_MV 1U
/* 中调步进，适合常规手动搜索。 */
#define APP_OCXO_CAL_STEP_MID_MV 10U
/* 粗调步进，适合快速拉近目标电压。 */
#define APP_OCXO_CAL_STEP_COARSE_MV 100U
/* 恒温晶振校准时使用的 AD9959 通道。 */
#define APP_OCXO_CAL_DDS_CH 0U
/* 恒温晶振校准时 AD9959 CH0 输出频率，单位 Hz。 */
#define APP_OCXO_CAL_DDS_FREQ_HZ 120000000UL
/* 恒温晶振校准时 AD9959 CH0 输出幅度码，范围 0~1023。 */
#define APP_OCXO_CAL_DDS_AMP_CODE 1023U
/* 上电自检通过后是否默认自动进入任务；无历史设置时使用该值。 */
#define APP_OCXO_CAL_AUTO_TASK_DEFAULT_ENABLE 1U
/* 没有历史设置时，上电默认播放启动动画；UI 里的 ANIM ON/OFF 会写入 Flash，下次上电生效。 */
#define APP_OCXO_CAL_BOOT_ANIM_DEFAULT_ENABLE 1U
/* 运行中切频/切调制监测默认关闭；在 SET 菜单打开后写入 Flash 并立即生效。 */
#define APP_OCXO_CAL_RUNTIME_MONITOR_DEFAULT_ENABLE 0U
/* ASK 普通=ASK 数字解调；增强=改走 AM 包络解调，适合先验证示波器输出。 */
#define APP_OCXO_CAL_ASK_ANALOG_DEMOD_DEFAULT_ENABLE 0U
/* FSK 普通=FSK 数字判决；增强=改走 FM 频率解调，适合先验证模拟波形。 */
#define APP_OCXO_CAL_FSK_ANALOG_DEMOD_DEFAULT_ENABLE 0U
/* 屏幕按键蜂鸣默认关闭；打开后每次触摸按钮会短响。 */
#define APP_OCXO_CAL_BEEP_UI_DEFAULT_ENABLE 0U
/* 扫频锁定提示音默认关闭。 */
#define APP_OCXO_CAL_BEEP_SWEEP_LOCK_DEFAULT_ENABLE 0U
/* 识别完成提示音默认关闭。 */
#define APP_OCXO_CAL_BEEP_ANALYZE_DONE_DEFAULT_ENABLE 0U
/* 解调输出启动提示音默认关闭。 */
#define APP_OCXO_CAL_BEEP_DEMOD_START_DEFAULT_ENABLE 0U
/* MIXED 识别结果自动重分析默认次数；0=不重试，1~5=固定次数，255=一直重试。 */
#define APP_OCXO_CAL_MIXED_RETRY_DEFAULT_COUNT 3U
/* MIXED 自动重分析最多显示到 5 次，再按一次进入无限重试。 */
#define APP_OCXO_CAL_MIXED_RETRY_MAX_COUNT 5U
/* MIXED 自动重分析无限重试的内部编码，UI 显示为 INF。 */
#define APP_OCXO_CAL_MIXED_RETRY_INFINITE 255U

typedef enum
{
    APP_OCXO_CAL_NONE = 0,
    APP_OCXO_CAL_HISTORY,
    APP_OCXO_CAL_CURRENT,
    APP_OCXO_CAL_RUNNING,
    APP_OCXO_CAL_SAVING,
    APP_OCXO_CAL_SAVE_OK,
    APP_OCXO_CAL_ERROR
} app_ocxo_cal_state_t;

typedef struct
{
    uint32_t dac_mv;
    uint32_t step_mv;
    app_ocxo_cal_state_t state;
    uint8_t valid;
    uint8_t flash_loaded;
    uint8_t auto_task_enable;
    uint8_t boot_anim_enable;
    uint8_t runtime_monitor_enable;
    uint8_t ask_analog_demod_enable;
    uint8_t fsk_analog_demod_enable;
    uint8_t beep_ui_enable;
    uint8_t beep_sweep_lock_enable;
    uint8_t beep_analyze_done_enable;
    uint8_t beep_demod_start_enable;
    uint8_t mixed_retry_count;
} app_ocxo_cal_status_t;

void app_ocxo_cal_init(void);
uint8_t app_ocxo_cal_load_from_flash(void);
uint8_t app_ocxo_cal_save_to_flash(void);
void app_ocxo_cal_enter(void);
void app_ocxo_cal_leave(void);
void app_ocxo_cal_set_mv(uint32_t mv);
void app_ocxo_cal_adjust(int32_t delta_mv);
void app_ocxo_cal_cycle_step(void);
void app_ocxo_cal_get_status(app_ocxo_cal_status_t *status_out);
uint8_t app_ocxo_cal_get_auto_task_enable(void);
uint8_t app_ocxo_cal_set_auto_task_enable(uint8_t enable);
uint8_t app_ocxo_cal_get_boot_anim_enable(void);
uint8_t app_ocxo_cal_set_boot_anim_enable(uint8_t enable);
uint8_t app_ocxo_cal_get_runtime_monitor_enable(void);
uint8_t app_ocxo_cal_set_runtime_monitor_enable(uint8_t enable);
uint8_t app_ocxo_cal_get_ask_analog_demod_enable(void);
uint8_t app_ocxo_cal_set_ask_analog_demod_enable(uint8_t enable);
uint8_t app_ocxo_cal_get_fsk_analog_demod_enable(void);
uint8_t app_ocxo_cal_set_fsk_analog_demod_enable(uint8_t enable);
uint8_t app_ocxo_cal_get_beep_ui_enable(void);
uint8_t app_ocxo_cal_set_beep_ui_enable(uint8_t enable);
uint8_t app_ocxo_cal_get_beep_sweep_lock_enable(void);
uint8_t app_ocxo_cal_set_beep_sweep_lock_enable(uint8_t enable);
uint8_t app_ocxo_cal_get_beep_analyze_done_enable(void);
uint8_t app_ocxo_cal_set_beep_analyze_done_enable(uint8_t enable);
uint8_t app_ocxo_cal_get_beep_demod_start_enable(void);
uint8_t app_ocxo_cal_set_beep_demod_start_enable(uint8_t enable);
uint8_t app_ocxo_cal_get_mixed_retry_count(void);
uint8_t app_ocxo_cal_cycle_mixed_retry_count(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_OCXO_CAL_H */
