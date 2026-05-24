#include "app_ocxo_cal.h"

#include "app_board_flash.h"
#include "dac.h"
#include "RtosTypes.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define APP_OCXO_CAL_DAC_MAX_CODE 4095U
#define APP_OCXO_CAL_DAC_VREF_MV 3300U
#define APP_OCXO_CAL_FLASH_ADDR (APP_BOARD_FLASH_TOTAL_SIZE - (2UL * APP_BOARD_FLASH_SECTOR_SIZE))
#define APP_OCXO_CAL_FLASH_MAGIC 0x4F43584FUL
#define APP_OCXO_CAL_FLASH_VERSION 1UL
#define APP_OCXO_CAL_FLASH_CRC_SEED 2166136261UL

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t record_size;
    uint32_t dac_mv;
    uint32_t crc;
} app_ocxo_cal_flash_record_t;

static app_ocxo_cal_status_t g_ocxo_status =
{
    .dac_mv = APP_OCXO_CAL_DEFAULT_MV,
    .step_mv = APP_OCXO_CAL_STEP_MID_MV,
    .state = APP_OCXO_CAL_NONE,
    .valid = 0U,
    .flash_loaded = 0U
};
static app_ocxo_cal_flash_record_t g_ocxo_flash_record;

static uint32_t app_ocxo_cal_limit_mv(uint32_t mv);
static uint32_t app_ocxo_cal_mv_to_code(uint32_t mv);
static void app_ocxo_cal_apply_dac(uint32_t mv);
static uint32_t app_ocxo_cal_crc(const app_ocxo_cal_flash_record_t *record);
static uint8_t app_ocxo_cal_record_valid(const app_ocxo_cal_flash_record_t *record);
static void app_ocxo_cal_log(const char *text, uint32_t value);

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

/* 计算 Flash 记录 CRC，只覆盖 crc 字段之前的数据。 */
static uint32_t app_ocxo_cal_crc(const app_ocxo_cal_flash_record_t *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint32_t crc = APP_OCXO_CAL_FLASH_CRC_SEED;
    uint32_t idx;

    if (record == NULL)
    {
        return 0UL;
    }

    for (idx = 0UL; idx < (sizeof(app_ocxo_cal_flash_record_t) - sizeof(uint32_t)); idx++)
    {
        crc ^= (uint32_t)bytes[idx];
        crc *= 16777619UL;
    }

    return crc;
}

