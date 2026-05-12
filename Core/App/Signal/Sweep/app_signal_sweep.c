#include "app_signal_sweep.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cmsis_os2.h"
#include "app_dds_ctrl.h"
#include "RtosTypes.h"

/* 宏定义说明：APP_SIGSWEEP_MAX_STEPS = 512U；扫频模块内部 Vpp 表最多保存的频点数，防止数组越界。 */
#define APP_SIGSWEEP_MAX_STEPS 512U
/* 宏定义说明：APP_SIGSWEEP_INVALID_STEP = 0xFFFFU；扫频模块用来表示“没有找到有效频点索引”的无效索引哨兵值。 */
#define APP_SIGSWEEP_INVALID_STEP 0xFFFFU

/* 单个 ADC block 的峰峰值统计结果。 */
typedef struct
{
    uint32_t metric_vpp;                       /* 综合 Vpp 指标，取 I/Q 两路较大值作为扫频强度。 */
    uint32_t i_vpp;                            /* I 路峰峰值。 */
    uint32_t q_vpp;                            /* Q 路峰峰值。 */
} app_signal_sweep_vpp_t;

/* 单轮扫频结束后的载波判断结果。 */
typedef struct
{
    uint8_t carrier_present;                   /* 1 表示 Vpp 曲线满足载波存在判据。 */
    uint8_t locked;                            /* 1 表示本轮估计可作为最终锁定结果。 */
    uint32_t estimated_hz;                     /* 根据 Vpp 曲线估计出的载波频率。 */
} app_signal_sweep_result_t;

/* 扫频状态机上下文。 */
typedef struct
{
    app_signal_detect_status_t status;          /* 对外可读状态，保持与原 app_signal_detect 状态字段兼容。 */
    uint32_t vpp_table[APP_SIGSWEEP_MAX_STEPS]; /* 当前一轮扫频中每个频点的 Vpp 指标。 */
    uint64_t step_vpp_sum;                      /* 当前频点 dwell 期间综合 Vpp 累加和。 */
    uint64_t step_i_vpp_sum;                    /* 当前频点 dwell 期间 I 路 Vpp 累加和。 */
    uint64_t step_q_vpp_sum;                    /* 当前频点 dwell 期间 Q 路 Vpp 累加和。 */
    uint32_t coarse_estimate_hz;                /* 粗扫估计结果，作为细扫中心频率。 */
    uint16_t step_dwell_count;                  /* 当前频点已累计的 ADC block 数。 */
    uint16_t scan_dwell_blocks;                 /* 每个频点需要累计的 ADC block 数。 */
    uint32_t scan_start_hz;                     /* 当前扫描阶段起始频率。 */
    uint32_t scan_stop_hz;                      /* 当前扫描阶段停止频率。 */
    uint32_t scan_step_hz;                      /* 当前扫描阶段步进频率。 */
    uint8_t inited;                             /* 初始化完成标志。 */
} app_signal_sweep_ctx_t;

static app_signal_sweep_ctx_t g_sweep;

static uint16_t app_signal_sweep_calc_step_count(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz);
static uint32_t app_signal_sweep_step_freq_hz(uint32_t start_hz, uint32_t step_hz, uint16_t step_index);
static uint32_t app_signal_sweep_clamp_to_valid_hz(uint32_t hz);
static void app_signal_sweep_set_lo(uint32_t lo_hz);
static app_signal_sweep_vpp_t app_signal_sweep_block_vpp(const uint16_t *i_buf,
                                                         const uint16_t *q_buf,
                                                         uint32_t sample_cnt);
static void app_signal_sweep_begin_scan(void);
static void app_signal_sweep_begin_sweep(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz, uint8_t stage);
static void app_signal_sweep_begin_fine_scan(uint32_t center_hz);
static void app_signal_sweep_finish_step(uint32_t avg_vpp, uint32_t avg_i_vpp, uint32_t avg_q_vpp);
static app_signal_sweep_result_t app_signal_sweep_eval_current_sweep(void);
static void app_signal_sweep_finish_current_sweep(void);
static void app_signal_sweep_finish_locked(uint8_t carrier_present, uint8_t locked, uint32_t estimate_hz);
/* 条件编译判断：判断 `(APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
static const char *app_signal_sweep_stage_name(uint8_t stage);
static void app_signal_sweep_trace_line(const char *text);
static void app_signal_sweep_trace_point(uint32_t lo_hz, uint32_t vpp, uint32_t i_vpp, uint32_t q_vpp);
static void app_signal_sweep_trace_sweep_done(const char *name);
#endif

/* 条件编译判断：判断 `(APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
static void app_signal_sweep_trace_line(const char *text)
{
    /* 判断：`text != NULL`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后调用 print_queue_send_log()：把格式化后的日志文本送入打印队列，最终由串口/日志任务输出。 */
    if (text != NULL)
    {
        /* 函数跳转：调用 print_queue_send_log()，把格式化后的日志文本送入打印队列，最终由串口/日志任务输出。 */
        print_queue_send_log(text);
    }
}

