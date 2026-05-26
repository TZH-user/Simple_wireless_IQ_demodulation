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

#ifdef __cplusplus
}
#endif

#endif /* APP_OCXO_CAL_H */
