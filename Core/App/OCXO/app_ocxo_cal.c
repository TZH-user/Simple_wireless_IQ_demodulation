#include "app_ocxo_cal.h"

#include "app_board_flash.h"
#include "app_dds_ctrl.h"
#include "dac.h"
#include "RtosTypes.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define APP_OCXO_CAL_DAC_MAX_CODE 4095U
#define APP_OCXO_CAL_DAC_VREF_MV 3300U
#define APP_OCXO_CAL_FLASH_ADDR (APP_BOARD_FLASH_TOTAL_SIZE - (2UL * APP_BOARD_FLASH_SECTOR_SIZE))
#define APP_OCXO_CAL_FLASH_MAGIC 0x4F43584FUL
#define APP_OCXO_CAL_FLASH_VERSION 9UL
#define APP_OCXO_CAL_FLASH_CRC_SEED 2166136261UL

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    uint32_t dac_mv;
    uint32_t crc;
} app_ocxo_cal_flash_record_v1_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    uint32_t dac_mv;
    uint32_t auto_task_enable;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t crc;
} app_ocxo_cal_flash_record_v2_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    uint32_t dac_mv;
    uint32_t auto_task_enable;
    uint32_t boot_anim_enable;
    uint32_t reserved0;
    uint32_t crc;
} app_ocxo_cal_flash_record_v3_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    uint32_t dac_mv;
    uint32_t auto_task_enable;
    uint32_t boot_anim_enable;
    uint32_t runtime_monitor_enable;
    uint32_t crc;
} app_ocxo_cal_flash_record_v4_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    uint32_t dac_mv;
    uint32_t auto_task_enable;
    uint32_t boot_anim_enable;
    uint32_t runtime_monitor_enable;
    uint32_t ask_analog_demod_enable;
    uint32_t fsk_analog_demod_enable;
    uint32_t beep_ui_enable;
    uint32_t beep_sweep_lock_enable;
    uint32_t beep_analyze_done_enable;
    uint32_t beep_demod_start_enable;
    uint32_t crc;
} app_ocxo_cal_flash_record_v5_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    uint32_t dac_mv;
    uint32_t auto_task_enable;
    uint32_t boot_anim_enable;
    uint32_t runtime_monitor_enable;
    uint32_t ask_analog_demod_enable;
    uint32_t fsk_analog_demod_enable;
    uint32_t beep_ui_enable;
    uint32_t beep_sweep_lock_enable;
    uint32_t beep_analyze_done_enable;
    uint32_t beep_demod_start_enable;
    uint32_t mixed_retry_count;
    uint32_t crc;
} app_ocxo_cal_flash_record_v6_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    uint32_t dac_mv;
    uint32_t auto_task_enable;
    uint32_t boot_anim_enable;
    uint32_t runtime_monitor_enable;
    uint32_t ask_analog_demod_enable;
    uint32_t fsk_analog_demod_enable;
    uint32_t beep_ui_enable;
    uint32_t beep_sweep_lock_enable;
    uint32_t beep_analyze_done_enable;
    uint32_t beep_demod_start_enable;
    uint32_t mixed_retry_count;
    uint32_t ask_square_dc_shift;
    uint32_t ask_square_threshold_code;
    uint32_t crc;
} app_ocxo_cal_flash_record_v7_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    uint32_t dac_mv;
    uint32_t auto_task_enable;
    uint32_t boot_anim_enable;
    uint32_t runtime_monitor_enable;
    uint32_t ask_analog_demod_enable;
    uint32_t fsk_analog_demod_enable;
    uint32_t beep_ui_enable;
    uint32_t beep_sweep_lock_enable;
    uint32_t beep_analyze_done_enable;
    uint32_t beep_demod_start_enable;
    uint32_t mixed_retry_count;
    uint32_t ask_square_dc_shift;
    uint32_t ask_square_threshold_code;
    int32_t dds_offset_hz;
    uint32_t crc;
} app_ocxo_cal_flash_record_v8_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    uint32_t dac_mv;
    uint32_t auto_task_enable;
    uint32_t boot_anim_enable;
    uint32_t runtime_monitor_enable;
    uint32_t ask_analog_demod_enable;
    uint32_t fsk_analog_demod_enable;
    uint32_t beep_ui_enable;
    uint32_t beep_sweep_lock_enable;
    uint32_t beep_analyze_done_enable;
    uint32_t beep_demod_start_enable;
    uint32_t mixed_retry_count;
    uint32_t ask_square_dc_shift;
    uint32_t ask_square_threshold_code;
    int32_t dds_offset_hz;
    uint32_t background_enable;
    uint32_t crc;
} app_ocxo_cal_flash_record_v9_t;

static app_ocxo_cal_status_t g_ocxo_status =
{
    .dac_mv = APP_OCXO_CAL_DEFAULT_MV,
    .step_mv = APP_OCXO_CAL_STEP_MID_MV,
    .state = APP_OCXO_CAL_NONE,
    .valid = 0U,
    .flash_loaded = 0U,
    .auto_task_enable = APP_OCXO_CAL_AUTO_TASK_DEFAULT_ENABLE,
    .boot_anim_enable = APP_OCXO_CAL_BOOT_ANIM_DEFAULT_ENABLE,
    .background_enable = APP_OCXO_CAL_BACKGROUND_DEFAULT_ENABLE,
    .runtime_monitor_enable = APP_OCXO_CAL_RUNTIME_MONITOR_DEFAULT_ENABLE,
    .ask_analog_demod_enable = APP_OCXO_CAL_ASK_ANALOG_DEMOD_DEFAULT_ENABLE,
    .fsk_analog_demod_enable = APP_OCXO_CAL_FSK_ANALOG_DEMOD_DEFAULT_ENABLE,
    .beep_ui_enable = APP_OCXO_CAL_BEEP_UI_DEFAULT_ENABLE,
    .beep_sweep_lock_enable = APP_OCXO_CAL_BEEP_SWEEP_LOCK_DEFAULT_ENABLE,
    .beep_analyze_done_enable = APP_OCXO_CAL_BEEP_ANALYZE_DONE_DEFAULT_ENABLE,
    .beep_demod_start_enable = APP_OCXO_CAL_BEEP_DEMOD_START_DEFAULT_ENABLE,
    .mixed_retry_count = APP_OCXO_CAL_MIXED_RETRY_DEFAULT_COUNT,
    .ask_square_dc_shift = APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_DEFAULT,
    .ask_square_threshold_code = APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_DEFAULT,
    .dds_offset_hz = APP_OCXO_CAL_DDS_OFFSET_DEFAULT_HZ,
    .dds_offset_step_hz = APP_OCXO_CAL_DDS_OFFSET_STEP_MID_HZ,
    .dds_offset_running = 0U
};
static uint32_t g_ocxo_saved_mv = APP_OCXO_CAL_DEFAULT_MV;
static app_ocxo_cal_flash_record_v9_t g_ocxo_flash_record;
static uint8_t g_ocxo_initialized = 0U;

static uint32_t app_ocxo_cal_limit_mv(uint32_t mv);
static uint32_t app_ocxo_cal_mv_to_code(uint32_t mv);
static void app_ocxo_cal_apply_dac(uint32_t mv);
static uint32_t app_ocxo_cal_crc_bytes(const void *record, uint32_t crc_offset);
static uint8_t app_ocxo_cal_decode_record(const app_ocxo_cal_flash_record_v9_t *record,
                                          uint32_t *dac_mv,
                                          uint8_t *auto_task_enable,
                                          uint8_t *boot_anim_enable,
                                          uint8_t *runtime_monitor_enable,
                                          uint8_t *ask_analog_demod_enable,
                                          uint8_t *fsk_analog_demod_enable,
                                          uint8_t *beep_ui_enable,
                                          uint8_t *beep_sweep_lock_enable,
                                          uint8_t *beep_analyze_done_enable,
                                          uint8_t *beep_demod_start_enable,
                                          uint8_t *mixed_retry_count,
                                          uint8_t *ask_square_dc_shift,
                                          uint16_t *ask_square_threshold_code,
                                          int32_t *dds_offset_hz,
                                          uint8_t *background_enable);
static uint8_t app_ocxo_cal_write_flash(uint8_t update_ocxo_state);
static void app_ocxo_cal_log(const char *text, uint32_t value);
static void app_ocxo_cal_log_i32(const char *text, int32_t value);
static int32_t app_ocxo_cal_limit_dds_offset_hz(int32_t offset_hz);
static void app_ocxo_cal_apply_dds_offset(void);

