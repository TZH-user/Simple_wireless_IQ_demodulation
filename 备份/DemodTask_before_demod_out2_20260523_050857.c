#include "DemodTask.h"

#include "RtosTypes.h"
#include "AppDebugConfig.h"
#include "rx_demod.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "main.h"
#include "dac.h"
#include <stdio.h>
#include <string.h>

#define DEMOD_SAMPLE_RATE_HZ       2048000U
#define DEMOD_BLOCK_LOG_ENABLE     0U
#define DEMOD_START_LOG_ENABLE     1U
#define DEMOD_MODE_LOG_ENABLE      1U

/* ── ADC 块接收（与 sweep_task_publish_block 镜像模式）── */
typedef struct
{
    const uint16_t *i_buf;
    const uint16_t *q_buf;
    uint32_t        sample_cnt;
    uint8_t         pending;
} demod_block_t;

static demod_block_t      g_demod_block;
static demod_task_stats_t g_demod_stats;
static volatile uint8_t   g_demod_active = 0U;
static analyze_result_t   g_demod_result;
/* 解调输出缓冲区按 ADC 单块长度预留，后续接 DAC DMA 时直接复用。 */
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
static uint16_t           g_demod_dac_out_buf[RX_DEMOD_MAX_BLOCK_SAMPLES];

/* ── 内部辅助：Analyze 模式 → DEMODE 模式 ── */
static RxMode demod_map_mode(analyze_mode_t analyze_mode)
{
    switch (analyze_mode)
    {
    case ANALYZE_MODE_AM:  return RX_MODE_AM;
    case ANALYZE_MODE_ASK: return RX_MODE_ASK;
    case ANALYZE_MODE_FM:  return RX_MODE_FM;
    case ANALYZE_MODE_FSK: return RX_MODE_FSK;
    case ANALYZE_MODE_PSK: return RX_MODE_PSK;
    case ANALYZE_MODE_CW:  return RX_MODE_LOOPBACK;
    default:               return RX_MODE_LOOPBACK;
    }
}

/* ── 内部辅助：运行一次解调处理 ── */
static void demod_process_block(RxMode            rx_mode,
                                const uint16_t   *i_buf,
                                const uint16_t   *q_buf,
                                uint32_t          n,
                                uint16_t         *dac_out)
{
    switch (rx_mode)
    {
    case RX_MODE_AM:
    case RX_MODE_ASK:
        RxDemod_AM_ProcessBlock(i_buf, q_buf, n, dac_out);
        break;
    case RX_MODE_FM:
        RxDemod_FM_ProcessBlock(i_buf, q_buf, n, dac_out);
        break;
    case RX_MODE_FSK:
        RxDemod_FSK_ProcessBlock(i_buf, q_buf, n, dac_out);
        break;
    case RX_MODE_PSK:
        RxDemod_PSK_ProcessBlock(i_buf, q_buf, n, dac_out);
        break;
    default:
        break;
    }
}

/* ── 由 AdcTask 调用，发布 ADC 数据块给解调任务 ── */
void demod_task_publish_block(const uint16_t *i_buf,
                              const uint16_t *q_buf,
                              uint32_t        sample_cnt)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    g_demod_block.i_buf      = i_buf;
    g_demod_block.q_buf      = q_buf;
    g_demod_block.sample_cnt = sample_cnt;
    g_demod_block.pending    = 1U;
    __set_PRIMASK(primask);
}

/* ── 公共 API ── */

void demod_task_start_with_result(const analyze_result_t *result)
{
    uint32_t primask;

    if (result == NULL)
    {
        return;
    }

    RxDemod_Reset();

    primask = __get_PRIMASK();
    __disable_irq();
    g_demod_result = *result;
    g_demod_active = 1U;
    __set_PRIMASK(primask);

#if (DEMOD_START_LOG_ENABLE != 0U)
    {
        char log_buf[128];
        int n = snprintf(log_buf, sizeof(log_buf),
                         "demod: start mode=%d center=%luHz mod=%luHz low_if=%ldHz\r\n",
                         (int)result->mode,
                         (unsigned long)result->center_hz,
                         (unsigned long)result->mod_hz,
                         (long)result->low_if_hz);
        if ((n > 0) && ((size_t)n < sizeof(log_buf)))
        {
            print_queue_send(log_buf);
        }
    }
#endif
}

