#include <stdio.h>
#include "RtosTypes.h"
#include "SI5351.h"
#include "app_dds_ctrl.h"

#ifndef DDS_UART_STATUS_ENABLE
#define DDS_UART_STATUS_ENABLE 0U
#endif

void StartDDSTask(void *argument)
{
    AppDdsCmd cmd;
    const AppDdsStatus *st;
    uint8_t wait_logged;
#if (DDS_UART_STATUS_ENABLE != 0U)
    char log_buf[96];
#endif

    (void)argument;
    wait_logged = 0U;

    while (!app_si5351_is_clock_ready())
    {
        if ((wait_logged == 0U) && (g_uart_mode == UART_MODE_LOG))
        {
            print_queue_send_log("dds: wait si5351 refclk\r\n");
            wait_logged = 1U;
        }
        osDelay(10U);
    }

    if ((wait_logged != 0U) && (g_uart_mode == UART_MODE_LOG))
    {
        print_queue_send_log("dds: si5351 refclk ready\r\n");
    }

    AppDDS_Init();

#if (DDS_UART_STATUS_ENABLE != 0U)
    st = AppDDS_GetStatus();
    snprintf(log_buf, sizeof(log_buf),
             "dds: init done hw=%u ch=%u dirty=0x%02X err=%ld\r\n",
             st->hw_ready,
             st->selected_ch,
             st->dirty_mask,
             (long)st->last_err);
    if (g_uart_mode == UART_MODE_LOG)
    {
       print_queue_send_log(log_buf);
    }
#else
    (void)st;
#endif

    for (;;)
    {
        if (osMessageQueueGet(DDSQueueHandle, &cmd, NULL, osWaitForever) == osOK)
        {
            AppDDS_ExecuteCmd(&cmd);

#if (DDS_UART_STATUS_ENABLE != 0U)
            st = AppDDS_GetStatus();
            snprintf(log_buf, sizeof(log_buf),
                     "dds: cmd=%u arg=%lu hw=%u ch=%u dirty=0x%02X err=%ld\r\n",
                     (unsigned int)cmd.type,
                     (unsigned long)cmd.u32,
                     st->hw_ready,
                     st->selected_ch,
                     st->dirty_mask,
                     (long)st->last_err);
            if (g_uart_mode == UART_MODE_LOG)
            {
                print_queue_send_log(log_buf);
            }
#endif
        }
    }
}