/* 把用户输入电压限制在安全调节范围内。 */
static uint32_t app_ocxo_cal_limit_mv(uint32_t mv)
{
    if (mv < APP_OCXO_CAL_MIN_MV)
    {
        return APP_OCXO_CAL_MIN_MV;
    }

    if (mv > APP_OCXO_CAL_MAX_MV)
    {
        return APP_OCXO_CAL_MAX_MV;
    }

    return mv;
}

/* 将 mV 转成 12 位 DAC 码值。 */
/* 限制 AD9959 全域频偏范围，避免保存异常值后影响所有 DDS 输出。 */
static int32_t app_ocxo_cal_limit_dds_offset_hz(int32_t offset_hz)
{
    if (offset_hz < APP_OCXO_CAL_DDS_OFFSET_MIN_HZ)
    {
        return APP_OCXO_CAL_DDS_OFFSET_MIN_HZ;
    }

    if (offset_hz > APP_OCXO_CAL_DDS_OFFSET_MAX_HZ)
    {
        return APP_OCXO_CAL_DDS_OFFSET_MAX_HZ;
    }

    return offset_hz;
}

/* 把当前全域频偏推送到 DDS 控制层，并要求重新 Apply 已缓存频率。 */
static void app_ocxo_cal_apply_dds_offset(void)
{
    AppDdsCmd cmd;
    const AppDdsStatus *dds_status;

    AppDDS_SetGlobalFreqOffsetHz(g_ocxo_status.dds_offset_hz);
    dds_status = AppDDS_GetStatus();
    if ((dds_status == NULL) || (dds_status->hw_ready == 0U))
    {
        return;
    }

    cmd = AppDDS_MakeApplyCmd();
    (void)AppDDS_DispatchCmd(&cmd);
}

static uint32_t app_ocxo_cal_mv_to_code(uint32_t mv)
{
    if (mv >= APP_OCXO_CAL_DAC_VREF_MV)
    {
        return APP_OCXO_CAL_DAC_MAX_CODE;
    }

    return (mv * APP_OCXO_CAL_DAC_MAX_CODE + (APP_OCXO_CAL_DAC_VREF_MV / 2U)) / APP_OCXO_CAL_DAC_VREF_MV;
}

/* 把当前 OCXO 控制电压输出到 PA4/DAC1_OUT1。 */
static void app_ocxo_cal_apply_dac(uint32_t mv)
{
    uint32_t dac_code = app_ocxo_cal_mv_to_code(mv);

    (void)HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
    (void)HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, dac_code);
}

/* 计算 Flash 记录 CRC，只覆盖 crc 字段之前的数据，便于兼容旧记录。 */
static uint32_t app_ocxo_cal_crc_bytes(const void *record, uint32_t crc_offset)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t crc = APP_OCXO_CAL_FLASH_CRC_SEED;
    uint32_t idx;

    if (record == NULL)
    {
        return 0UL;
    }

    for (idx = 0UL; idx < crc_offset; idx++)
    {
        crc ^= (uint32_t)bytes[idx];
        crc *= 16777619UL;
    }

    return crc;
}