/* 输出单个扫频点的 Vpp 采样结果，供串口画图或人工检查曲线形状。 */
static void app_signal_sweep_trace_point(uint32_t lo_hz, uint32_t vpp, uint32_t i_vpp, uint32_t q_vpp)
{
    char line[96];

    /* 函数跳转：调用 snprintf()，把当前状态格式化成字符串，供后续日志输出。 */
    (void)snprintf(line,
                   sizeof(line),
                   "vpp,%lu,%lu,%lu,%lu\r\n",
                   (unsigned long)lo_hz,
                   (unsigned long)vpp,
                   (unsigned long)i_vpp,
                   (unsigned long)q_vpp);
    /* 函数跳转：调用 app_signal_sweep_trace_line()，把扫频日志字符串送到日志队列。 */
    app_signal_sweep_trace_line(line);
}

/* 输出当前一轮粗扫或细扫结束摘要。 */
static void app_signal_sweep_trace_sweep_done(const char *name)
{
    char line[144];

    /* 函数跳转：调用 snprintf()，把当前状态格式化成字符串，供后续日志输出。 */
    (void)snprintf(line,
                   sizeof(line),
                   "vpp_%s_done,est=%lu,peak=%lu,valley=%lu,th=%lu,left=%lu,right=%lu\r\n",
                   name,
                   (unsigned long)g_sweep.status.estimated_carrier_hz,
                   (unsigned long)g_sweep.status.peak_vpp_raw,
                   (unsigned long)g_sweep.status.valley_vpp_raw,
                   (unsigned long)g_sweep.status.threshold_vpp_raw,
                   (unsigned long)g_sweep.status.left_edge_hz,
                   (unsigned long)g_sweep.status.right_edge_hz);
    /* 函数跳转：调用 app_signal_sweep_trace_line()，把扫频日志字符串送到日志队列。 */
    app_signal_sweep_trace_line(line);
}
#endif