void demod_task_stop(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    g_demod_active          = 0U;
    g_demod_stats.state     = DEMOD_STATE_IDLE;
    __set_PRIMASK(primask);

    RxDemod_Reset();
}

void demod_task_get_stats(demod_task_stats_t *stats_out)
{
    uint32_t primask;

    if (stats_out == NULL)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    *stats_out = g_demod_stats;
    __set_PRIMASK(primask);
}

uint8_t demod_task_is_running(void)
{
    return (g_demod_active != 0U) ? 1U : 0U;
}

/* ── RTOS 任务入口 ── */
void StartDemodTask(void *argument)
{
    (void)argument;

    /* 解调输出缓冲区已放到静态 DMA 区，任务这里只保留运行状态。 */
    RxMode   current_mode = RX_MODE_LOOPBACK;
    uint8_t  mode_configured = 0U;

    RxDemod_Init(DEMOD_SAMPLE_RATE_HZ);

    for (;;)
    {
        demod_block_t block;
        uint32_t      primask;

        /* 未激活时休眠，等 demod_task_start_with_result 唤醒。 */
        if (g_demod_active == 0U)
        {
            mode_configured = 0U;
            g_demod_block.pending = 0U;
            osDelay(20U);
            continue;
        }

        /* 首次激活或模式变更时，重新配置解调器。 */
        if (mode_configured == 0U)
        {
            analyze_result_t result;

            primask = __get_PRIMASK();
            __disable_irq();
            result = g_demod_result;
            __set_PRIMASK(primask);

            current_mode = demod_map_mode(result.mode);
            RxDemod_SetMode(current_mode);
            mode_configured = 1U;

            g_demod_stats.state     = DEMOD_STATE_RUNNING;
            g_demod_stats.mode      = result.mode;
            g_demod_stats.center_hz = result.center_hz;
            g_demod_stats.mod_hz    = result.mod_hz;
            g_demod_stats.depth_pm  = result.depth_pm;
            g_demod_stats.low_if_hz = result.low_if_hz;
            g_demod_stats.block_cnt = 0U;
            g_demod_stats.sample_cnt = 0U;

#if (DEMOD_MODE_LOG_ENABLE != 0U)
            {
                char log_buf[96];
                int n = snprintf(log_buf, sizeof(log_buf),
                                 "demod: mode set rx=%d analyze=%d\r\n",
                                 (int)current_mode, (int)result.mode);
                if ((n > 0) && ((size_t)n < sizeof(log_buf)))
                {
                    print_queue_send(log_buf);
                }
            }
#endif
        }

        /* 等待 ADC 数据块就绪 */
        if (DemodBlockReadySemHandle == NULL)
        {
            osDelay(10U);
            continue;
        }

        if (osSemaphoreAcquire(DemodBlockReadySemHandle, osWaitForever) != osOK)
        {
            continue;
        }

        /* 临界区领取数据块，与 AdcTask ISR 互斥。 */
        primask = __get_PRIMASK();
        __disable_irq();
        block = g_demod_block;
        g_demod_block.pending = 0U;
        __set_PRIMASK(primask);

        if (block.pending == 0U)
        {
            continue;
        }

        if (g_demod_active == 0U)
        {
            continue;
        }

        if (block.sample_cnt > RX_DEMOD_MAX_BLOCK_SAMPLES)
        {
            block.sample_cnt = RX_DEMOD_MAX_BLOCK_SAMPLES;
        }

        /* 运行解调算法 */
        demod_process_block(current_mode,
                            block.i_buf,
                            block.q_buf,
                            block.sample_cnt,
                            g_demod_dac_out_buf);

        /*
         * TODO: DAC DMA 输出
         * 当前 HAL_DAC_Start_DMA 依赖 TIM 触发配置，接入前需在 CubeMX 中
         * 将 DAC1 的 Trigger 设为对应 TIM，并配置 DMA 循环/双缓冲模式。
         * 接入后替换为：
         *   HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1,
         *                     (uint32_t *)g_demod_dac_out_buf, block.sample_cnt,
         *                     DAC_ALIGN_12B_R);
         */
        (void)g_demod_dac_out_buf;

        g_demod_stats.block_cnt++;
        g_demod_stats.sample_cnt += block.sample_cnt;
    }
}