/* 解码 OCXO Flash 记录；V1 只含电压，自动任务开关使用默认值。 */
static uint8_t app_ocxo_cal_decode_record(const app_ocxo_cal_flash_record_v9_t *record,
                                          uint32_t *dac_mv,
                                          uint8_t *auto_task_enable,
                                          uint8_t *boot_anim_enable,
                                          uint8_t *runtime_monitor_enable,
                                          uint8_t *ask_analog_demod_enable,
                                          uint8_t *fsk_analog_demod_enable,
                                          uint8_t *beep_ui_enable,
                                          uint8_t *beep_sweep_lock_enable,
                                          uint8_t *beep_analyze_done_enable,
                                          uint8_t *beep_demod_start_enable,
                                          uint8_t *mixed_retry_count,
                                          uint8_t *ask_square_dc_shift,
                                          uint16_t *ask_square_threshold_code,
                                          int32_t *dds_offset_hz,
                                          uint8_t *background_enable)
{
    const app_ocxo_cal_flash_record_v1_t *record_v1;
    const app_ocxo_cal_flash_record_v2_t *record_v2;
    const app_ocxo_cal_flash_record_v3_t *record_v3;
    const app_ocxo_cal_flash_record_v4_t *record_v4;
    const app_ocxo_cal_flash_record_v5_t *record_v5;
    const app_ocxo_cal_flash_record_v6_t *record_v6;
    const app_ocxo_cal_flash_record_v7_t *record_v7;
    const app_ocxo_cal_flash_record_v8_t *record_v8;

    if (record == NULL)
    {
        return 0U;
    }

    if (record->magic != APP_OCXO_CAL_FLASH_MAGIC)
    {
        return 0U;
    }

    if (auto_task_enable != NULL)
    {
        *auto_task_enable = APP_OCXO_CAL_AUTO_TASK_DEFAULT_ENABLE;
    }
    if (boot_anim_enable != NULL)
    {
        *boot_anim_enable = APP_OCXO_CAL_BOOT_ANIM_DEFAULT_ENABLE;
    }
    if (runtime_monitor_enable != NULL)
    {
        *runtime_monitor_enable = APP_OCXO_CAL_RUNTIME_MONITOR_DEFAULT_ENABLE;
    }
    if (ask_analog_demod_enable != NULL)
    {
        *ask_analog_demod_enable = APP_OCXO_CAL_ASK_ANALOG_DEMOD_DEFAULT_ENABLE;
    }
    if (fsk_analog_demod_enable != NULL)
    {
        *fsk_analog_demod_enable = APP_OCXO_CAL_FSK_ANALOG_DEMOD_DEFAULT_ENABLE;
    }
    if (beep_ui_enable != NULL)
    {
        *beep_ui_enable = APP_OCXO_CAL_BEEP_UI_DEFAULT_ENABLE;
    }
    if (beep_sweep_lock_enable != NULL)
    {
        *beep_sweep_lock_enable = APP_OCXO_CAL_BEEP_SWEEP_LOCK_DEFAULT_ENABLE;
    }
    if (beep_analyze_done_enable != NULL)
    {
        *beep_analyze_done_enable = APP_OCXO_CAL_BEEP_ANALYZE_DONE_DEFAULT_ENABLE;
    }
    if (beep_demod_start_enable != NULL)
    {
        *beep_demod_start_enable = APP_OCXO_CAL_BEEP_DEMOD_START_DEFAULT_ENABLE;
    }
    if (mixed_retry_count != NULL)
    {
        *mixed_retry_count = APP_OCXO_CAL_MIXED_RETRY_DEFAULT_COUNT;
    }
    if (ask_square_dc_shift != NULL)
    {
        *ask_square_dc_shift = APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_DEFAULT;
    }
    if (ask_square_threshold_code != NULL)
    {
        *ask_square_threshold_code = APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_DEFAULT;
    }
    if (dds_offset_hz != NULL)
    {
        *dds_offset_hz = APP_OCXO_CAL_DDS_OFFSET_DEFAULT_HZ;
    }
    if (background_enable != NULL)
    {
        *background_enable = APP_OCXO_CAL_BACKGROUND_DEFAULT_ENABLE;
    }

    record_v1 = (const app_ocxo_cal_flash_record_v1_t *)record;
    if ((record_v1->version == 1UL) &&
        (record_v1->record_size == sizeof(app_ocxo_cal_flash_record_v1_t)) &&
        (record_v1->dac_mv >= APP_OCXO_CAL_MIN_MV) &&
        (record_v1->dac_mv <= APP_OCXO_CAL_MAX_MV) &&
        (record_v1->crc == app_ocxo_cal_crc_bytes(record_v1, offsetof(app_ocxo_cal_flash_record_v1_t, crc))))
    {
        if (dac_mv != NULL)
        {
            *dac_mv = record_v1->dac_mv;
        }

        if (auto_task_enable != NULL)
        {
            *auto_task_enable = APP_OCXO_CAL_AUTO_TASK_DEFAULT_ENABLE;
        }

        if (boot_anim_enable != NULL)
        {
            *boot_anim_enable = APP_OCXO_CAL_BOOT_ANIM_DEFAULT_ENABLE;
        }

        if (runtime_monitor_enable != NULL)
        {
            *runtime_monitor_enable = APP_OCXO_CAL_RUNTIME_MONITOR_DEFAULT_ENABLE;
        }

        return 1U;
    }

    record_v2 = (const app_ocxo_cal_flash_record_v2_t *)record;
    if ((record_v2->version == 2UL) &&
        (record_v2->record_size == sizeof(app_ocxo_cal_flash_record_v2_t)) &&
        (record_v2->dac_mv >= APP_OCXO_CAL_MIN_MV) &&
        (record_v2->dac_mv <= APP_OCXO_CAL_MAX_MV) &&
        (record_v2->crc == app_ocxo_cal_crc_bytes(record_v2, offsetof(app_ocxo_cal_flash_record_v2_t, crc))))
    {
        if (dac_mv != NULL)
        {
            *dac_mv = record_v2->dac_mv;
        }

        if (auto_task_enable != NULL)
        {
            *auto_task_enable = (record_v2->auto_task_enable != 0UL) ? 1U : 0U;
        }

        if (boot_anim_enable != NULL)
        {
            *boot_anim_enable = APP_OCXO_CAL_BOOT_ANIM_DEFAULT_ENABLE;
        }

        if (runtime_monitor_enable != NULL)
        {
            *runtime_monitor_enable = APP_OCXO_CAL_RUNTIME_MONITOR_DEFAULT_ENABLE;
        }

        return 1U;
    }

    record_v3 = (const app_ocxo_cal_flash_record_v3_t *)record;
    if ((record_v3->version == 3UL) &&
        (record_v3->record_size == sizeof(app_ocxo_cal_flash_record_v3_t)) &&
        (record_v3->dac_mv >= APP_OCXO_CAL_MIN_MV) &&
        (record_v3->dac_mv <= APP_OCXO_CAL_MAX_MV) &&
        (record_v3->crc == app_ocxo_cal_crc_bytes(record_v3, offsetof(app_ocxo_cal_flash_record_v3_t, crc))))
    {
        if (dac_mv != NULL)
        {
            *dac_mv = record_v3->dac_mv;
        }

        if (auto_task_enable != NULL)
        {
            *auto_task_enable = (record_v3->auto_task_enable != 0UL) ? 1U : 0U;
        }

        if (boot_anim_enable != NULL)
        {
            *boot_anim_enable = (record_v3->boot_anim_enable != 0UL) ? 1U : 0U;
        }

        if (runtime_monitor_enable != NULL)
        {
            *runtime_monitor_enable = APP_OCXO_CAL_RUNTIME_MONITOR_DEFAULT_ENABLE;
        }

        return 1U;
    }

    record_v4 = (const app_ocxo_cal_flash_record_v4_t *)record;
    if ((record_v4->version == 4UL) &&
        (record_v4->record_size == sizeof(app_ocxo_cal_flash_record_v4_t)) &&
        (record_v4->dac_mv >= APP_OCXO_CAL_MIN_MV) &&
        (record_v4->dac_mv <= APP_OCXO_CAL_MAX_MV) &&
        (record_v4->crc == app_ocxo_cal_crc_bytes(record_v4, offsetof(app_ocxo_cal_flash_record_v4_t, crc))))
    {
        if (dac_mv != NULL)
        {
            *dac_mv = record_v4->dac_mv;
        }

        if (auto_task_enable != NULL)
        {
            *auto_task_enable = (record_v4->auto_task_enable != 0UL) ? 1U : 0U;
        }

        if (boot_anim_enable != NULL)
        {
            *boot_anim_enable = (record_v4->boot_anim_enable != 0UL) ? 1U : 0U;
        }

        if (runtime_monitor_enable != NULL)
        {
            *runtime_monitor_enable = (record_v4->runtime_monitor_enable != 0UL) ? 1U : 0U;
        }

        return 1U;
    }

    record_v5 = (const app_ocxo_cal_flash_record_v5_t *)record;
    if ((record_v5->version == 5UL) &&
        (record_v5->record_size == sizeof(app_ocxo_cal_flash_record_v5_t)) &&
        (record_v5->dac_mv >= APP_OCXO_CAL_MIN_MV) &&
        (record_v5->dac_mv <= APP_OCXO_CAL_MAX_MV) &&
        (record_v5->crc == app_ocxo_cal_crc_bytes(record_v5, offsetof(app_ocxo_cal_flash_record_v5_t, crc))))
    {
        if (dac_mv != NULL)
        {
            *dac_mv = record_v5->dac_mv;
        }

        if (auto_task_enable != NULL)
        {
            *auto_task_enable = (record_v5->auto_task_enable != 0UL) ? 1U : 0U;
        }

        if (boot_anim_enable != NULL)
        {
            *boot_anim_enable = (record_v5->boot_anim_enable != 0UL) ? 1U : 0U;
        }

        if (runtime_monitor_enable != NULL)
        {
            *runtime_monitor_enable = (record_v5->runtime_monitor_enable != 0UL) ? 1U : 0U;
        }

        if (ask_analog_demod_enable != NULL)
        {
            *ask_analog_demod_enable = (record_v5->ask_analog_demod_enable != 0UL) ? 1U : 0U;
        }

        if (fsk_analog_demod_enable != NULL)
        {
            *fsk_analog_demod_enable = (record_v5->fsk_analog_demod_enable != 0UL) ? 1U : 0U;
        }

        if (beep_ui_enable != NULL)
        {
            *beep_ui_enable = (record_v5->beep_ui_enable != 0UL) ? 1U : 0U;
        }

        if (beep_sweep_lock_enable != NULL)
        {
            *beep_sweep_lock_enable = (record_v5->beep_sweep_lock_enable != 0UL) ? 1U : 0U;
        }

        if (beep_analyze_done_enable != NULL)
        {
            *beep_analyze_done_enable = (record_v5->beep_analyze_done_enable != 0UL) ? 1U : 0U;
        }

        if (beep_demod_start_enable != NULL)
        {
            *beep_demod_start_enable = (record_v5->beep_demod_start_enable != 0UL) ? 1U : 0U;
        }

        return 1U;
    }

    record_v6 = (const app_ocxo_cal_flash_record_v6_t *)record;
    if ((record_v6->version == 6UL) &&
        (record_v6->record_size == sizeof(app_ocxo_cal_flash_record_v6_t)) &&
        (record_v6->dac_mv >= APP_OCXO_CAL_MIN_MV) &&
        (record_v6->dac_mv <= APP_OCXO_CAL_MAX_MV) &&
        (record_v6->crc == app_ocxo_cal_crc_bytes(record_v6, offsetof(app_ocxo_cal_flash_record_v6_t, crc))))
    {
        if (dac_mv != NULL)
        {
            *dac_mv = record_v6->dac_mv;
        }

        if (auto_task_enable != NULL)
        {
            *auto_task_enable = (record_v6->auto_task_enable != 0UL) ? 1U : 0U;
        }

        if (boot_anim_enable != NULL)
        {
            *boot_anim_enable = (record_v6->boot_anim_enable != 0UL) ? 1U : 0U;
        }

        if (runtime_monitor_enable != NULL)
        {
            *runtime_monitor_enable = (record_v6->runtime_monitor_enable != 0UL) ? 1U : 0U;
        }

        if (ask_analog_demod_enable != NULL)
        {
            *ask_analog_demod_enable = (record_v6->ask_analog_demod_enable != 0UL) ? 1U : 0U;
        }

        if (fsk_analog_demod_enable != NULL)
        {
            *fsk_analog_demod_enable = (record_v6->fsk_analog_demod_enable != 0UL) ? 1U : 0U;
        }

        if (beep_ui_enable != NULL)
        {
            *beep_ui_enable = (record_v6->beep_ui_enable != 0UL) ? 1U : 0U;
        }

        if (beep_sweep_lock_enable != NULL)
        {
            *beep_sweep_lock_enable = (record_v6->beep_sweep_lock_enable != 0UL) ? 1U : 0U;
        }

        if (beep_analyze_done_enable != NULL)
        {
            *beep_analyze_done_enable = (record_v6->beep_analyze_done_enable != 0UL) ? 1U : 0U;
        }

        if (beep_demod_start_enable != NULL)
        {
            *beep_demod_start_enable = (record_v6->beep_demod_start_enable != 0UL) ? 1U : 0U;
        }

        if (mixed_retry_count != NULL)
        {
            if (record_v6->mixed_retry_count == APP_OCXO_CAL_MIXED_RETRY_INFINITE)
            {
                *mixed_retry_count = APP_OCXO_CAL_MIXED_RETRY_INFINITE;
            }
            else if (record_v6->mixed_retry_count > APP_OCXO_CAL_MIXED_RETRY_MAX_COUNT)
            {
                *mixed_retry_count = APP_OCXO_CAL_MIXED_RETRY_DEFAULT_COUNT;
            }
            else
            {
                *mixed_retry_count = (uint8_t)record_v6->mixed_retry_count;
            }
        }

        return 1U;
    }

    record_v7 = (const app_ocxo_cal_flash_record_v7_t *)record;
    if ((record_v7->version == 7UL) &&
        (record_v7->record_size == sizeof(app_ocxo_cal_flash_record_v7_t)) &&
        (record_v7->dac_mv >= APP_OCXO_CAL_MIN_MV) &&
        (record_v7->dac_mv <= APP_OCXO_CAL_MAX_MV) &&
        (record_v7->crc == app_ocxo_cal_crc_bytes(record_v7, offsetof(app_ocxo_cal_flash_record_v7_t, crc))))
    {
        if (dac_mv != NULL)
        {
            *dac_mv = record_v7->dac_mv;
        }

        if (auto_task_enable != NULL)
        {
            *auto_task_enable = (record_v7->auto_task_enable != 0UL) ? 1U : 0U;
        }

        if (boot_anim_enable != NULL)
        {
            *boot_anim_enable = (record_v7->boot_anim_enable != 0UL) ? 1U : 0U;
        }

        if (runtime_monitor_enable != NULL)
        {
            *runtime_monitor_enable = (record_v7->runtime_monitor_enable != 0UL) ? 1U : 0U;
        }

        if (ask_analog_demod_enable != NULL)
        {
            *ask_analog_demod_enable = (record_v7->ask_analog_demod_enable != 0UL) ? 1U : 0U;
        }

        if (fsk_analog_demod_enable != NULL)
        {
            *fsk_analog_demod_enable = (record_v7->fsk_analog_demod_enable != 0UL) ? 1U : 0U;
        }

        if (beep_ui_enable != NULL)
        {
            *beep_ui_enable = (record_v7->beep_ui_enable != 0UL) ? 1U : 0U;
        }

        if (beep_sweep_lock_enable != NULL)
        {
            *beep_sweep_lock_enable = (record_v7->beep_sweep_lock_enable != 0UL) ? 1U : 0U;
        }

        if (beep_analyze_done_enable != NULL)
        {
            *beep_analyze_done_enable = (record_v7->beep_analyze_done_enable != 0UL) ? 1U : 0U;
        }

        if (beep_demod_start_enable != NULL)
        {
            *beep_demod_start_enable = (record_v7->beep_demod_start_enable != 0UL) ? 1U : 0U;
        }

        if (mixed_retry_count != NULL)
        {
            if (record_v7->mixed_retry_count == APP_OCXO_CAL_MIXED_RETRY_INFINITE)
            {
                *mixed_retry_count = APP_OCXO_CAL_MIXED_RETRY_INFINITE;
            }
            else if (record_v7->mixed_retry_count > APP_OCXO_CAL_MIXED_RETRY_MAX_COUNT)
            {
                *mixed_retry_count = APP_OCXO_CAL_MIXED_RETRY_DEFAULT_COUNT;
            }
            else
            {
                *mixed_retry_count = (uint8_t)record_v7->mixed_retry_count;
            }
        }

        if (ask_square_dc_shift != NULL)
        {
            if ((record_v7->ask_square_dc_shift < APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_MIN) ||
                (record_v7->ask_square_dc_shift > APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_MAX))
            {
                *ask_square_dc_shift = APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_DEFAULT;
            }
            else
            {
                *ask_square_dc_shift = (uint8_t)record_v7->ask_square_dc_shift;
            }
        }

        if (ask_square_threshold_code != NULL)
        {
            if ((record_v7->ask_square_threshold_code < APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_MIN) ||
                (record_v7->ask_square_threshold_code > APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_MAX))
            {
                *ask_square_threshold_code = APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_DEFAULT;
            }
            else
            {
                *ask_square_threshold_code = (uint16_t)record_v7->ask_square_threshold_code;
            }
        }

        return 1U;
    }

    record_v8 = (const app_ocxo_cal_flash_record_v8_t *)record;
    if ((record_v8->version == 8UL) &&
        (record_v8->record_size == sizeof(app_ocxo_cal_flash_record_v8_t)) &&
        (record_v8->dac_mv >= APP_OCXO_CAL_MIN_MV) &&
        (record_v8->dac_mv <= APP_OCXO_CAL_MAX_MV) &&
        (record_v8->dds_offset_hz >= APP_OCXO_CAL_DDS_OFFSET_MIN_HZ) &&
        (record_v8->dds_offset_hz <= APP_OCXO_CAL_DDS_OFFSET_MAX_HZ) &&
        (record_v8->crc == app_ocxo_cal_crc_bytes(record_v8, offsetof(app_ocxo_cal_flash_record_v8_t, crc))))
    {
        if (dac_mv != NULL) { *dac_mv = record_v8->dac_mv; }
        if (auto_task_enable != NULL) { *auto_task_enable = (record_v8->auto_task_enable != 0UL) ? 1U : 0U; }
        if (boot_anim_enable != NULL) { *boot_anim_enable = (record_v8->boot_anim_enable != 0UL) ? 1U : 0U; }
        if (runtime_monitor_enable != NULL) { *runtime_monitor_enable = (record_v8->runtime_monitor_enable != 0UL) ? 1U : 0U; }
        if (ask_analog_demod_enable != NULL) { *ask_analog_demod_enable = (record_v8->ask_analog_demod_enable != 0UL) ? 1U : 0U; }
        if (fsk_analog_demod_enable != NULL) { *fsk_analog_demod_enable = (record_v8->fsk_analog_demod_enable != 0UL) ? 1U : 0U; }
        if (beep_ui_enable != NULL) { *beep_ui_enable = (record_v8->beep_ui_enable != 0UL) ? 1U : 0U; }
        if (beep_sweep_lock_enable != NULL) { *beep_sweep_lock_enable = (record_v8->beep_sweep_lock_enable != 0UL) ? 1U : 0U; }
        if (beep_analyze_done_enable != NULL) { *beep_analyze_done_enable = (record_v8->beep_analyze_done_enable != 0UL) ? 1U : 0U; }
        if (beep_demod_start_enable != NULL) { *beep_demod_start_enable = (record_v8->beep_demod_start_enable != 0UL) ? 1U : 0U; }
        if (mixed_retry_count != NULL)
        {
            *mixed_retry_count = ((record_v8->mixed_retry_count == APP_OCXO_CAL_MIXED_RETRY_INFINITE) ?
                                  APP_OCXO_CAL_MIXED_RETRY_INFINITE :
                                  ((record_v8->mixed_retry_count <= APP_OCXO_CAL_MIXED_RETRY_MAX_COUNT) ?
                                   (uint8_t)record_v8->mixed_retry_count : APP_OCXO_CAL_MIXED_RETRY_DEFAULT_COUNT));
        }
        if (ask_square_dc_shift != NULL)
        {
            *ask_square_dc_shift = ((record_v8->ask_square_dc_shift >= APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_MIN) &&
                                    (record_v8->ask_square_dc_shift <= APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_MAX)) ?
                                   (uint8_t)record_v8->ask_square_dc_shift : APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_DEFAULT;
        }
        if (ask_square_threshold_code != NULL)
        {
            *ask_square_threshold_code = ((record_v8->ask_square_threshold_code >= APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_MIN) &&
                                          (record_v8->ask_square_threshold_code <= APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_MAX)) ?
                                         (uint16_t)record_v8->ask_square_threshold_code : APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_DEFAULT;
        }
        if (dds_offset_hz != NULL) { *dds_offset_hz = app_ocxo_cal_limit_dds_offset_hz(record_v8->dds_offset_hz); }
        return 1U;
    }

    if ((record->version != APP_OCXO_CAL_FLASH_VERSION) ||
        (record->record_size != sizeof(app_ocxo_cal_flash_record_v9_t)) ||
        (record->dac_mv < APP_OCXO_CAL_MIN_MV) ||
        (record->dac_mv > APP_OCXO_CAL_MAX_MV) ||
        (record->dds_offset_hz < APP_OCXO_CAL_DDS_OFFSET_MIN_HZ) ||
        (record->dds_offset_hz > APP_OCXO_CAL_DDS_OFFSET_MAX_HZ) ||
        (record->crc != app_ocxo_cal_crc_bytes(record, offsetof(app_ocxo_cal_flash_record_v9_t, crc))))
    {
        return 0U;
    }

    if (dac_mv != NULL) { *dac_mv = record->dac_mv; }
    if (auto_task_enable != NULL) { *auto_task_enable = (record->auto_task_enable != 0UL) ? 1U : 0U; }
    if (boot_anim_enable != NULL) { *boot_anim_enable = (record->boot_anim_enable != 0UL) ? 1U : 0U; }
    if (runtime_monitor_enable != NULL) { *runtime_monitor_enable = (record->runtime_monitor_enable != 0UL) ? 1U : 0U; }
    if (ask_analog_demod_enable != NULL) { *ask_analog_demod_enable = (record->ask_analog_demod_enable != 0UL) ? 1U : 0U; }
    if (fsk_analog_demod_enable != NULL) { *fsk_analog_demod_enable = (record->fsk_analog_demod_enable != 0UL) ? 1U : 0U; }
    if (beep_ui_enable != NULL) { *beep_ui_enable = (record->beep_ui_enable != 0UL) ? 1U : 0U; }
    if (beep_sweep_lock_enable != NULL) { *beep_sweep_lock_enable = (record->beep_sweep_lock_enable != 0UL) ? 1U : 0U; }
    if (beep_analyze_done_enable != NULL) { *beep_analyze_done_enable = (record->beep_analyze_done_enable != 0UL) ? 1U : 0U; }
    if (beep_demod_start_enable != NULL) { *beep_demod_start_enable = (record->beep_demod_start_enable != 0UL) ? 1U : 0U; }
    if (mixed_retry_count != NULL)
    {
        *mixed_retry_count = ((record->mixed_retry_count == APP_OCXO_CAL_MIXED_RETRY_INFINITE) ?
                              APP_OCXO_CAL_MIXED_RETRY_INFINITE :
                              ((record->mixed_retry_count <= APP_OCXO_CAL_MIXED_RETRY_MAX_COUNT) ?
                               (uint8_t)record->mixed_retry_count : APP_OCXO_CAL_MIXED_RETRY_DEFAULT_COUNT));
    }
    if (ask_square_dc_shift != NULL)
    {
        *ask_square_dc_shift = ((record->ask_square_dc_shift >= APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_MIN) &&
                                (record->ask_square_dc_shift <= APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_MAX)) ?
                               (uint8_t)record->ask_square_dc_shift : APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_DEFAULT;
    }
    if (ask_square_threshold_code != NULL)
    {
        *ask_square_threshold_code = ((record->ask_square_threshold_code >= APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_MIN) &&
                                      (record->ask_square_threshold_code <= APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_MAX)) ?
                                     (uint16_t)record->ask_square_threshold_code : APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_DEFAULT;
    }
    if (dds_offset_hz != NULL) { *dds_offset_hz = app_ocxo_cal_limit_dds_offset_hz(record->dds_offset_hz); }
    if (background_enable != NULL) { *background_enable = (record->background_enable != 0UL) ? 1U : 0U; }

    return 1U;
}