/* 根据起止频率和步进频率计算本轮扫频需要的频点数量。 */
static uint16_t app_signal_sweep_calc_step_count(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz)
{
    uint32_t span_hz;
    uint32_t steps_u32;

    /* 判断：`(step_hz == 0UL) || (stop_hz < start_hz)`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；确认步进频率是否有效；确认扫描上下限是否反向；成立后直接返回/退出当前函数或返回指定结果。 */
    if ((step_hz == 0UL) || (stop_hz < start_hz))
    {
        return 1U;
    }

    span_hz = stop_hz - start_hz;
    steps_u32 = (span_hz / step_hz) + 1UL;
    /* 判断：`steps_u32 > APP_SIGSWEEP_MAX_STEPS`。含义：确认频点数量是否超过表容量；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (steps_u32 > APP_SIGSWEEP_MAX_STEPS)
    {
        steps_u32 = APP_SIGSWEEP_MAX_STEPS;
    }

    return (uint16_t)steps_u32;
}

/* 根据起始频率、步进频率和步骤索引计算当前扫频点。 */
static uint32_t app_signal_sweep_step_freq_hz(uint32_t start_hz, uint32_t step_hz, uint16_t step_index)
{
    return start_hz + ((uint32_t)step_index * step_hz);
}

/* 将估计频率限制在当前配置允许的有效载波范围内。 */
static uint32_t app_signal_sweep_clamp_to_valid_hz(uint32_t hz)
{
    /* 判断：`hz < APP_SIGDET_VALID_START_HZ`。含义：确认频率是否超出最终有效范围；成立后直接返回/退出当前函数或返回指定结果。 */
    if (hz < APP_SIGDET_VALID_START_HZ)
    {
        return APP_SIGDET_VALID_START_HZ;
    }

    /* 判断：`hz > APP_SIGDET_VALID_STOP_HZ`。含义：确认频率是否超出最终有效范围；成立后直接返回/退出当前函数或返回指定结果。 */
    if (hz > APP_SIGDET_VALID_STOP_HZ)
    {
        return APP_SIGDET_VALID_STOP_HZ;
    }

    return hz;
}

/* 通过 DDS 控制层写入新的 LO 频率；本模块不直接操作 AD9959 寄存器。 */
static void app_signal_sweep_set_lo(uint32_t lo_hz)
{
    AppDdsCmd cmd;

    /* 函数跳转：调用 AppDDS_MakeSetChFreqApplyCmd()，生成 DDS 设置某通道频率并立即应用的命令。 */
    cmd = AppDDS_MakeSetChFreqApplyCmd((uint8_t)APP_SIGDET_DDS_CHANNEL, lo_hz);
    /* 函数跳转：调用 AppDDS_DispatchCmd()，把 DDS 命令派发到 DDS 控制层，由底层完成真正写频。 */
    (void)AppDDS_DispatchCmd(&cmd);
}

/* 统计一个 ADC block 的 I/Q Vpp，取较大路作为扫频强度指标。 */
static app_signal_sweep_vpp_t app_signal_sweep_block_vpp(const uint16_t *i_buf,
                                                         const uint16_t *q_buf,
                                                         uint32_t sample_cnt)
{
    app_signal_sweep_vpp_t out;
    uint32_t idx;
    uint16_t i_min;
    uint16_t i_max;
    uint16_t q_min;
    uint16_t q_max;

    /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
    memset(&out, 0, sizeof(out));

    /* 判断：`(i_buf == NULL) || (q_buf == NULL) || (sample_cnt == 0U)`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；判断样本数量是否足够进行当前统计；成立后直接返回/退出当前函数或返回指定结果。 */
    if ((i_buf == NULL) || (q_buf == NULL) || (sample_cnt == 0U))
    {
        return out;
    }

    i_min = i_buf[0];
    i_max = i_buf[0];
    q_min = q_buf[0];
    q_max = q_buf[0];

    for (idx = 1U; idx < sample_cnt; idx++)
    {
        uint16_t i_sample = i_buf[idx];
        uint16_t q_sample = q_buf[idx];

        /* 判断：`i_sample < i_min`。含义：更新当前 block 的 I/Q 最小值或最大值；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
        if (i_sample < i_min)
        {
            i_min = i_sample;
        }
        /* 判断：`i_sample > i_max`。含义：更新当前 block 的 I/Q 最小值或最大值；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
        if (i_sample > i_max)
        {
            i_max = i_sample;
        }
        /* 判断：`q_sample < q_min`。含义：更新当前 block 的 I/Q 最小值或最大值；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
        if (q_sample < q_min)
        {
            q_min = q_sample;
        }
        /* 判断：`q_sample > q_max`。含义：更新当前 block 的 I/Q 最小值或最大值；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
        if (q_sample > q_max)
        {
            q_max = q_sample;
        }
    }

    out.i_vpp = (uint32_t)i_max - (uint32_t)i_min;
    out.q_vpp = (uint32_t)q_max - (uint32_t)q_min;
    out.metric_vpp = (out.i_vpp > out.q_vpp) ? out.i_vpp : out.q_vpp;

    return out;
}

/* 条件编译判断：判断 `(APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
static const char *app_signal_sweep_stage_name(uint8_t stage)
{
    /* 判断：`stage == APP_SIGDET_STAGE_VPP_FINE_SCAN`。含义：根据当前扫频阶段选择不同流程；成立后直接返回/退出当前函数或返回指定结果。 */
    if (stage == APP_SIGDET_STAGE_VPP_FINE_SCAN)
    {
        return "fine";
    }
    /* 判断：`stage == APP_SIGDET_STAGE_VPP_LOCKED`。含义：根据当前扫频阶段选择不同流程；成立后直接返回/退出当前函数或返回指定结果。 */
    if (stage == APP_SIGDET_STAGE_VPP_LOCKED)
    {
        return "locked";
    }
    return "coarse";
}
#endif

static void app_signal_sweep_begin_scan(void)
{
    /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
    memset(&g_sweep.status, 0, sizeof(g_sweep.status));
    /* 函数跳转：调用 osKernelGetTickCount()，读取 RTOS tick，用于记录扫频开始/结束或控制更新间隔。 */
    g_sweep.status.scan_start_tick_ms = osKernelGetTickCount();
    g_sweep.coarse_estimate_hz = 0UL;

    /* 函数跳转：调用 app_signal_sweep_begin_sweep()，进入一轮粗扫/细扫初始化，并把 LO 设置到第一个频点。 */
    app_signal_sweep_begin_sweep(APP_SIGDET_SCAN_START_HZ,
                                 APP_SIGDET_SCAN_STOP_HZ,
                                 APP_SIGDET_SCAN_STEP_HZ,
                                 APP_SIGDET_STAGE_VPP_COARSE_SCAN);
}

/* 启动一轮粗扫或细扫，并把 LO 设置到本轮扫描的第一个频点。 */
static void app_signal_sweep_begin_sweep(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz, uint8_t stage)
{
    uint32_t overall_start_tick = g_sweep.status.scan_start_tick_ms;

    /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
    memset(&g_sweep.vpp_table[0], 0, sizeof(g_sweep.vpp_table));

    g_sweep.scan_start_hz = start_hz;
    g_sweep.scan_stop_hz = stop_hz;
    g_sweep.scan_step_hz = step_hz;
    /* 判断：`g_sweep.scan_step_hz == 0UL`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；确认步进频率是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (g_sweep.scan_step_hz == 0UL)
    {
        g_sweep.scan_step_hz = 1UL;
    }

    g_sweep.scan_dwell_blocks = APP_SIGDET_DWELL_BLOCKS_PER_STEP;
    /* 判断：`g_sweep.scan_dwell_blocks == 0U`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (g_sweep.scan_dwell_blocks == 0U)
    {
        g_sweep.scan_dwell_blocks = 1U;
    }

    /* 清空当前频点的 dwell 累加器，准备统计新的扫描阶段。 */
    g_sweep.step_vpp_sum = 0ULL;
    g_sweep.step_i_vpp_sum = 0ULL;
    g_sweep.step_q_vpp_sum = 0ULL;
    g_sweep.step_dwell_count = 0U;

    g_sweep.status.scanning = 1U;
    g_sweep.status.carrier_present = 0U;
    g_sweep.status.locked = 0U;
    g_sweep.status.stage = stage;
    g_sweep.status.current_lo_hz = g_sweep.scan_start_hz;
    g_sweep.status.estimated_carrier_hz = g_sweep.scan_start_hz;
    g_sweep.status.raw_estimate_hz = g_sweep.scan_start_hz;
    g_sweep.status.demod_lo_hz = g_sweep.scan_start_hz;
    g_sweep.status.correction_hz = 0;
    g_sweep.status.coarse_estimate_hz = g_sweep.coarse_estimate_hz;
    g_sweep.status.current_vpp_raw = 0U;
    g_sweep.status.current_i_vpp_raw = 0U;
    g_sweep.status.current_q_vpp_raw = 0U;
    g_sweep.status.peak_vpp_raw = 0U;
    g_sweep.status.valley_vpp_raw = 0xFFFFFFFFUL;
    g_sweep.status.threshold_vpp_raw = 0U;
    g_sweep.status.left_edge_hz = 0U;
    g_sweep.status.right_edge_hz = 0U;
    g_sweep.status.step_index = 0U;
    /* 函数跳转：调用 app_signal_sweep_calc_step_count()，根据起止频率和步进计算本轮需要扫描多少个频点。 */
    g_sweep.status.step_count = app_signal_sweep_calc_step_count(g_sweep.scan_start_hz,
                                                                  g_sweep.scan_stop_hz,
                                                                  g_sweep.scan_step_hz);
    g_sweep.status.valley_step_index = APP_SIGSWEEP_INVALID_STEP;
    g_sweep.status.peak_step_index = 0U;
    g_sweep.status.scan_start_tick_ms = overall_start_tick;
    g_sweep.status.scan_finish_tick_ms = 0U;
    /* 函数跳转：调用 app_signal_sweep_set_lo()，跳到 DDS 控制层设置新的 LO 频率。 */
    app_signal_sweep_set_lo(g_sweep.status.current_lo_hz);

/* 条件编译判断：判断 `(APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
    {
        char line[128];

        /* 函数跳转：调用 snprintf()，把当前状态格式化成字符串，供后续日志输出。 */
        (void)snprintf(line,
                       sizeof(line),
                       "vpp_stage,%s\r\n",
                       /* 函数跳转：调用 app_signal_sweep_stage_name()，把阶段枚举转换成日志里可读的阶段名称。 */
                       app_signal_sweep_stage_name(stage));
        /* 函数跳转：调用 app_signal_sweep_trace_line()，把扫频日志字符串送到日志队列。 */
        app_signal_sweep_trace_line(line);

        /* 函数跳转：调用 snprintf()，把当前状态格式化成字符串，供后续日志输出。 */
        (void)snprintf(line,
                       sizeof(line),
                       "vpp_begin,%lu,%lu,%lu,%u\r\n",
                       (unsigned long)g_sweep.scan_start_hz,
                       (unsigned long)g_sweep.scan_stop_hz,
                       (unsigned long)g_sweep.scan_step_hz,
                       (unsigned)g_sweep.scan_dwell_blocks);
        /* 函数跳转：调用 app_signal_sweep_trace_line()，把扫频日志字符串送到日志队列。 */
        app_signal_sweep_trace_line(line);
        /* 函数跳转：调用 app_signal_sweep_trace_line()，把扫频日志字符串送到日志队列。 */
        app_signal_sweep_trace_line("vpp,lo_hz,total_vpp,i_vpp,q_vpp\r\n");
    }
#endif
}

/* 以粗扫估计结果为中心创建细扫窗口。 */
static void app_signal_sweep_begin_fine_scan(uint32_t center_hz)
{
    uint32_t start_hz;
    uint32_t stop_hz;

    /* 判断：`center_hz > (APP_SIGDET_SCAN_START_HZ + APP_SIGDET_FINE_SPAN_HZ)`。含义：决定是否进入下面的大括号分支；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (center_hz > (APP_SIGDET_SCAN_START_HZ + APP_SIGDET_FINE_SPAN_HZ))
    {
        start_hz = center_hz - APP_SIGDET_FINE_SPAN_HZ;
    }
    /* 否则分支：上一个 if/else if 条件不成立时走这里，执行备用路径或默认处理。 */
    else
    {
        start_hz = APP_SIGDET_SCAN_START_HZ;
    }

    /* 判断：`center_hz > (0xFFFFFFFFUL - APP_SIGDET_FINE_SPAN_HZ)`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (center_hz > (0xFFFFFFFFUL - APP_SIGDET_FINE_SPAN_HZ))
    {
        stop_hz = APP_SIGDET_SCAN_STOP_HZ;
    }
    /* 否则分支：上一个 if/else if 条件不成立时走这里，执行备用路径或默认处理。 */
    else
    {
        stop_hz = center_hz + APP_SIGDET_FINE_SPAN_HZ;
        /* 判断：`stop_hz > APP_SIGDET_SCAN_STOP_HZ`。含义：决定是否进入下面的大括号分支；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
        if (stop_hz > APP_SIGDET_SCAN_STOP_HZ)
        {
            stop_hz = APP_SIGDET_SCAN_STOP_HZ;
        }
    }

    /* 判断：`stop_hz < start_hz`。含义：确认扫描上下限是否反向；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (stop_hz < start_hz)
    {
        stop_hz = start_hz;
    }

    /* 函数跳转：调用 app_signal_sweep_begin_sweep()，进入一轮粗扫/细扫初始化，并把 LO 设置到第一个频点。 */
    app_signal_sweep_begin_sweep(start_hz,
                                 stop_hz,
                                 APP_SIGDET_FINE_STEP_HZ,
                                 APP_SIGDET_STAGE_VPP_FINE_SCAN);
}

/* 保存当前频点的平均 Vpp，并更新本轮扫频的峰值、谷值和索引。 */
static void app_signal_sweep_finish_step(uint32_t avg_vpp, uint32_t avg_i_vpp, uint32_t avg_q_vpp)
{
    uint16_t idx = g_sweep.status.step_index;

    /* 判断：`idx < APP_SIGSWEEP_MAX_STEPS`。含义：确认频点数量是否超过表容量；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (idx < APP_SIGSWEEP_MAX_STEPS)
    {
        g_sweep.vpp_table[idx] = avg_vpp;
    }

    g_sweep.status.current_vpp_raw = avg_vpp;
    g_sweep.status.current_i_vpp_raw = avg_i_vpp;
    g_sweep.status.current_q_vpp_raw = avg_q_vpp;

    /* 判断：`avg_vpp > g_sweep.status.peak_vpp_raw`。含义：判断曲线边沿、峰值或谷值索引是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (avg_vpp > g_sweep.status.peak_vpp_raw)
    {
        g_sweep.status.peak_vpp_raw = avg_vpp;
        g_sweep.status.peak_step_index = idx;
    }

    /* 判断：`(g_sweep.status.valley_step_index == APP_SIGSWEEP_INVALID_STEP) || (avg_vpp < g_sweep.status.valley_vpp_raw)`。含义：判断曲线边沿、峰值或谷值索引是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if ((g_sweep.status.valley_step_index == APP_SIGSWEEP_INVALID_STEP) ||
        (avg_vpp < g_sweep.status.valley_vpp_raw))
    {
        g_sweep.status.valley_vpp_raw = avg_vpp;
        g_sweep.status.valley_step_index = idx;
    }

/* 条件编译判断：判断 `(APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
    /* 函数跳转：调用 app_signal_sweep_trace_point()，记录当前 LO 频点的 Vpp/I/Q Vpp 曲线点。；调用 app_signal_sweep_step_freq_hz()，根据 step 索引换算当前 LO 频率。 */
    app_signal_sweep_trace_point(app_signal_sweep_step_freq_hz(g_sweep.scan_start_hz,
                                                               g_sweep.scan_step_hz,
                                                               idx),
                                 avg_vpp,
                                 avg_i_vpp,
                                 avg_q_vpp);
#endif
}

/* 根据本轮 Vpp 曲线估计载波频率，保持原有边界中点和谷值优先策略。 */
static app_signal_sweep_result_t app_signal_sweep_eval_current_sweep(void)
{
    app_signal_sweep_result_t result;
    uint32_t rise_raw;
    uint32_t threshold_delta;
    uint16_t idx;
    uint16_t first_active = APP_SIGSWEEP_INVALID_STEP;
    uint16_t last_active = APP_SIGSWEEP_INVALID_STEP;
    uint16_t valley_in_active = APP_SIGSWEEP_INVALID_STEP;
    uint32_t valley_in_active_vpp = 0xFFFFFFFFUL;

    /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
    memset(&result, 0, sizeof(result));
    result.estimated_hz = g_sweep.scan_start_hz;

    /* 判断：`g_sweep.status.valley_step_index == APP_SIGSWEEP_INVALID_STEP`。含义：判断曲线边沿、峰值或谷值索引是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (g_sweep.status.valley_step_index == APP_SIGSWEEP_INVALID_STEP)
    {
        g_sweep.status.carrier_present = 0U;
        g_sweep.status.locked = 0U;
        return result;
    }

    rise_raw = g_sweep.status.peak_vpp_raw - g_sweep.status.valley_vpp_raw;
    threshold_delta = rise_raw >> APP_SIGDET_VPP_THRESHOLD_SHIFT;
    /* 判断：`threshold_delta < APP_SIGDET_VPP_MIN_RISE_RAW`。含义：判断 Vpp 起伏/阈值是否达到载波检测要求；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (threshold_delta < APP_SIGDET_VPP_MIN_RISE_RAW)
    {
        threshold_delta = APP_SIGDET_VPP_MIN_RISE_RAW;
    }
    g_sweep.status.threshold_vpp_raw = g_sweep.status.valley_vpp_raw + threshold_delta;

    /* 判断：`rise_raw < APP_SIGDET_VPP_MIN_RISE_RAW`。含义：判断 Vpp 起伏/阈值是否达到载波检测要求；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (rise_raw < APP_SIGDET_VPP_MIN_RISE_RAW)
    {
        /* 函数跳转：调用 app_signal_sweep_step_freq_hz()，根据 step 索引换算当前 LO 频率。 */
        result.estimated_hz = app_signal_sweep_step_freq_hz(g_sweep.scan_start_hz,
                                                            g_sweep.scan_step_hz,
                                                            g_sweep.status.valley_step_index);
        g_sweep.status.carrier_present = 0U;
        g_sweep.status.locked = 0U;
        g_sweep.status.estimated_carrier_hz = result.estimated_hz;
        return result;
    }

    for (idx = 0U; idx < g_sweep.status.step_count; idx++)
    {
        /* 判断：`g_sweep.vpp_table[idx] >= g_sweep.status.threshold_vpp_raw`。含义：判断 Vpp 起伏/阈值是否达到载波检测要求；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
        if (g_sweep.vpp_table[idx] >= g_sweep.status.threshold_vpp_raw)
        {
            /* 判断：`first_active == APP_SIGSWEEP_INVALID_STEP`。含义：判断曲线边沿、峰值或谷值索引是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
            if (first_active == APP_SIGSWEEP_INVALID_STEP)
            {
                first_active = idx;
            }
            last_active = idx;
        }
    }

    /* 判断：`first_active == APP_SIGSWEEP_INVALID_STEP`。含义：判断曲线边沿、峰值或谷值索引是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (first_active == APP_SIGSWEEP_INVALID_STEP)
    {
        /* 函数跳转：调用 app_signal_sweep_step_freq_hz()，根据 step 索引换算当前 LO 频率。 */
        result.estimated_hz = app_signal_sweep_step_freq_hz(g_sweep.scan_start_hz,
                                                            g_sweep.scan_step_hz,
                                                            g_sweep.status.valley_step_index);
        g_sweep.status.carrier_present = 0U;
        g_sweep.status.locked = 0U;
        g_sweep.status.estimated_carrier_hz = result.estimated_hz;
        return result;
    }

    for (idx = first_active; idx <= last_active; idx++)
    {
        /* 判断：`g_sweep.vpp_table[idx] < valley_in_active_vpp`。含义：判断曲线边沿、峰值或谷值索引是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
        if (g_sweep.vpp_table[idx] < valley_in_active_vpp)
        {
            valley_in_active_vpp = g_sweep.vpp_table[idx];
            valley_in_active = idx;
        }
    }

    /* 函数跳转：调用 app_signal_sweep_step_freq_hz()，根据 step 索引换算当前 LO 频率。 */
    g_sweep.status.left_edge_hz = app_signal_sweep_step_freq_hz(g_sweep.scan_start_hz,
                                                                g_sweep.scan_step_hz,
                                                                first_active);
    /* 函数跳转：调用 app_signal_sweep_step_freq_hz()，根据 step 索引换算当前 LO 频率。 */
    g_sweep.status.right_edge_hz = app_signal_sweep_step_freq_hz(g_sweep.scan_start_hz,
                                                                 g_sweep.scan_step_hz,
                                                                 last_active);

    /* 判断：`(valley_in_active != APP_SIGSWEEP_INVALID_STEP) && (valley_in_active != first_active) && (valley_in_active != last_active)`。含义：判断曲线边沿、峰值或谷值索引是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if ((valley_in_active != APP_SIGSWEEP_INVALID_STEP) &&
        (valley_in_active != first_active) &&
        (valley_in_active != last_active))
    {
        /* 函数跳转：调用 app_signal_sweep_step_freq_hz()，根据 step 索引换算当前 LO 频率。 */
        result.estimated_hz = app_signal_sweep_step_freq_hz(g_sweep.scan_start_hz,
                                                            g_sweep.scan_step_hz,
                                                            valley_in_active);
    }
    /* 否则分支：上一个 if/else if 条件不成立时走这里，执行备用路径或默认处理。 */
    else
    {
        result.estimated_hz =
            (g_sweep.status.left_edge_hz / 2UL) + (g_sweep.status.right_edge_hz / 2UL) +
            ((g_sweep.status.left_edge_hz & 1UL) & (g_sweep.status.right_edge_hz & 1UL));
    }

    result.carrier_present = 1U;
    result.locked = 1U;
    g_sweep.status.carrier_present = 1U;
    g_sweep.status.locked = 0U;
    g_sweep.status.estimated_carrier_hz = result.estimated_hz;

    return result;
}

static void app_signal_sweep_finish_current_sweep(void)
{
    app_signal_sweep_result_t result;
    uint8_t stage = g_sweep.status.stage;

    /* 函数跳转：调用 app_signal_sweep_eval_current_sweep()，分析整轮 Vpp 曲线，判断是否有载波并估计频率。 */
    result = app_signal_sweep_eval_current_sweep();

/* 条件编译判断：判断 `(APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
    /* 函数跳转：调用 app_signal_sweep_trace_sweep_done()，输出本轮粗扫或细扫结束时的估计摘要。；调用 app_signal_sweep_stage_name()，把阶段枚举转换成日志里可读的阶段名称。 */
    app_signal_sweep_trace_sweep_done(app_signal_sweep_stage_name(stage));
#endif

    /* 判断：`(stage == APP_SIGDET_STAGE_VPP_COARSE_SCAN) && (result.carrier_present != 0U)`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；根据当前扫频阶段选择不同流程；确认本轮 Vpp 曲线是否已经判定存在载波；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if ((stage == APP_SIGDET_STAGE_VPP_COARSE_SCAN) && (result.carrier_present != 0U))
    {
        g_sweep.coarse_estimate_hz = result.estimated_hz;
        g_sweep.status.coarse_estimate_hz = g_sweep.coarse_estimate_hz;
        /* 函数跳转：调用 app_signal_sweep_begin_fine_scan()，根据粗扫估计中心启动细扫。 */
        app_signal_sweep_begin_fine_scan(g_sweep.coarse_estimate_hz);
        return;
    }

    /* 函数跳转：调用 app_signal_sweep_finish_locked()，结束扫频并写入最终锁定 LO。 */
    app_signal_sweep_finish_locked(result.carrier_present, result.locked, result.estimated_hz);
}

