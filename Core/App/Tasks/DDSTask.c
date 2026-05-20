#include <stdio.h>
#include "RtosTypes.h"
#include "AppDebugConfig.h"
#include "SI5351.h"
#include "app_si5351_drv.h"
#include "app_dds_ctrl.h"

#define DDS_LO_SET_LOG_ENABLE 0U
#define DDS_INIT_LOG_ENABLE 1U

/* ·························临时代码开始····················  */
/* 默认双通道输出开关，1 表示 DDS 任务启动后自动投递一组 40 MHz 正交输出命令。 */
#ifndef DDS_STARTUP_DUAL_TONE_ENABLE
#define DDS_STARTUP_DUAL_TONE_ENABLE 1U
#endif

#define DDS_STARTUP_CH2_INDEX 2U
#define DDS_STARTUP_CH3_INDEX 3U

/* 默认双通道输出频率，单位 Hz。 */
#define DDS_STARTUP_FREQ_HZ 40000000UL
/* 默认双通道输出幅度码。 */
#define DDS_STARTUP_AMP_CODE 512U
/* 默认“2 通道”相位，单位度。 */
#define DDS_STARTUP_CH2_PHASE_DEG 0U
/* 默认“3 通道”相位，单位度。 */
#define DDS_STARTUP_CH3_PHASE_DEG 90U
/* ·························临时代码结束····················  */

/* 投递一条 DDS 命令；若 RTOS 队列已绑定，则命令进入队列，否则退回直接执行。 */
static int dds_dispatch_cmd_checked(const AppDdsCmd *cmd)
{
    int ret;

    /* 判断命令指针是否有效。 */
    if (cmd == NULL)
    {
        return -1;
    }

    ret = AppDDS_DispatchCmd(cmd);
    return ret;
}

/* 启动后默认投递两路 40 MHz 正交输出配置，并在最后统一 Apply。 */
/* ·························临时代码开始····················  */
static void dds_post_startup_dual_tone(void)
{
#if (DDS_STARTUP_DUAL_TONE_ENABLE != 0U)
    static const AppDdsCmd startup_cmds[] =
    {
        /* 先配置用户口径中的 2 通道。 */
        { .type = APP_DDS_CMD_SELECT_CH,      .u32 = 0U,                 .u16 = 0U,                     .ch = DDS_STARTUP_CH2_INDEX },
        { .type = APP_DDS_CMD_SET_FREQ,       .u32 = DDS_STARTUP_FREQ_HZ,.u16 = 0U,                     .ch = 0U },
        { .type = APP_DDS_CMD_SET_AMP,        .u32 = 0U,                 .u16 = DDS_STARTUP_AMP_CODE,   .ch = 0U },
        { .type = APP_DDS_CMD_SET_PHASE_DEG,  .u32 = 0U,                 .u16 = DDS_STARTUP_CH2_PHASE_DEG, .ch = 0U },
        /* 再配置用户口径中的 3 通道。 */
        { .type = APP_DDS_CMD_SELECT_CH,      .u32 = 0U,                 .u16 = 0U,                     .ch = DDS_STARTUP_CH3_INDEX },
        { .type = APP_DDS_CMD_SET_FREQ,       .u32 = DDS_STARTUP_FREQ_HZ,.u16 = 0U,                     .ch = 0U },
        { .type = APP_DDS_CMD_SET_AMP,        .u32 = 0U,                 .u16 = DDS_STARTUP_AMP_CODE,   .ch = 0U },
        { .type = APP_DDS_CMD_SET_PHASE_DEG,  .u32 = 0U,                 .u16 = DDS_STARTUP_CH3_PHASE_DEG, .ch = 0U },
        /* 最后统一 Apply，保证两路尽量同步生效。 */
        { .type = APP_DDS_CMD_APPLY,          .u32 = 0U,                 .u16 = 0U,                     .ch = 0U }
    };
    uint32_t idx;

    for (idx = 0U; idx < (sizeof(startup_cmds) / sizeof(startup_cmds[0])); idx++)
    {
        /* 判断命令投递是否成功；失败时只记日志，不中断 DDS 主任务启动。 */
        if (dds_dispatch_cmd_checked(&startup_cmds[idx]) != 0)
        {
            print_queue_send("dds: startup dual-tone dispatch failed\r\n");
            break;
        }
    }
#endif
}
/* ·························临时代码结束····················  */

void StartDDSTask(void *argument)
{
    AppDdsCmd cmd;
    const AppDdsStatus *st;
    uint8_t wait_logged;
#if (APP_PRINT_LOG_ENABLE != 0U)
    char log_buf[96];
#endif

    (void)argument;
    wait_logged = 0U;

    while (!app_si5351_is_clock_ready())
    {
        if (wait_logged == 0U)
        {
            print_queue_send("dds: wait si5351 refclk\r\n");
            wait_logged = 1U;
        }
        osDelay(10U);
    }

    if (wait_logged != 0U)
    {
        print_queue_send("dds: si5351 refclk ready\r\n");
    }

    AppDDS_Init();

    /* DDS 初始化完成后，默认投递一组 CH2/CH3 双通道 40 MHz 正交输出命令。 */
    /* 临时代码 */
    dds_post_startup_dual_tone();

#if (DDS_INIT_LOG_ENABLE != 0U)
    st = AppDDS_GetStatus();
    snprintf(log_buf, sizeof(log_buf),
             "dds: init done hw=%u ch=%u dirty=0x%02X err=%ld\r\n",
             st->hw_ready,
             st->selected_ch,
             st->dirty_mask,
             (long)st->last_err);
    print_queue_send(log_buf);
#else
    (void)st;
#endif

    for (;;)
    {
        if (osMessageQueueGet(DDSQueueHandle, &cmd, NULL, osWaitForever) == osOK)
        {
            AppDDS_ExecuteCmd(&cmd);

#if (DDS_LO_SET_LOG_ENABLE != 0U)
            st = AppDDS_GetStatus();
            snprintf(log_buf, sizeof(log_buf),
                     "dds: cmd=%u arg=%lu hw=%u ch=%u dirty=0x%02X err=%ld\r\n",
                     (unsigned int)cmd.type,
                     (unsigned long)cmd.u32,
                     st->hw_ready,
                     st->selected_ch,
                     st->dirty_mask,
                     (long)st->last_err);
            print_queue_send(log_buf);
#endif
        }
    }
}