/* 写入 OCXO 设置记录；自动任务开关和电压共用一个扇区。 */
static uint8_t app_ocxo_cal_write_flash(uint8_t update_ocxo_state)
{
    app_board_flash_result_t result;
    app_ocxo_cal_state_t old_state = g_ocxo_status.state;
    uint32_t record_dac_mv = (update_ocxo_state != 0U) ? g_ocxo_status.dac_mv : g_ocxo_saved_mv;

    record_dac_mv = app_ocxo_cal_limit_mv(record_dac_mv);

    if (update_ocxo_state != 0U)
    {
        g_ocxo_status.state = APP_OCXO_CAL_SAVING;
    }

    memset(&g_ocxo_flash_record, 0, sizeof(g_ocxo_flash_record));
    g_ocxo_flash_record.magic = APP_OCXO_CAL_FLASH_MAGIC;
    g_ocxo_flash_record.version = APP_OCXO_CAL_FLASH_VERSION;
    g_ocxo_flash_record.record_size = sizeof(g_ocxo_flash_record);
    g_ocxo_flash_record.dac_mv = record_dac_mv;
    g_ocxo_flash_record.auto_task_enable = (g_ocxo_status.auto_task_enable != 0U) ? 1UL : 0UL;
    g_ocxo_flash_record.boot_anim_enable = (g_ocxo_status.boot_anim_enable != 0U) ? 1UL : 0UL;
    g_ocxo_flash_record.runtime_monitor_enable = (g_ocxo_status.runtime_monitor_enable != 0U) ? 1UL : 0UL;
    g_ocxo_flash_record.ask_analog_demod_enable = (g_ocxo_status.ask_analog_demod_enable != 0U) ? 1UL : 0UL;
    g_ocxo_flash_record.fsk_analog_demod_enable = (g_ocxo_status.fsk_analog_demod_enable != 0U) ? 1UL : 0UL;
    g_ocxo_flash_record.beep_ui_enable = (g_ocxo_status.beep_ui_enable != 0U) ? 1UL : 0UL;
    g_ocxo_flash_record.beep_sweep_lock_enable = (g_ocxo_status.beep_sweep_lock_enable != 0U) ? 1UL : 0UL;
    g_ocxo_flash_record.beep_analyze_done_enable = (g_ocxo_status.beep_analyze_done_enable != 0U) ? 1UL : 0UL;
    g_ocxo_flash_record.beep_demod_start_enable = (g_ocxo_status.beep_demod_start_enable != 0U) ? 1UL : 0UL;
    g_ocxo_flash_record.mixed_retry_count = (uint32_t)g_ocxo_status.mixed_retry_count;
    g_ocxo_flash_record.ask_square_dc_shift = (uint32_t)g_ocxo_status.ask_square_dc_shift;
    g_ocxo_flash_record.ask_square_threshold_code = (uint32_t)g_ocxo_status.ask_square_threshold_code;
    g_ocxo_flash_record.dds_offset_hz = app_ocxo_cal_limit_dds_offset_hz(g_ocxo_status.dds_offset_hz);
    g_ocxo_flash_record.background_enable = (g_ocxo_status.background_enable != 0U) ? 1UL : 0UL;
    g_ocxo_flash_record.crc = app_ocxo_cal_crc_bytes(&g_ocxo_flash_record,
                                                     offsetof(app_ocxo_cal_flash_record_v9_t, crc));

    result = app_board_flash_erase_4k(APP_OCXO_CAL_FLASH_ADDR);
    if (result != APP_BOARD_FLASH_OK)
    {
        if (update_ocxo_state != 0U)
        {
            g_ocxo_status.state = APP_OCXO_CAL_ERROR;
        }
        else
        {
            g_ocxo_status.state = old_state;
        }
        return 0U;
    }

    result = app_board_flash_write(APP_OCXO_CAL_FLASH_ADDR,
                                   (const uint8_t *)&g_ocxo_flash_record,
                                   sizeof(g_ocxo_flash_record));
    if (result != APP_BOARD_FLASH_OK)
    {
        if (update_ocxo_state != 0U)
        {
            g_ocxo_status.state = APP_OCXO_CAL_ERROR;
        }
        else
        {
            g_ocxo_status.state = old_state;
        }
        return 0U;
    }

    memset(&g_ocxo_flash_record, 0, sizeof(g_ocxo_flash_record));
    result = app_board_flash_read(APP_OCXO_CAL_FLASH_ADDR,
                                  (uint8_t *)&g_ocxo_flash_record,
                                  sizeof(g_ocxo_flash_record));
    if ((result != APP_BOARD_FLASH_OK) ||
        (app_ocxo_cal_decode_record(&g_ocxo_flash_record, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) == 0U))
    {
        if (update_ocxo_state != 0U)
        {
            g_ocxo_status.state = APP_OCXO_CAL_ERROR;
        }
        else
        {
            g_ocxo_status.state = old_state;
        }
        return 0U;
    }

    g_ocxo_status.valid = 1U;
    g_ocxo_status.flash_loaded = 0U;
    if (update_ocxo_state != 0U)
    {
        g_ocxo_saved_mv = g_ocxo_status.dac_mv;
    }
    else if (g_ocxo_status.state == APP_OCXO_CAL_NONE)
    {
        g_ocxo_saved_mv = record_dac_mv;
    }

    if (update_ocxo_state != 0U)
    {
        g_ocxo_status.state = APP_OCXO_CAL_SAVE_OK;
        app_ocxo_cal_apply_dac(g_ocxo_status.dac_mv);
    }
    else
    {
        g_ocxo_status.state = old_state;
    }

    return 1U;
}

