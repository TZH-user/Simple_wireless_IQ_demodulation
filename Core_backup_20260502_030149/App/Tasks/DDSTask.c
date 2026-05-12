#include <stdio.h>
#include "RtosTypes.h"
#include "app_dds_ctrl.h"

void StartDDSTask(void *argument)
{
    AppDdsCmd cmd;
    const AppDdsStatus *st;
    char log_buf[96];

    (void)argument;

    AppDDS_Init();

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

    for (;;)
    {
        if (osMessageQueueGet(DDSQueueHandle, &cmd, NULL, osWaitForever) == osOK)
        {
            AppDDS_ExecuteCmd(&cmd);

            st = AppDDS_GetStatus();
            snprintf(log_buf, sizeof(log_buf),
                     "dds: after cmd hw=%u ch=%u dirty=0x%02X err=%ld\r\n",
                     st->hw_ready,
                     st->selected_ch,
                     st->dirty_mask,
                     (long)st->last_err);
            print_queue_send_log(log_buf);
        }
    }
}
