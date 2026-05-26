#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "adc.h"
#include "tim.h"
#include "AppDebugConfig.h"
#include "ModDetectTask.h"
#include "DemodTask.h"
#include <stdio.h>
#include "RtosTypes.h"

#define ADC_NEAR_RAIL_DELTA     164U                    /* ~1% of 14-bit FS (16383) */
#define ADC_CLIP_LOW            10U                     /* 设置低 clipping 值 */
#define ADC_CLIP_HIGH           16373U                  /* 16383 - 10 */
#define ADC_FS_MAX              16383U                  /* 14-bit ADC 的最大码值 */
#define ADC_LOG_ENABLE          0    /* 是否启用 ADC 数据的 UART 输出 */

/* ADC DMA与时钟开启并复位采样完成标志位 */
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

/* 使能 ADC DMA 中断并在 ISR 中设置就绪标志位；ADC 任务通过检查这些标志位来处理新的数据块。 */
static inline void adc_invalidate_block_cache(uint32_t start_index)
{
#if (__DCACHE_PRESENT == 1U)    /* 条件编译判断：如果处理器支持数据缓存 */
    const int32_t bytes = (int32_t)(ADC_BLOCK_N * sizeof(uint16_t));
    SCB_InvalidateDCache_by_Addr((uint32_t *)&adc1_buf[start_index], bytes);
    SCB_InvalidateDCache_by_Addr((uint32_t *)&adc2_buf[start_index], bytes);
#else                           /* 如果没有数据缓存，函数不执行任何操作。 */
    (void)start_index;
#endif
}

/* ADC 任务主循环：等待 ADC 数据块就绪，处理数据块，更新统计，并可选地将数据发送到 UART 输出。 */
void StartAdcTask(void *argument)
{
    (void)argument;
    adc_start_stream();

    for (;;)
    {
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

            if (block_flag == 0U)   break;                  /* 没有块就绪，退出等待 */

            adc_invalidate_block_cache(start_index);        /* 块准备好了；使DCache失效。 */

            if (block_flag == 0x01U)    adc_task_half_cnt++;
            else                        adc_task_full_cnt++;

#if (ADC_LOG_ENABLE != 0U)
            adc_block_log(start_index);
#endif

            sweep_task_publish_block(&adc1_buf[start_index], &adc2_buf[start_index], ADC_BLOCK_N);
            if (SweepBlockReadySemHandle != NULL)
            {
                /* 通知ModDetectTask执行算法任务 */
                (void)osSemaphoreRelease(SweepBlockReadySemHandle);
            }
            demod_task_publish_block(&adc1_buf[start_index], &adc2_buf[start_index], ADC_BLOCK_N);
            if (DemodBlockReadySemHandle != NULL)
            {
                (void)osSemaphoreRelease(DemodBlockReadySemHandle);
            }

        }
    }
}

#if (ADC_LOG_ENABLE != 0U)
/*
    * ADC 数据转换：
        - 输入：ADC 原始码值（0 到 16383）
        - 输出：电压值，单位为 0.0001 V（即 1e-4 V），范围约为 0 到 3.3000 V
        - 计算方法：电压 = (原始码值 / 16383) * 3.3 V
*/
static uint32_t adc_raw_to_v_1e4(uint16_t raw)
{
    return (uint32_t)((((uint64_t)raw * 33000ULL) + (ADC_FS_MAX / 2U)) / ADC_FS_MAX);
}
#endif

#if (ADC_LOG_ENABLE != 0U)
/*
    * 将 ADC 数据块发布到消息队列
        - 输入：块起始索引（0 或 ADC_BLOCK_N）
        - 处理：调用 adc转换函数后将数据转换到串口输出格式，分批次放入消息队列
        - 输出：无直接返回值，但会将格式化的电压数据发送到 UART 输出任务。
*/
static void adc_block_log(uint32_t start_index)
{
    print_msg_t msg;
    int n;

    msg.len = 0U;

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

        if ((n <= 0) || ((size_t)n >= (sizeof(msg.data) - msg.len)))    /* 如果 snprintf 失败或缓冲区不足，发送完当前数据再继续填充 */
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

            if ((n <= 0) || ((size_t)n >= sizeof(msg.data)))            /* 如果再次 snprintf 失败，丢弃这个数据点继续循环。 */
            {
                continue;
            }
        }

        msg.len += (uint16_t)n;
    }

    if (msg.len > 0U)                                                   /* 发送剩余数据 */
    {
        osMessageQueuePut(PrintQueueHandle, &msg, 0U, osWaitForever);
    }
}
#endif