static void app_ocxo_cal_log(const char *text, uint32_t value)
{
    char log_buf[80];
    int n;

    if (text == NULL)
    {
        return;
    }

    n = snprintf(log_buf, sizeof(log_buf), "ocxo:%s,%lu\r\n", text, (unsigned long)value);
    if ((n > 0) && ((size_t)n < sizeof(log_buf)))
    {
        print_queue_send(log_buf);
    }
}

static void app_ocxo_cal_log_i32(const char *text, int32_t value)
{
    char log_buf[80];
    int n;

    if (text == NULL)
    {
        return;
    }

    n = snprintf(log_buf, sizeof(log_buf), "ocxo:%s,%ld\r\n", text, (long)value);
    if ((n > 0) && ((size_t)n < sizeof(log_buf)))
    {
        print_queue_send(log_buf);
    }
}

/* 初始化 OCXO 电压状态，并先输出默认电压。 */
void app_ocxo_cal_init(void)
{
    if (g_ocxo_initialized != 0U)
    {
        return;
    }

    memset(&g_ocxo_flash_record, 0, sizeof(g_ocxo_flash_record));
    g_ocxo_status.dac_mv = APP_OCXO_CAL_DEFAULT_MV;
    g_ocxo_status.step_mv = APP_OCXO_CAL_STEP_MID_MV;
    g_ocxo_status.state = APP_OCXO_CAL_NONE;
    g_ocxo_status.valid = 0U;
    g_ocxo_status.flash_loaded = 0U;
    g_ocxo_status.auto_task_enable = APP_OCXO_CAL_AUTO_TASK_DEFAULT_ENABLE;
    g_ocxo_status.boot_anim_enable = APP_OCXO_CAL_BOOT_ANIM_DEFAULT_ENABLE;
    g_ocxo_status.background_enable = APP_OCXO_CAL_BACKGROUND_DEFAULT_ENABLE;
    g_ocxo_status.runtime_monitor_enable = APP_OCXO_CAL_RUNTIME_MONITOR_DEFAULT_ENABLE;
    g_ocxo_status.ask_analog_demod_enable = APP_OCXO_CAL_ASK_ANALOG_DEMOD_DEFAULT_ENABLE;
    g_ocxo_status.fsk_analog_demod_enable = APP_OCXO_CAL_FSK_ANALOG_DEMOD_DEFAULT_ENABLE;
    g_ocxo_status.beep_ui_enable = APP_OCXO_CAL_BEEP_UI_DEFAULT_ENABLE;
    g_ocxo_status.beep_sweep_lock_enable = APP_OCXO_CAL_BEEP_SWEEP_LOCK_DEFAULT_ENABLE;
    g_ocxo_status.beep_analyze_done_enable = APP_OCXO_CAL_BEEP_ANALYZE_DONE_DEFAULT_ENABLE;
    g_ocxo_status.beep_demod_start_enable = APP_OCXO_CAL_BEEP_DEMOD_START_DEFAULT_ENABLE;
    g_ocxo_status.mixed_retry_count = APP_OCXO_CAL_MIXED_RETRY_DEFAULT_COUNT;
    g_ocxo_status.ask_square_dc_shift = APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_DEFAULT;
    g_ocxo_status.ask_square_threshold_code = APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_DEFAULT;
    g_ocxo_status.dds_offset_hz = APP_OCXO_CAL_DDS_OFFSET_DEFAULT_HZ;
    g_ocxo_status.dds_offset_step_hz = APP_OCXO_CAL_DDS_OFFSET_STEP_MID_HZ;
    g_ocxo_status.dds_offset_running = 0U;
    g_ocxo_saved_mv = APP_OCXO_CAL_DEFAULT_MV;
    g_ocxo_initialized = 1U;
    app_ocxo_cal_apply_dac(g_ocxo_status.dac_mv);
    app_ocxo_cal_apply_dds_offset();
}

