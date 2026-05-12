#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "adc.h"
#include "tim.h"
#include "app_adc_log.h"
#include "app_signal_detect.h"
#include "AppDebugConfig.h"
#include <stdio.h>
#include "RtosTypes.h"

#define ADC_NEAR_RAIL_DELTA     164U                    /* ~1% of 14-bit FS (16383) */
#define ADC_CLIP_LOW            10U                     /* 设置低 clipping 值 */
#define ADC_CLIP_HIGH           16373U                  /* 16383 - 10 */
#define ADC_FS_MAX              16383U                  /* 14-bit ADC 的最大码值 */

static uint32_t adc_raw_to_v_1e4(uint16_t raw)
{
    return (uint32_t)((((uint64_t)raw * 33000ULL) + (ADC_FS_MAX / 2U)) / ADC_FS_MAX);
}

static void adc_start_stream(void)
{
    adc1_half_ready = 0U;
    adc1_full_ready = 0U;
    adc2_half_ready = 0U;
    adc2_full_ready = 0U;
    adc_block_ready_mask = 0U;

    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc1_buf, ADC_BUFFER_N) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_ADC_Start_DMA(&hadc2, (uint32_t *)adc2_buf, ADC_BUFFER_N) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_TIM_Base_Start(&htim2) != HAL_OK)
    {
        Error_Handler();
    }
}

static inline void adc_invalidate_block_cache(uint32_t start_index)
{
#if (__DCACHE_PRESENT == 1U)
    const int32_t bytes = (int32_t)(ADC_BLOCK_N * sizeof(uint16_t));
    SCB_InvalidateDCache_by_Addr((uint32_t *)&adc1_buf[start_index], bytes);
    SCB_InvalidateDCache_by_Addr((uint32_t *)&adc2_buf[start_index], bytes);
#else
    (void)start_index;
#endif
}

static void adc_publish_block_to_queue(uint32_t start_index)
{
    print_msg_t msg;
    int n;

    msg.len = 0U;
    msg.kind = PRINT_KIND_VOFA;

    for (uint32_t i = start_index; i < (start_index + ADC_BLOCK_N); i += VOFA_SEND_STEP)
    {
        uint32_t adc1_v_1e4 = adc_raw_to_v_1e4(adc1_buf[i]);
        uint32_t adc2_v_1e4 = adc_raw_to_v_1e4(adc2_buf[i]);

        n = snprintf(&msg.data[msg.len],
                     sizeof(msg.data) - msg.len,
                     "%lu.%04lu,%lu.%04lu\n",
                     adc1_v_1e4 / 10000UL,
                     adc1_v_1e4 % 10000UL,
                     adc2_v_1e4 / 10000UL,
                     adc2_v_1e4 % 10000UL);

        if ((n <= 0) || ((size_t)n >= (sizeof(msg.data) - msg.len)))
        {
            osMessageQueuePut(PrintQueueHandle, &msg, 0U, osWaitForever);
            msg.len = 0U;

            n = snprintf(&msg.data[msg.len],
                         sizeof(msg.data) - msg.len,
                         "%lu.%04lu,%lu.%04lu\n",
                         adc1_v_1e4 / 10000UL,
                         adc1_v_1e4 % 10000UL,
                         adc2_v_1e4 / 10000UL,
                         adc2_v_1e4 % 10000UL);

            if ((n <= 0) || ((size_t)n >= sizeof(msg.data)))
            {
                continue;
            }
        }

        msg.len += (uint16_t)n;
    }

    if (msg.len > 0U)
    {
        osMessageQueuePut(PrintQueueHandle, &msg, 0U, osWaitForever);
    }
}

static uint32_t  adc_range_sample_cnt;
static uint16_t adc_range_i_min, adc_range_i_max;
static uint16_t adc_range_q_min, adc_range_q_max;
static uint32_t adc_range_i_clip_lo, adc_range_i_clip_hi;
static uint32_t adc_range_q_clip_lo, adc_range_q_clip_hi;
static uint32_t adc_range_i_near_lo, adc_range_i_near_hi;
static uint32_t adc_range_q_near_lo, adc_range_q_near_hi;

static void adc_range_reset(void)
{
    adc_range_sample_cnt  = 0U;
    adc_range_i_min = 0xFFFFU; adc_range_i_max = 0U;
    adc_range_q_min = 0xFFFFU; adc_range_q_max = 0U;
    adc_range_i_clip_lo = 0U; adc_range_i_clip_hi = 0U;
    adc_range_q_clip_lo = 0U; adc_range_q_clip_hi = 0U;
    adc_range_i_near_lo = 0U; adc_range_i_near_hi = 0U;
    adc_range_q_near_lo = 0U; adc_range_q_near_hi = 0U;
}