/* 完成本轮扫频：写入最终 LO、更新锁定状态，并输出原有 sigdet 日志。 */
static void app_signal_sweep_finish_locked(uint8_t carrier_present, uint8_t locked, uint32_t estimate_hz)
{
    /* 函数跳转：调用 app_signal_sweep_clamp_to_valid_hz()，把估计频率限制到有效载波频段内。 */
    uint32_t raw_hz = app_signal_sweep_clamp_to_valid_hz(estimate_hz);
    uint32_t final_hz = raw_hz;
    int32_t correction_hz;

    /* 判断：`final_hz != APP_SIGDET_VALID_STOP_HZ`。含义：确认频率是否超出最终有效范围；成立后执行下面大括号中的保护、状态更新或流程跳转。 */
    if (final_hz != APP_SIGDET_VALID_STOP_HZ)
    {
        uint32_t half_fine_step_hz = APP_SIGDET_FINE_STEP_HZ / 2UL;

        /* 判断：`(half_fine_step_hz != 0UL) && ((final_hz % APP_SIGDET_FINE_STEP_HZ) == half_fine_step_hz)`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；确认步进频率是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
        if ((half_fine_step_hz != 0UL) &&
            ((final_hz % APP_SIGDET_FINE_STEP_HZ) == half_fine_step_hz))
        {
            /* 判断：`final_hz > half_fine_step_hz`。含义：确认步进频率是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
            if (final_hz > half_fine_step_hz)
            {
                final_hz -= half_fine_step_hz;
            }
            /* 否则分支：上一个 if/else if 条件不成立时走这里，执行备用路径或默认处理。 */
            else
            {
                final_hz = 0UL;
            }
            /* 函数跳转：调用 app_signal_sweep_clamp_to_valid_hz()，把估计频率限制到有效载波频段内。 */
            final_hz = app_signal_sweep_clamp_to_valid_hz(final_hz);
        }
    }

    correction_hz = (int32_t)final_hz - (int32_t)raw_hz;

    g_sweep.status.raw_estimate_hz = raw_hz;
    g_sweep.status.demod_lo_hz = final_hz;
    g_sweep.status.correction_hz = correction_hz;
    g_sweep.status.estimated_carrier_hz = final_hz;
    g_sweep.status.current_lo_hz = final_hz;
    g_sweep.status.carrier_present = carrier_present;
    g_sweep.status.locked = (carrier_present != 0U) ? locked : 0U;
    g_sweep.status.scanning = 0U;
    g_sweep.status.stage = APP_SIGDET_STAGE_VPP_LOCKED;
    /* 函数跳转：调用 osKernelGetTickCount()，读取 RTOS tick，用于记录扫频开始/结束或控制更新间隔。 */
    g_sweep.status.scan_finish_tick_ms = osKernelGetTickCount();

    /* 函数跳转：调用 app_signal_sweep_set_lo()，跳到 DDS 控制层设置新的 LO 频率。 */
    app_signal_sweep_set_lo(final_hz);

    /* 判断：`g_uart_mode == UART_MODE_LOG`。含义：判断是否可以安全输出日志；成立后调用 snprintf()：把当前状态格式化成字符串，供后续日志输出。 */
    if (g_uart_mode == UART_MODE_LOG)
    {
        char line[125];
        uint32_t lock_ms = g_sweep.status.scan_finish_tick_ms - g_sweep.status.scan_start_tick_ms;
        /* 函数跳转：调用 snprintf()，把当前状态格式化成字符串，供后续日志输出。 */
        int n = snprintf(line,
                         sizeof(line),
                         "sigdet_final,final_lo=%lu,est=%lu,raw=%lu,corr=%ld,lk=%u,ms=%lu\r\n",
                         (unsigned long)g_sweep.status.current_lo_hz,
                         (unsigned long)g_sweep.status.estimated_carrier_hz,
                         (unsigned long)g_sweep.status.raw_estimate_hz,
                         (long)g_sweep.status.correction_hz,
                         (unsigned)g_sweep.status.locked,
                         (unsigned long)lock_ms);
        /* 判断：`(n > 0) && ((size_t)n < sizeof(line))`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；判断是否可以安全输出日志；成立后调用 print_queue_send_log()：把格式化后的日志文本送入打印队列，最终由串口/日志任务输出。 */
        if ((n > 0) && ((size_t)n < sizeof(line)))
        {
            /* 函数跳转：调用 print_queue_send_log()，把格式化后的日志文本送入打印队列，最终由串口/日志任务输出。 */
            print_queue_send_log(line);
        }

        /* 函数跳转：调用 snprintf()，把当前状态格式化成字符串，供后续日志输出。 */
        n = snprintf(line,
                     sizeof(line),
                     "sigdet_edge,l=%lu,r=%lu,p=%lu,v=%lu,t=%lu,vi=%u,pi=%u,co=%lu\r\n",
                     (unsigned long)g_sweep.status.left_edge_hz,
                     (unsigned long)g_sweep.status.right_edge_hz,
                     (unsigned long)g_sweep.status.peak_vpp_raw,
                     (unsigned long)g_sweep.status.valley_vpp_raw,
                     (unsigned long)g_sweep.status.threshold_vpp_raw,
                     (unsigned)g_sweep.status.valley_step_index,
                     (unsigned)g_sweep.status.peak_step_index,
                     (unsigned long)g_sweep.status.coarse_estimate_hz);
        /* 判断：`(n > 0) && ((size_t)n < sizeof(line))`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；判断是否可以安全输出日志；成立后调用 print_queue_send_log()：把格式化后的日志文本送入打印队列，最终由串口/日志任务输出。 */
        if ((n > 0) && ((size_t)n < sizeof(line)))
        {
            /* 函数跳转：调用 print_queue_send_log()，把格式化后的日志文本送入打印队列，最终由串口/日志任务输出。 */
            print_queue_send_log(line);
        }
    }