/* 从外部 Flash 读取上次保存的恒温晶振控制电压。 */
uint8_t app_ocxo_cal_load_from_flash(void)
{
    app_board_flash_result_t result;
    uint32_t dac_mv;
    uint8_t auto_task_enable;
    uint8_t boot_anim_enable;
    uint8_t background_enable;
    uint8_t runtime_monitor_enable;
    uint8_t ask_analog_demod_enable;
    uint8_t fsk_analog_demod_enable;
    uint8_t beep_ui_enable;
    uint8_t beep_sweep_lock_enable;
    uint8_t beep_analyze_done_enable;
    uint8_t beep_demod_start_enable;
    uint8_t mixed_retry_count;
    uint8_t ask_square_dc_shift;
    uint16_t ask_square_threshold_code;
    int32_t dds_offset_hz;

    if (g_ocxo_initialized == 0U)
    {
        app_ocxo_cal_init();
    }

    result = app_board_flash_read(APP_OCXO_CAL_FLASH_ADDR,
                                  (uint8_t *)&g_ocxo_flash_record,
                                  sizeof(g_ocxo_flash_record));
    if ((result != APP_BOARD_FLASH_OK) ||
        (app_ocxo_cal_decode_record(&g_ocxo_flash_record,
                                    &dac_mv,
                                    &auto_task_enable,
                                    &boot_anim_enable,
                                    &runtime_monitor_enable,
                                    &ask_analog_demod_enable,
                                    &fsk_analog_demod_enable,
                                    &beep_ui_enable,
                                    &beep_sweep_lock_enable,
                                    &beep_analyze_done_enable,
                                    &beep_demod_start_enable,
                                    &mixed_retry_count,
                                    &ask_square_dc_shift,
                                    &ask_square_threshold_code,
                                    &dds_offset_hz,
                                    &background_enable) == 0U))
    {
        g_ocxo_status.state = APP_OCXO_CAL_NONE;
        g_ocxo_status.valid = 0U;
        g_ocxo_status.flash_loaded = 0U;
        g_ocxo_status.auto_task_enable = APP_OCXO_CAL_AUTO_TASK_DEFAULT_ENABLE;
        g_ocxo_status.boot_anim_enable = APP_OCXO_CAL_BOOT_ANIM_DEFAULT_ENABLE;
        g_ocxo_status.runtime_monitor_enable = APP_OCXO_CAL_RUNTIME_MONITOR_DEFAULT_ENABLE;
        g_ocxo_status.ask_analog_demod_enable = APP_OCXO_CAL_ASK_ANALOG_DEMOD_DEFAULT_ENABLE;
        g_ocxo_status.fsk_analog_demod_enable = APP_OCXO_CAL_FSK_ANALOG_DEMOD_DEFAULT_ENABLE;
        g_ocxo_status.beep_ui_enable = APP_OCXO_CAL_BEEP_UI_DEFAULT_ENABLE;
        g_ocxo_status.beep_sweep_lock_enable = APP_OCXO_CAL_BEEP_SWEEP_LOCK_DEFAULT_ENABLE;
        g_ocxo_status.beep_analyze_done_enable = APP_OCXO_CAL_BEEP_ANALYZE_DONE_DEFAULT_ENABLE;
        g_ocxo_status.beep_demod_start_enable = APP_OCXO_CAL_BEEP_DEMOD_START_DEFAULT_ENABLE;
        g_ocxo_status.mixed_retry_count = APP_OCXO_CAL_MIXED_RETRY_DEFAULT_COUNT;
        g_ocxo_status.ask_square_dc_shift = APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_DEFAULT;
        g_ocxo_status.ask_square_threshold_code = APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_DEFAULT;
        g_ocxo_status.dds_offset_hz = APP_OCXO_CAL_DDS_OFFSET_DEFAULT_HZ;
        g_ocxo_status.dds_offset_step_hz = APP_OCXO_CAL_DDS_OFFSET_STEP_MID_HZ;
        g_ocxo_status.dds_offset_running = 0U;
        g_ocxo_saved_mv = APP_OCXO_CAL_DEFAULT_MV;
        app_ocxo_cal_apply_dac(g_ocxo_status.dac_mv);
        app_ocxo_cal_apply_dds_offset();
        return 0U;
    }

    g_ocxo_status.dac_mv = dac_mv;
    g_ocxo_status.auto_task_enable = auto_task_enable;
    g_ocxo_status.boot_anim_enable = boot_anim_enable;
    g_ocxo_status.background_enable = background_enable;
    g_ocxo_status.runtime_monitor_enable = runtime_monitor_enable;
    g_ocxo_status.ask_analog_demod_enable = ask_analog_demod_enable;
    g_ocxo_status.fsk_analog_demod_enable = fsk_analog_demod_enable;
    g_ocxo_status.beep_ui_enable = beep_ui_enable;
    g_ocxo_status.beep_sweep_lock_enable = beep_sweep_lock_enable;
    g_ocxo_status.beep_analyze_done_enable = beep_analyze_done_enable;
    g_ocxo_status.beep_demod_start_enable = beep_demod_start_enable;
    g_ocxo_status.mixed_retry_count = mixed_retry_count;
    g_ocxo_status.ask_square_dc_shift = ask_square_dc_shift;
    g_ocxo_status.ask_square_threshold_code = ask_square_threshold_code;
    g_ocxo_status.dds_offset_hz = app_ocxo_cal_limit_dds_offset_hz(dds_offset_hz);
    g_ocxo_status.dds_offset_step_hz = APP_OCXO_CAL_DDS_OFFSET_STEP_MID_HZ;
    g_ocxo_status.dds_offset_running = 0U;
    g_ocxo_status.state = APP_OCXO_CAL_HISTORY;
    g_ocxo_status.valid = 1U;
    g_ocxo_status.flash_loaded = 1U;
    g_ocxo_saved_mv = g_ocxo_status.dac_mv;
    app_ocxo_cal_apply_dac(g_ocxo_status.dac_mv);
    app_ocxo_cal_apply_dds_offset();
    return 1U;
}