static void adc_range_accum(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t block_n)
{
    for (uint32_t i = 0U; i < block_n; i++)
    {
        uint16_t iv = i_buf[i];
        uint16_t qv = q_buf[i];

        if (iv < adc_range_i_min) adc_range_i_min = iv;
        if (iv > adc_range_i_max) adc_range_i_max = iv;
        if (qv < adc_range_q_min) adc_range_q_min = qv;
        if (qv > adc_range_q_max) adc_range_q_max = qv;

        if (iv <= ADC_CLIP_LOW)  adc_range_i_clip_lo++;
        if (iv >= ADC_CLIP_HIGH) adc_range_i_clip_hi++;
        if (qv <= ADC_CLIP_LOW)  adc_range_q_clip_lo++;
        if (qv >= ADC_CLIP_HIGH) adc_range_q_clip_hi++;

        if (iv <= ADC_NEAR_RAIL_DELTA)            adc_range_i_near_lo++;
        if (iv >= (ADC_FS_MAX - ADC_NEAR_RAIL_DELTA)) adc_range_i_near_hi++;
        if (qv <= ADC_NEAR_RAIL_DELTA)            adc_range_q_near_lo++;
        if (qv >= (ADC_FS_MAX - ADC_NEAR_RAIL_DELTA)) adc_range_q_near_hi++;
    }

    adc_range_sample_cnt += block_n;
}

void StartAdcTask(void *argument)
{
    (void)argument;
    adc_start_stream();
    app_signal_detect_init();
    adc_range_reset();

    for (;;)
    {
        app_signal_detect_status_t adc_sig_log_status;
        app_adc_log_health_snapshot_t health_snapshot;
        app_adc_log_range_snapshot_t range_snapshot;

        if (osSemaphoreAcquire(AdcFrameReadySemHandle, osWaitForever) != osOK)
        {
            continue;
        }

        for (;;)
        {
            uint8_t block_flag = 0U;
            uint32_t start_index = 0U;

            uint32_t primask = __get_PRIMASK(); /* 进入临界区以避免与ADC ISR赛跑。 */
            __disable_irq();    
            /* Claim one completed ADC block before leaving the critical section. */
            if ((adc_block_ready_mask & 0x01U) != 0U)   /* 通过检查位掩码来确定哪个 ADC 块已经准备好被处理；0x01 表示第一个块，0x02 表示第二个块。 */
            {
                adc_block_ready_mask &= (uint8_t)~0x01U;    /* 清除就绪标志以领取这个块，防止 ISR 重复设置同一块就绪。 */
                block_flag = 0x01U;                         /* 设置块标志以指示后续处理哪个块；0x01 表示第一个块，0x02 表示第二个块。 */
                start_index = 0U;                           /* 根据块标志计算这个块在 ADC 缓冲区中的起始索引；第一个块从索引 0 开始，第二个块从 ADC_BLOCK_N 开始。 */
            }
            else if ((adc_block_ready_mask & 0x02U) != 0U)
            {
                adc_block_ready_mask &= (uint8_t)~0x02U;
                block_flag = 0x02U;
                start_index = ADC_BLOCK_N;
            }
            __set_PRIMASK(primask); /* 离开临界区 */

            if (block_flag == 0U)   break;

            adc_invalidate_block_cache(start_index);        /* 块准备好了；在接触新的DMA样本之前使DCache失效。 */
            if (ADC_RANGE_LOG_ENABLE != 0U)
            {
                adc_range_accum(&adc1_buf[start_index], &adc2_buf[start_index], ADC_BLOCK_N);
            }

            if (block_flag == 0x01U)    adc_task_half_cnt++;
            else                        adc_task_full_cnt++;

            /* 可选的VOFA路径：将原始样本转换为电压并将其排队用于UART输出 */
            if ((ADC_UART_OUTPUT_ENABLE != 0U) && (g_uart_mode == UART_MODE_VOFA))
            {
                adc_publish_block_to_queue(start_index);
            }

            /* ADC 任务只分发当前块，扫频、IQ 预处理和载波闭环由 Signal 层内部决定。 */
            app_signal_pipeline_process_block(&adc1_buf[start_index], &adc2_buf[start_index], ADC_BLOCK_N);

        }

        {
            uint32_t primask = __get_PRIMASK();
            __disable_irq();
            health_snapshot.pending_mask = adc_block_ready_mask;
            health_snapshot.isr_half = adc_isr_half_cnt;
            health_snapshot.isr_full = adc_isr_full_cnt;
            health_snapshot.task_half = adc_task_half_cnt;
            health_snapshot.task_full = adc_task_full_cnt;
            health_snapshot.overrun = adc_overrun_cnt;
            __set_PRIMASK(primask);
        }
        app_adc_log_health_1s(&health_snapshot);

        app_signal_detect_get_status(&adc_sig_log_status);
        app_adc_log_algo_1s(&adc_sig_log_status);

        range_snapshot.sample_cnt = adc_range_sample_cnt;
        range_snapshot.i_min = adc_range_i_min;
        range_snapshot.i_max = adc_range_i_max;
        range_snapshot.q_min = adc_range_q_min;
        range_snapshot.q_max = adc_range_q_max;
        range_snapshot.i_clip_lo = adc_range_i_clip_lo;
        range_snapshot.i_clip_hi = adc_range_i_clip_hi;
        range_snapshot.q_clip_lo = adc_range_q_clip_lo;
        range_snapshot.q_clip_hi = adc_range_q_clip_hi;
        range_snapshot.i_near_lo = adc_range_i_near_lo;
        range_snapshot.i_near_hi = adc_range_i_near_hi;
        range_snapshot.q_near_lo = adc_range_q_near_lo;
        range_snapshot.q_near_hi = adc_range_q_near_hi;
        if (app_adc_log_range_1s(&range_snapshot) != 0U)
        {
            adc_range_reset();
        }
    }
}