/* 条件编译判断：判断 `(APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
    {
        char line[144];

        /* 函数跳转：调用 snprintf()，把当前状态格式化成字符串，供后续日志输出。 */
        (void)snprintf(line,
                       sizeof(line),
                       "vpp_done,est=%lu,peak=%lu,valley=%lu,th=%lu,left=%lu,right=%lu\r\n",
                       (unsigned long)g_sweep.status.estimated_carrier_hz,
                       (unsigned long)g_sweep.status.peak_vpp_raw,
                       (unsigned long)g_sweep.status.valley_vpp_raw,
                       (unsigned long)g_sweep.status.threshold_vpp_raw,
                       (unsigned long)g_sweep.status.left_edge_hz,
                       (unsigned long)g_sweep.status.right_edge_hz);
        /* 函数跳转：调用 app_signal_sweep_trace_line()，把扫频日志字符串送到日志队列。 */
        app_signal_sweep_trace_line(line);
    }
#endif
}

/* 初始化扫频状态机，并立即从粗扫阶段开始。 */
void app_signal_sweep_init(void)
{
    /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
    memset(&g_sweep, 0, sizeof(g_sweep));
    /* 函数跳转：调用 app_signal_sweep_begin_scan()，重新初始化扫频流程，从粗扫开始。 */
    app_signal_sweep_begin_scan();
    g_sweep.inited = 1U;
}