/* 将当前恒温晶振控制电压保存到外部 Flash。 */
uint8_t app_ocxo_cal_save_to_flash(void)
{
    if (app_ocxo_cal_write_flash(1U) == 0U)
    {
        app_ocxo_cal_log("flash,save", 0U);
        return 0U;
    }

    app_ocxo_cal_log("flash,save", g_ocxo_status.dac_mv);
    return 1U;
}

/* 进入手动校准状态，保持当前 DAC 电压连续输出。 */
void app_ocxo_cal_enter(void)
{
    g_ocxo_status.state = APP_OCXO_CAL_RUNNING;
    app_ocxo_cal_apply_dac(g_ocxo_status.dac_mv);
}

/* 退出手动校准界面；未保存的新电压仍继续输出，但右上角状态不再显示 RUN。 */
void app_ocxo_cal_leave(void)
{
    if (g_ocxo_status.state != APP_OCXO_CAL_RUNNING)
    {
        return;
    }

    if (g_ocxo_status.valid == 0U)
    {
        g_ocxo_status.state = APP_OCXO_CAL_NONE;
    }
    else if ((g_ocxo_status.flash_loaded != 0U) && (g_ocxo_status.dac_mv == g_ocxo_saved_mv))
    {
        g_ocxo_status.state = APP_OCXO_CAL_HISTORY;
    }
    else
    {
        g_ocxo_status.state = APP_OCXO_CAL_CURRENT;
    }
}

/* 直接设置 PA4/DAC1_OUT1 电压，单位 mV。 */
void app_ocxo_cal_set_mv(uint32_t mv)
{
    g_ocxo_status.dac_mv = app_ocxo_cal_limit_mv(mv);
    g_ocxo_status.valid = 1U;
    if (g_ocxo_status.state != APP_OCXO_CAL_SAVING)
    {
        g_ocxo_status.state = APP_OCXO_CAL_RUNNING;
    }
    app_ocxo_cal_apply_dac(g_ocxo_status.dac_mv);
}

/* 按当前步进增减 PA4/DAC1_OUT1 电压。 */
void app_ocxo_cal_adjust(int32_t delta_mv)
{
    int32_t next_mv = (int32_t)g_ocxo_status.dac_mv + delta_mv;

    if (next_mv < (int32_t)APP_OCXO_CAL_MIN_MV)
    {
        next_mv = (int32_t)APP_OCXO_CAL_MIN_MV;
    }
    else if (next_mv > (int32_t)APP_OCXO_CAL_MAX_MV)
    {
        next_mv = (int32_t)APP_OCXO_CAL_MAX_MV;
    }

    app_ocxo_cal_set_mv((uint32_t)next_mv);
}

/* 循环切换 1mV、10mV、100mV 三档调节步进。 */
void app_ocxo_cal_cycle_step(void)
{
    if (g_ocxo_status.step_mv == APP_OCXO_CAL_STEP_FINE_MV)
    {
        g_ocxo_status.step_mv = APP_OCXO_CAL_STEP_MID_MV;
    }
    else if (g_ocxo_status.step_mv == APP_OCXO_CAL_STEP_MID_MV)
    {
        g_ocxo_status.step_mv = APP_OCXO_CAL_STEP_COARSE_MV;
    }
    else
    {
        g_ocxo_status.step_mv = APP_OCXO_CAL_STEP_FINE_MV;
    }
}

void app_ocxo_cal_get_status(app_ocxo_cal_status_t *status_out)
{
    if (status_out == NULL)
    {
        return;
    }

    *status_out = g_ocxo_status;
}

uint8_t app_ocxo_cal_get_auto_task_enable(void)
{
    return (g_ocxo_status.auto_task_enable != 0U) ? 1U : 0U;
}

/* UI 修改自动运行开关后立即落盘，保存时会保留当前 OCXO 电压。 */
uint8_t app_ocxo_cal_set_auto_task_enable(uint8_t enable)
{
    g_ocxo_status.auto_task_enable = (enable != 0U) ? 1U : 0U;
    if (app_ocxo_cal_write_flash(0U) == 0U)
    {
        app_ocxo_cal_log("auto_task,save", 0U);
        return 0U;
    }

    app_ocxo_cal_log("auto_task", g_ocxo_status.auto_task_enable);
    return 1U;
}

uint8_t app_ocxo_cal_get_boot_anim_enable(void)
{
    return (g_ocxo_status.boot_anim_enable != 0U) ? 1U : 0U;
}

/* UI 修改启动动画开关后立即落盘；该开关只影响下一次上电启动流程。 */
uint8_t app_ocxo_cal_set_boot_anim_enable(uint8_t enable)
{
    g_ocxo_status.boot_anim_enable = (enable != 0U) ? 1U : 0U;
    if (app_ocxo_cal_write_flash(0U) == 0U)
    {
        app_ocxo_cal_log("boot_anim,save", 0U);
        return 0U;
    }

    app_ocxo_cal_log("boot_anim", g_ocxo_status.boot_anim_enable);
    return 1U;
}

uint8_t app_ocxo_cal_get_background_enable(void)
{
    return (g_ocxo_status.background_enable != 0U) ? 1U : 0U;
}

/* UI 背景开关立即落盘；背景数据无效时界面仍会自动回退到纯色主题。 */
uint8_t app_ocxo_cal_set_background_enable(uint8_t enable)
{
    g_ocxo_status.background_enable = (enable != 0U) ? 1U : 0U;
    if (app_ocxo_cal_write_flash(0U) == 0U)
    {
        app_ocxo_cal_log("background,save", 0U);
        return 0U;
    }

    app_ocxo_cal_log("background", g_ocxo_status.background_enable);
    return 1U;
}

uint8_t app_ocxo_cal_get_runtime_monitor_enable(void)
{
    return (g_ocxo_status.runtime_monitor_enable != 0U) ? 1U : 0U;
}

/* UI 修改运行中监测开关后立即落盘，并让 DemodTask 在下一块 ADC 数据起使用新设置。 */
uint8_t app_ocxo_cal_set_runtime_monitor_enable(uint8_t enable)
{
    g_ocxo_status.runtime_monitor_enable = (enable != 0U) ? 1U : 0U;
    if (app_ocxo_cal_write_flash(0U) == 0U)
    {
        app_ocxo_cal_log("runtime_monitor,save", 0U);
        return 0U;
    }

    app_ocxo_cal_log("runtime_monitor", g_ocxo_status.runtime_monitor_enable);
    return 1U;
}

/* 保存普通功能开关；这些设置与 OCXO 电压共用同一条 Flash 配置记录。 */
static uint8_t app_ocxo_cal_set_flag(uint8_t *field, uint8_t enable, const char *name)
{
    uint32_t value;

    if ((field == NULL) || (name == NULL))
    {
        return 0U;
    }

    *field = (enable != 0U) ? 1U : 0U;
    value = (uint32_t)(*field);
    if (app_ocxo_cal_write_flash(0U) == 0U)
    {
        app_ocxo_cal_log(name, 0U);
        return 0U;
    }

    app_ocxo_cal_log(name, value);
    return 1U;
}

uint8_t app_ocxo_cal_get_ask_analog_demod_enable(void)
{
    return (g_ocxo_status.ask_analog_demod_enable != 0U) ? 1U : 0U;
}