/* 检查 Flash 记录是否属于当前 OCXO 校准结构。 */
static uint8_t app_ocxo_cal_record_valid(const app_ocxo_cal_flash_record_t *record)
{
    if (record == NULL)
    {
        return 0U;
    }

    if ((record->magic != APP_OCXO_CAL_FLASH_MAGIC) ||
        (record->version != APP_OCXO_CAL_FLASH_VERSION) ||
        (record->record_size != sizeof(app_ocxo_cal_flash_record_t)) ||
        (record->dac_mv < APP_OCXO_CAL_MIN_MV) ||
        (record->dac_mv > APP_OCXO_CAL_MAX_MV))
    {
        return 0U;
    }

    return (record->crc == app_ocxo_cal_crc(record)) ? 1U : 0U;
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

/* 初始化 OCXO 电压状态，并先输出默认电压。 */
void app_ocxo_cal_init(void)
{
    memset(&g_ocxo_flash_record, 0, sizeof(g_ocxo_flash_record));
    g_ocxo_status.dac_mv = APP_OCXO_CAL_DEFAULT_MV;
    g_ocxo_status.step_mv = APP_OCXO_CAL_STEP_MID_MV;
    g_ocxo_status.state = APP_OCXO_CAL_NONE;
    g_ocxo_status.valid = 0U;
    g_ocxo_status.flash_loaded = 0U;
    app_ocxo_cal_apply_dac(g_ocxo_status.dac_mv);
}

/* 从外部 Flash 读取上次保存的恒温晶振控制电压。 */
uint8_t app_ocxo_cal_load_from_flash(void)
{
    app_board_flash_result_t result;

    result = app_board_flash_read(APP_OCXO_CAL_FLASH_ADDR,
                                  (uint8_t *)&g_ocxo_flash_record,
                                  sizeof(g_ocxo_flash_record));
    if ((result != APP_BOARD_FLASH_OK) ||
        (app_ocxo_cal_record_valid(&g_ocxo_flash_record) == 0U))
    {
        g_ocxo_status.state = APP_OCXO_CAL_NONE;
        g_ocxo_status.valid = 0U;
        g_ocxo_status.flash_loaded = 0U;
        app_ocxo_cal_apply_dac(g_ocxo_status.dac_mv);
        return 0U;
    }

    g_ocxo_status.dac_mv = g_ocxo_flash_record.dac_mv;
    g_ocxo_status.state = APP_OCXO_CAL_HISTORY;
    g_ocxo_status.valid = 1U;
    g_ocxo_status.flash_loaded = 1U;
    app_ocxo_cal_apply_dac(g_ocxo_status.dac_mv);
    return 1U;
}

/* 将当前恒温晶振控制电压保存到外部 Flash。 */
uint8_t app_ocxo_cal_save_to_flash(void)
{
    app_board_flash_result_t result;

    g_ocxo_status.state = APP_OCXO_CAL_SAVING;
    memset(&g_ocxo_flash_record, 0, sizeof(g_ocxo_flash_record));
    g_ocxo_flash_record.magic = APP_OCXO_CAL_FLASH_MAGIC;
    g_ocxo_flash_record.version = APP_OCXO_CAL_FLASH_VERSION;
    g_ocxo_flash_record.record_size = sizeof(g_ocxo_flash_record);
    g_ocxo_flash_record.dac_mv = g_ocxo_status.dac_mv;
    g_ocxo_flash_record.crc = app_ocxo_cal_crc(&g_ocxo_flash_record);

    result = app_board_flash_erase_4k(APP_OCXO_CAL_FLASH_ADDR);
    if (result != APP_BOARD_FLASH_OK)
    {
        g_ocxo_status.state = APP_OCXO_CAL_ERROR;
        app_ocxo_cal_log("flash,save", 0U);
        return 0U;
    }

    result = app_board_flash_write(APP_OCXO_CAL_FLASH_ADDR,
                                   (const uint8_t *)&g_ocxo_flash_record,
                                   sizeof(g_ocxo_flash_record));
    if (result != APP_BOARD_FLASH_OK)
    {
        g_ocxo_status.state = APP_OCXO_CAL_ERROR;
        app_ocxo_cal_log("flash,save", 0U);
        return 0U;
    }

    memset(&g_ocxo_flash_record, 0, sizeof(g_ocxo_flash_record));
    result = app_board_flash_read(APP_OCXO_CAL_FLASH_ADDR,
                                  (uint8_t *)&g_ocxo_flash_record,
                                  sizeof(g_ocxo_flash_record));
    if ((result != APP_BOARD_FLASH_OK) ||
        (app_ocxo_cal_record_valid(&g_ocxo_flash_record) == 0U))
    {
        g_ocxo_status.state = APP_OCXO_CAL_ERROR;
        app_ocxo_cal_log("flash,save", 0U);
        return 0U;
    }

    g_ocxo_status.valid = 1U;
    g_ocxo_status.flash_loaded = 0U;
    g_ocxo_status.state = APP_OCXO_CAL_SAVE_OK;
    app_ocxo_cal_apply_dac(g_ocxo_status.dac_mv);
    app_ocxo_cal_log("flash,save", g_ocxo_status.dac_mv);
    return 1U;
}

/* 进入手动校准状态，保持当前 DAC 电压连续输出。 */
void app_ocxo_cal_enter(void)
{
    g_ocxo_status.state = APP_OCXO_CAL_RUNNING;
    app_ocxo_cal_apply_dac(g_ocxo_status.dac_mv);
}

/* 直接设置 PA4/DAC1_OUT1 电压，单位 mV。 */
void app_ocxo_cal_set_mv(uint32_t mv)
{
    g_ocxo_status.dac_mv = app_ocxo_cal_limit_mv(mv);
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