/* 请求重新扫频；若模块尚未初始化则忽略。 */
void app_signal_sweep_request_rescan(void)
{
    /* 判断：`g_sweep.inited == 0U`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后直接返回/退出当前函数或返回指定结果。 */
    if (g_sweep.inited == 0U)
    {
        return;
    }

    /* 函数跳转：调用 app_signal_sweep_begin_scan()，重新初始化扫频流程，从粗扫开始。 */
    app_signal_sweep_begin_scan();
}

/*
 * 扫频 block 入口。
 * 每个频点累计 scan_dwell_blocks 个 ADC block 后计算平均 Vpp，
 * 再切换到下一个 LO 频点，或结束当前粗扫/细扫阶段。
 */
void app_signal_sweep_process_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt)
{
    app_signal_sweep_vpp_t vpp;
    uint32_t avg_vpp;
    uint32_t avg_i_vpp;
    uint32_t avg_q_vpp;

    /* 判断：`(g_sweep.inited == 0U) || (g_sweep.status.scanning == 0U)`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后直接返回/退出当前函数或返回指定结果。 */
    if ((g_sweep.inited == 0U) || (g_sweep.status.scanning == 0U))
    {
        return;
    }

    /* 函数跳转：调用 app_signal_sweep_block_vpp()，统计当前 ADC block 的 I/Q 峰峰值，返回本频点强度。 */
    vpp = app_signal_sweep_block_vpp(i_buf, q_buf, sample_cnt);
    g_sweep.step_vpp_sum += vpp.metric_vpp;
    g_sweep.step_i_vpp_sum += vpp.i_vpp;
    g_sweep.step_q_vpp_sum += vpp.q_vpp;
    g_sweep.step_dwell_count++;

    /* 判断：`g_sweep.step_dwell_count < g_sweep.scan_dwell_blocks`。含义：决定是否进入下面的大括号分支；成立后直接返回/退出当前函数或返回指定结果。 */
    if (g_sweep.step_dwell_count < g_sweep.scan_dwell_blocks)
    {
        return;
    }

    avg_vpp = (uint32_t)(g_sweep.step_vpp_sum / g_sweep.step_dwell_count);
    avg_i_vpp = (uint32_t)(g_sweep.step_i_vpp_sum / g_sweep.step_dwell_count);
    avg_q_vpp = (uint32_t)(g_sweep.step_q_vpp_sum / g_sweep.step_dwell_count);
    /* 函数跳转：调用 app_signal_sweep_finish_step()，保存当前频点平均 Vpp，并更新峰值/谷值/索引。 */
    app_signal_sweep_finish_step(avg_vpp, avg_i_vpp, avg_q_vpp);

    g_sweep.step_vpp_sum = 0ULL;
    g_sweep.step_i_vpp_sum = 0ULL;
    g_sweep.step_q_vpp_sum = 0ULL;
    g_sweep.step_dwell_count = 0U;

    /* 判断：`(g_sweep.status.step_index + 1U) < g_sweep.status.step_count`。含义：决定是否进入下面的大括号分支；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if ((g_sweep.status.step_index + 1U) < g_sweep.status.step_count)
    {
        g_sweep.status.step_index++;
        /* 函数跳转：调用 app_signal_sweep_step_freq_hz()，根据 step 索引换算当前 LO 频率。 */
        g_sweep.status.current_lo_hz = app_signal_sweep_step_freq_hz(g_sweep.scan_start_hz,
                                                                      g_sweep.scan_step_hz,
                                                                      g_sweep.status.step_index);
        /* 函数跳转：调用 app_signal_sweep_set_lo()，跳到 DDS 控制层设置新的 LO 频率。 */
        app_signal_sweep_set_lo(g_sweep.status.current_lo_hz);
        return;
    }

    /* 函数跳转：调用 app_signal_sweep_finish_current_sweep()，结束当前粗扫/细扫阶段，决定进入细扫还是最终锁定。 */
    app_signal_sweep_finish_current_sweep();
}

/* 返回当前扫频状态快照。 */
void app_signal_sweep_get_status(app_signal_detect_status_t *status_out)
{
    /* 判断：`status_out == NULL`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后直接返回/退出当前函数或返回指定结果。 */
    if (status_out == NULL)
    {
        return;
    }

    *status_out = g_sweep.status;
}