uint8_t app_ocxo_cal_set_ask_analog_demod_enable(uint8_t enable)
{
    return app_ocxo_cal_set_flag(&g_ocxo_status.ask_analog_demod_enable, enable, "ask_analog_demod");
}

uint8_t app_ocxo_cal_get_fsk_analog_demod_enable(void)
{
    return (g_ocxo_status.fsk_analog_demod_enable != 0U) ? 1U : 0U;
}

uint8_t app_ocxo_cal_set_fsk_analog_demod_enable(uint8_t enable)
{
    return app_ocxo_cal_set_flag(&g_ocxo_status.fsk_analog_demod_enable, enable, "fsk_analog_demod");
}

uint8_t app_ocxo_cal_get_beep_ui_enable(void)
{
    return (g_ocxo_status.beep_ui_enable != 0U) ? 1U : 0U;
}

uint8_t app_ocxo_cal_set_beep_ui_enable(uint8_t enable)
{
    return app_ocxo_cal_set_flag(&g_ocxo_status.beep_ui_enable, enable, "beep_ui");
}

uint8_t app_ocxo_cal_get_beep_sweep_lock_enable(void)
{
    return (g_ocxo_status.beep_sweep_lock_enable != 0U) ? 1U : 0U;
}

uint8_t app_ocxo_cal_set_beep_sweep_lock_enable(uint8_t enable)
{
    return app_ocxo_cal_set_flag(&g_ocxo_status.beep_sweep_lock_enable, enable, "beep_sweep");
}

uint8_t app_ocxo_cal_get_beep_analyze_done_enable(void)
{
    return (g_ocxo_status.beep_analyze_done_enable != 0U) ? 1U : 0U;
}

uint8_t app_ocxo_cal_set_beep_analyze_done_enable(uint8_t enable)
{
    return app_ocxo_cal_set_flag(&g_ocxo_status.beep_analyze_done_enable, enable, "beep_analyze");
}

uint8_t app_ocxo_cal_get_beep_demod_start_enable(void)
{
    return (g_ocxo_status.beep_demod_start_enable != 0U) ? 1U : 0U;
}

uint8_t app_ocxo_cal_set_beep_demod_start_enable(uint8_t enable)
{
    return app_ocxo_cal_set_flag(&g_ocxo_status.beep_demod_start_enable, enable, "beep_demod");
}

uint8_t app_ocxo_cal_get_mixed_retry_count(void)
{
    return g_ocxo_status.mixed_retry_count;
}

/* 设置页每按一次递增 MIXED 重分析次数：0->1->...->5->INF->0。 */
uint8_t app_ocxo_cal_cycle_mixed_retry_count(void)
{
    uint8_t next_count;

    if (g_ocxo_status.mixed_retry_count == APP_OCXO_CAL_MIXED_RETRY_INFINITE)
    {
        next_count = 0U;
    }
    else if (g_ocxo_status.mixed_retry_count >= APP_OCXO_CAL_MIXED_RETRY_MAX_COUNT)
    {
        next_count = APP_OCXO_CAL_MIXED_RETRY_INFINITE;
    }
    else
    {
        next_count = g_ocxo_status.mixed_retry_count + 1U;
    }

    g_ocxo_status.mixed_retry_count = next_count;
    if (app_ocxo_cal_write_flash(0U) == 0U)
    {
        app_ocxo_cal_log("mixed_retry,save", 0U);
        return 0U;
    }

    app_ocxo_cal_log("mixed_retry", (uint32_t)g_ocxo_status.mixed_retry_count);
    return 1U;
}

uint8_t app_ocxo_cal_get_ask_square_dc_shift(void)
{
    if ((g_ocxo_status.ask_square_dc_shift < APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_MIN) ||
        (g_ocxo_status.ask_square_dc_shift > APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_MAX))
    {
        return APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_DEFAULT;
    }

    return g_ocxo_status.ask_square_dc_shift;
}

/* 设置页每按一次递增 ASK 比较器中心跟踪速度，超过上限后回到下限。 */
uint8_t app_ocxo_cal_cycle_ask_square_dc_shift(void)
{
    uint8_t next_shift = app_ocxo_cal_get_ask_square_dc_shift();

    if (next_shift >= APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_MAX)
    {
        next_shift = APP_OCXO_CAL_ASK_SQUARE_DC_SHIFT_MIN;
    }
    else
    {
        next_shift++;
    }

    g_ocxo_status.ask_square_dc_shift = next_shift;
    if (app_ocxo_cal_write_flash(0U) == 0U)
    {
        app_ocxo_cal_log("ask_sq_dc,save", 0U);
        return 0U;
    }

    app_ocxo_cal_log("ask_sq_dc", (uint32_t)g_ocxo_status.ask_square_dc_shift);
    return 1U;
}

uint16_t app_ocxo_cal_get_ask_square_threshold_code(void)
{
    if ((g_ocxo_status.ask_square_threshold_code < APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_MIN) ||
        (g_ocxo_status.ask_square_threshold_code > APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_MAX))
    {
        return APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_DEFAULT;
    }

    return g_ocxo_status.ask_square_threshold_code;
}

/* 设置页每按一次增加 ASK 比较器门限，超过上限后回到下限。 */
uint8_t app_ocxo_cal_cycle_ask_square_threshold_code(void)
{
    uint16_t next_threshold = app_ocxo_cal_get_ask_square_threshold_code();

    if (next_threshold >= APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_MAX)
    {
        next_threshold = APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_MIN;
    }
    else
    {
        next_threshold = (uint16_t)(next_threshold + APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_STEP);
        if (next_threshold > APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_MAX)
        {
            next_threshold = APP_OCXO_CAL_ASK_SQUARE_THRESHOLD_MAX;
        }
    }

    g_ocxo_status.ask_square_threshold_code = next_threshold;
    if (app_ocxo_cal_write_flash(0U) == 0U)
    {
        app_ocxo_cal_log("ask_sq_th,save", 0U);
        return 0U;
    }

    app_ocxo_cal_log("ask_sq_th", (uint32_t)g_ocxo_status.ask_square_threshold_code);
    return 1U;
}

void app_ocxo_cal_dds_offset_enter(void)
{
    g_ocxo_status.dds_offset_running = 1U;
    app_ocxo_cal_apply_dds_offset();
}

void app_ocxo_cal_dds_offset_leave(void)
{
    g_ocxo_status.dds_offset_running = 0U;
}

void app_ocxo_cal_dds_offset_adjust(int32_t delta_hz)
{
    int32_t next_offset_hz = g_ocxo_status.dds_offset_hz + delta_hz;

    g_ocxo_status.dds_offset_hz = app_ocxo_cal_limit_dds_offset_hz(next_offset_hz);
    g_ocxo_status.dds_offset_running = 1U;
    app_ocxo_cal_apply_dds_offset();
}

void app_ocxo_cal_dds_offset_cycle_step(void)
{
    if (g_ocxo_status.dds_offset_step_hz == APP_OCXO_CAL_DDS_OFFSET_STEP_FINE_HZ)
    {
        g_ocxo_status.dds_offset_step_hz = APP_OCXO_CAL_DDS_OFFSET_STEP_MID_HZ;
    }
    else if (g_ocxo_status.dds_offset_step_hz == APP_OCXO_CAL_DDS_OFFSET_STEP_MID_HZ)
    {
        g_ocxo_status.dds_offset_step_hz = APP_OCXO_CAL_DDS_OFFSET_STEP_COARSE_HZ;
    }
    else
    {
        g_ocxo_status.dds_offset_step_hz = APP_OCXO_CAL_DDS_OFFSET_STEP_FINE_HZ;
    }
}

uint8_t app_ocxo_cal_save_dds_offset_to_flash(void)
{
    g_ocxo_status.dds_offset_hz = app_ocxo_cal_limit_dds_offset_hz(g_ocxo_status.dds_offset_hz);
    if (app_ocxo_cal_write_flash(0U) == 0U)
    {
        app_ocxo_cal_log("dds_offset,save", 0U);
        return 0U;
    }

    app_ocxo_cal_apply_dds_offset();
    app_ocxo_cal_log_i32("dds_offset", g_ocxo_status.dds_offset_hz);
    return 1U;
}

int32_t app_ocxo_cal_get_dds_offset_hz(void)
{
    return app_ocxo_cal_limit_dds_offset_hz(g_ocxo_status.dds_offset_hz);
}
