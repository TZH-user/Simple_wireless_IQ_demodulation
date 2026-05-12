#include "app_signal_detect.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "cmsis_os2.h"
#include "adc.h"
#include "app_adc_log.h"
#include "app_carrier_sync.h"
#include "app_iq_preproc.h"
#include "app_signal_sweep.h"

/* 宏定义说明：IQ_PREPROC_LOG_PERIOD_MS = 1000U；IQ 预处理跟踪日志的最小打印周期，避免串口刷屏。 */
#define IQ_PREPROC_LOG_PERIOD_MS 1000U

/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef ADC_IQ_PREPROC_ENABLE
/* 宏定义说明：ADC_IQ_PREPROC_ENABLE = 1U；ADC IQ 预处理总开关，1 表示启用去直流/校正/可选旋转流程。 */
#define ADC_IQ_PREPROC_ENABLE 1U
#endif

/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef ADC_IQ_PREPROC_ROTATE_RUNTIME_ENABLE
/* 默认只做锁定后的 IQ 频偏分析，不切入运行时 IQ 旋转输出路径。 */
/* 宏定义说明：ADC_IQ_PREPROC_ROTATE_RUNTIME_ENABLE = 0U；运行时 IQ 相位旋转校正开关，0 表示只分析不在线旋转。 */
#define ADC_IQ_PREPROC_ROTATE_RUNTIME_ENABLE 0U
#endif

/* 宏定义说明：IQ_PREPROC_VERIFY_WINDOW_SAMPLES = 32U；用于抽样验证 IQ 预处理效果的窗口样本数。 */
#define IQ_PREPROC_VERIFY_WINDOW_SAMPLES 32U

/* 条件编译判断：判断 `(ADC_IQ_PREPROC_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (ADC_IQ_PREPROC_ENABLE != 0U)
static app_iq_preproc_ctx_t iq_preproc_ctx;             /* IQ 预处理上下文，保存跨 block 的残余频偏估计状态。 */
static app_iq_preproc_result_t iq_preproc_result;       /* 最近一次 IQ 预处理结果，供日志和 VRFE 闭环使用。 */
static uint8_t iq_preproc_tracking_active = 0U;         /* 1 表示当前处于锁定后的 IQ 跟踪窗口。 */
/* 条件编译判断：判断 `(ADC_IQ_PREPROC_ROTATE_RUNTIME_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (ADC_IQ_PREPROC_ROTATE_RUNTIME_ENABLE != 0U)
static uint16_t iq_preproc_i_buf[ADC_BLOCK_N];          /* 运行时旋转模式下的 I 路输出缓冲区。 */
static uint16_t iq_preproc_q_buf[ADC_BLOCK_N];          /* 运行时旋转模式下的 Q 路输出缓冲区。 */
#endif
static app_adc_log_iq_verify_snapshot_t iq_preproc_verify; /* IQ 预处理验证快照，用于串口低频日志。 */
#endif

/* 条件编译判断：判断 `(ADC_IQ_PREPROC_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (ADC_IQ_PREPROC_ENABLE != 0U)
/* 将相位限制到 [-pi, pi]，避免跨越正负 pi 时出现相位突跳。 */
static float app_signal_wrap_pi(float angle)
{
    const float pi = 3.14159265359f;
    const float two_pi = 6.28318530718f;

    while (angle > pi)
    {
        angle -= two_pi;
    }
    while (angle < -pi)
    {
        angle += two_pi;
    }

    return angle;
}

/* 计算一段原始 IQ 样本的平均相位和平均矢量幅度。 */
static uint8_t app_signal_calc_edge_phase_u16(const uint16_t *i_buf,
                                              const uint16_t *q_buf,
                                              uint32_t start_index,
                                              uint32_t sample_cnt,
                                              uint16_t adc_mid,
                                              float *phase_out,
                                              uint32_t *mag_out)
{
    int64_t sum_i = 0;
    int64_t sum_q = 0;

    /* 判断：`(i_buf == NULL) || (q_buf == NULL) || (phase_out == NULL) || (mag_out == NULL)`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后直接返回/退出当前函数或返回指定结果。 */
    if ((i_buf == NULL) || (q_buf == NULL) || (phase_out == NULL) || (mag_out == NULL))
    {
        return 0U;
    }

    for (uint32_t idx = 0U; idx < sample_cnt; idx++)
    {
        sum_i += (int32_t)i_buf[start_index + idx] - (int32_t)adc_mid;
        sum_q += (int32_t)q_buf[start_index + idx] - (int32_t)adc_mid;
    }

    /* 判断：`(sum_i == 0) && (sum_q == 0)`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后直接返回/退出当前函数或返回指定结果。 */
    if ((sum_i == 0) && (sum_q == 0))
    {
        *phase_out = 0.0f;
        *mag_out = 0U;
        return 0U;
    }

    *phase_out = atan2f((float)sum_q, (float)sum_i);
    *mag_out = (uint32_t)sqrtf((float)((sum_i * sum_i) + (sum_q * sum_q)));

    return 1U;
}

/* 按当前残余频偏估计做虚拟反旋转，再计算边缘窗口平均相位。 */
static uint8_t app_signal_calc_edge_phase_virtual_rot_u16(const app_iq_preproc_ctx_t *ctx,
                                                          const uint16_t *i_buf,
                                                          const uint16_t *q_buf,
                                                          uint32_t start_index,
                                                          uint32_t sample_cnt,
                                                          float phase0,
                                                          float phase_step,
                                                          float *phase_out,
                                                          uint32_t *mag_out)
{
    /* 函数跳转：调用 cosf()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
    float c = cosf(phase0 + ((float)start_index * phase_step));
    /* 函数跳转：调用 sinf()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
    float s = sinf(phase0 + ((float)start_index * phase_step));
    /* 函数跳转：调用 cosf()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
    float c_step = cosf(phase_step);
    /* 函数跳转：调用 sinf()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
    float s_step = sinf(phase_step);
    float sum_i = 0.0f;
    float sum_q = 0.0f;

    /* 判断：`(ctx == NULL) || (i_buf == NULL) || (q_buf == NULL) || (phase_out == NULL) || (mag_out == NULL)`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后直接返回/退出当前函数或返回指定结果。 */
    if ((ctx == NULL) || (i_buf == NULL) || (q_buf == NULL) || (phase_out == NULL) || (mag_out == NULL))
    {
        return 0U;
    }

    for (uint32_t idx = 0U; idx < sample_cnt; idx++)
    {
        float i = (float)((int32_t)i_buf[start_index + idx] - (int32_t)ctx->adc_mid);
        float q = (float)((int32_t)q_buf[start_index + idx] - (int32_t)ctx->adc_mid);
        float rot_i = (c * i) + (s * q);
        float rot_q = (-s * i) + (c * q);
        float next_c = (c * c_step) - (s * s_step);
        float next_s = (s * c_step) + (c * s_step);

        sum_i += rot_i;
        sum_q += rot_q;
        c = next_c;
        s = next_s;
    }

    /* 判断：`(sum_i == 0.0f) && (sum_q == 0.0f)`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后直接返回/退出当前函数或返回指定结果。 */
    if ((sum_i == 0.0f) && (sum_q == 0.0f))
    {
        *phase_out = 0.0f;
        *mag_out = 0U;
        return 0U;
    }

    *phase_out = atan2f(sum_q, sum_i);
    *mag_out = (uint32_t)(sqrtf((sum_i * sum_i) + (sum_q * sum_q)) + 0.5f);

    return 1U;
}

/* 生成 IQ 预处理验证快照，用于串口日志观察旋转前后的边缘相位差。 */
static void app_signal_iq_preproc_update_verify(const uint16_t *raw_i_buf,
                                                const uint16_t *raw_q_buf,
                                                const app_iq_preproc_ctx_t *ctx,
                                                uint32_t sample_cnt)
{
    const uint32_t edge_n = IQ_PREPROC_VERIFY_WINDOW_SAMPLES;
    float raw_head_phase;
    float raw_tail_phase;
    float rot_head_phase;
    float rot_tail_phase;
    uint32_t raw_head_mag;
    uint32_t raw_tail_mag;
    uint32_t rot_head_mag;
    uint32_t rot_tail_mag;
    float raw_dphi;
    float rot_dphi;
    float phase0;
    float phase_step;
    int32_t raw_abs;

    iq_preproc_verify.valid = 0U;

    /* 判断：`(ctx == NULL) || (sample_cnt < (edge_n * 2U))`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；判断样本数量是否足够进行当前统计；成立后直接返回/退出当前函数或返回指定结果。 */
    if ((ctx == NULL) || (sample_cnt < (edge_n * 2U)))
    {
        return;
    }

    phase_step = ctx->residual_rad_per_sample;
    /* 函数跳转：调用 app_signal_wrap_pi()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
    phase0 = app_signal_wrap_pi(ctx->phase_acc_rad - (phase_step * (float)sample_cnt));

    /* 判断：`(app_signal_calc_edge_phase_u16(raw_i_buf, raw_q_buf, 0U, edge_n, ctx->adc_mid, &raw_head_phase, &raw_head_mag) == 0U) || (app_signal_calc_edge_phase_u16(raw_i_buf, raw_q_buf, sample_cnt - edge_n, edge_n, ctx->adc_mid, &raw_tail_phase, &raw_tail_mag) == 0U) || (app_signal_calc_edge_phase_virtual_rot_u16(ctx, raw_i_buf, raw_q_buf, 0U, edge_n, phase0, phase_step, &rot_head_phase, &rot_head_mag) == 0U) || (app_signal_calc_edge_phase_virtual_rot_u16(ctx, raw_i_buf, raw_q_buf, sample_cnt - edge_n, edge_n, phase0, phase_step, &rot_tail_phase, &rot_tail_mag) == 0U)`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；判断样本数量是否足够进行当前统计；成立后直接返回/退出当前函数或返回指定结果。 */
    if ((app_signal_calc_edge_phase_u16(raw_i_buf, raw_q_buf, 0U, edge_n, ctx->adc_mid, &raw_head_phase, &raw_head_mag) == 0U) ||
        /* 函数跳转：调用 app_signal_calc_edge_phase_u16()，计算一段原始 u16 IQ 样本的平均相位和平均幅度。 */
        (app_signal_calc_edge_phase_u16(raw_i_buf, raw_q_buf, sample_cnt - edge_n, edge_n, ctx->adc_mid, &raw_tail_phase, &raw_tail_mag) == 0U) ||
        /* 函数跳转：调用 app_signal_calc_edge_phase_virtual_rot_u16()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
        (app_signal_calc_edge_phase_virtual_rot_u16(ctx, raw_i_buf, raw_q_buf, 0U, edge_n, phase0, phase_step, &rot_head_phase, &rot_head_mag) == 0U) ||
        /* 函数跳转：调用 app_signal_calc_edge_phase_virtual_rot_u16()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
        (app_signal_calc_edge_phase_virtual_rot_u16(ctx, raw_i_buf, raw_q_buf, sample_cnt - edge_n, edge_n, phase0, phase_step, &rot_tail_phase, &rot_tail_mag) == 0U))
    {
        return;
    }

    /* 函数跳转：调用 app_signal_wrap_pi()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
    raw_dphi = app_signal_wrap_pi(raw_tail_phase - raw_head_phase);
    /* 函数跳转：调用 app_signal_wrap_pi()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
    rot_dphi = app_signal_wrap_pi(rot_tail_phase - rot_head_phase);

    iq_preproc_verify.raw_dphi_urad = (int32_t)(raw_dphi * 1000000.0f);
    iq_preproc_verify.rot_dphi_urad = (int32_t)(rot_dphi * 1000000.0f);
    iq_preproc_verify.raw_head_mag = raw_head_mag;
    iq_preproc_verify.raw_tail_mag = raw_tail_mag;
    iq_preproc_verify.rot_head_mag = rot_head_mag;
    iq_preproc_verify.rot_tail_mag = rot_tail_mag;
    iq_preproc_verify.overrun_snapshot = adc_overrun_cnt;
    iq_preproc_verify.rotate_block_cnt_snapshot = app_iq_preproc_rotate_block_cnt;

    raw_abs = (iq_preproc_verify.raw_dphi_urad >= 0) ? iq_preproc_verify.raw_dphi_urad : -iq_preproc_verify.raw_dphi_urad;
    /* 判断：`raw_abs > 0`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (raw_abs > 0)
    {
        int32_t rot_abs = (iq_preproc_verify.rot_dphi_urad >= 0) ? iq_preproc_verify.rot_dphi_urad : -iq_preproc_verify.rot_dphi_urad;
        iq_preproc_verify.improve_pm = (int32_t)(((int64_t)(raw_abs - rot_abs) * 1000LL) / (int64_t)raw_abs);
    }
    /* 否则分支：上一个 if/else if 条件不成立时走这里，执行备用路径或默认处理。 */
    else
    {
        iq_preproc_verify.improve_pm = 0;
    }

    iq_preproc_verify.valid = 1U;
}

/* 清空 IQ 预处理历史，确保下一次锁定后从新的相位窗口重新估计。 */
static void app_signal_iq_preproc_reset_tracking(void)
{
    /* 函数跳转：调用 app_iq_preproc_reset()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
    app_iq_preproc_reset(&iq_preproc_ctx);
    /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
    memset(&iq_preproc_result, 0, sizeof(iq_preproc_result));
    /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
    memset(&iq_preproc_verify, 0, sizeof(iq_preproc_verify));
    iq_preproc_tracking_active = 0U;
}

/* 只在扫频锁定后分析残余频偏，并把结果交给 OCXO VRFE 慢速闭环。 */
static void app_signal_iq_preproc_process_block(const uint16_t *i_buf,
                                                const uint16_t *q_buf,
                                                uint32_t sample_cnt,
                                                uint8_t locked_gate)
{
    static uint32_t last_verify_tick = 0U;
    uint32_t now_tick;
    uint8_t analyzed = 0U;

    /* 判断：`locked_gate != 0U`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；确认扫频锁定门控是否允许后级处理；成立后调用 app_iq_preproc_reset()：执行该步骤对应的子流程。 */
    if (locked_gate != 0U)
    {
        /* 判断：`iq_preproc_tracking_active == 0U`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后调用 app_iq_preproc_reset()：执行该步骤对应的子流程。 */
        if (iq_preproc_tracking_active == 0U)
        {
            /* 函数跳转：调用 app_iq_preproc_reset()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
            app_iq_preproc_reset(&iq_preproc_ctx);
            /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
            memset(&iq_preproc_result, 0, sizeof(iq_preproc_result));
            /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
            memset(&iq_preproc_verify, 0, sizeof(iq_preproc_verify));
            iq_preproc_tracking_active = 1U;
        }

/* 条件编译判断：判断 `(ADC_IQ_PREPROC_ROTATE_RUNTIME_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (ADC_IQ_PREPROC_ROTATE_RUNTIME_ENABLE != 0U)
        /* 函数跳转：调用 app_iq_preproc_rotate_block_u16()，对整块 IQ 采样做运行时相位旋转校正。 */
        analyzed = app_iq_preproc_rotate_block_u16(&iq_preproc_ctx,
                                                   i_buf,
                                                   q_buf,
                                                   sample_cnt,
                                                   iq_preproc_i_buf,
                                                   iq_preproc_q_buf,
                                                   &iq_preproc_result);
/* 条件编译否则分支：前面的 #if/#elif 都不成立时编译下面代码块。 */
#else
        /* 函数跳转：调用 app_iq_preproc_analyze_block_u16()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
        analyzed = app_iq_preproc_analyze_block_u16(&iq_preproc_ctx,
                                                    i_buf,
                                                    q_buf,
                                                    sample_cnt,
                                                    &iq_preproc_result);
#endif
        /* 判断：`analyzed != 0U`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
        if (analyzed != 0U)
        {
            app_carrier_sync_status_t carrier_status;

            /* 函数跳转：调用 osKernelGetTickCount()，读取 RTOS tick，用于记录扫频开始/结束或控制更新间隔。 */
            now_tick = osKernelGetTickCount();
            /* 判断：`(uint32_t)(now_tick - last_verify_tick) >= IQ_PREPROC_LOG_PERIOD_MS`。含义：决定是否进入下面的大括号分支；成立后调用 app_signal_iq_preproc_update_verify()：执行该步骤对应的子流程。 */
            if ((uint32_t)(now_tick - last_verify_tick) >= IQ_PREPROC_LOG_PERIOD_MS)
            {
                /* 函数跳转：调用 app_signal_iq_preproc_update_verify()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
                app_signal_iq_preproc_update_verify(i_buf, q_buf, &iq_preproc_ctx, sample_cnt);
                last_verify_tick = now_tick;
            }

            /* 函数跳转：调用 app_adc_log_iq_preproc_1s()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
            app_adc_log_iq_preproc_1s(iq_preproc_tracking_active,
                                      &iq_preproc_result,
                                      &iq_preproc_verify);
            /* 函数跳转：调用 app_adc_log_iq_amp_phase_1s()，锁定后每秒输出 I/Q 两路幅度和 atan2(Q,I) 相位，便于串口观察相位是否靠近 0。 */
            app_adc_log_iq_amp_phase_1s(iq_preproc_tracking_active,
                                        &iq_preproc_result);
            /* 函数跳转：调用 app_carrier_sync_update_iq()，在频率锁定后用原始 IQ block 计算逐点相位并选择频率/相位闭环。 */
            app_carrier_sync_update_iq(1U,
                                       i_buf,
                                       q_buf,
                                       sample_cnt,
                                       iq_preproc_ctx.adc_mid,
                                       &iq_preproc_result);
            /* 函数跳转：调用 app_carrier_sync_get_status()，读取 VRFE 闭环模式、电压和相位误差给串口日志。 */
            app_carrier_sync_get_status(&carrier_status);
            /* 函数跳转：调用 app_adc_log_carrier_sync_1s()，每秒输出频率/相位闭环状态，便于判断是否已相位接管。 */
            app_adc_log_carrier_sync_1s(&carrier_status);
        }
        return;
    }

    /* 判断：`iq_preproc_tracking_active != 0U`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后调用 app_signal_iq_preproc_reset_tracking()：复位本文件维护的 IQ 预处理跟踪状态。 */
    if (iq_preproc_tracking_active != 0U)
    {
        /* 函数跳转：调用 app_signal_iq_preproc_reset_tracking()，复位本文件维护的 IQ 预处理跟踪状态。 */
        app_signal_iq_preproc_reset_tracking();
    }
    /* 函数跳转：调用 app_carrier_sync_update()，根据 IQ 预处理得到的残余频偏更新 DAC 控制量。 */
    app_carrier_sync_update(0U, NULL);
}
#endif

/* 初始化 Signal 门面层，统一启动扫频、VRFE 控制和 IQ 预处理上下文。 */
void app_signal_detect_init(void)
{
    /* 函数跳转：调用 app_signal_sweep_init()，初始化扫频模块并启动首次粗扫。 */
    app_signal_sweep_init();
    /* 函数跳转：调用 app_carrier_sync_init()，初始化载波同步状态并输出中心 DAC 电压。 */
    app_carrier_sync_init();
/* 条件编译判断：判断 `(ADC_IQ_PREPROC_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (ADC_IQ_PREPROC_ENABLE != 0U)
    /* 函数跳转：调用 app_iq_preproc_init()，初始化 IQ 预处理上下文，包括偏置/校正/残余频偏状态。 */
    app_iq_preproc_init(&iq_preproc_ctx, APP_IQ_PREPROC_DEFAULT_SAMPLE_RATE_HZ);
    /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
    memset(&iq_preproc_result, 0, sizeof(iq_preproc_result));
    /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
    memset(&iq_preproc_verify, 0, sizeof(iq_preproc_verify));
    iq_preproc_tracking_active = 0U;
#endif
}

/* 请求重新扫频，同时复位锁定后才允许运行的分析和闭环状态。 */
void app_signal_detect_request_rescan(void)
{
    /* 函数跳转：调用 app_signal_sweep_request_rescan()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
    app_signal_sweep_request_rescan();
/* 条件编译判断：判断 `(ADC_IQ_PREPROC_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (ADC_IQ_PREPROC_ENABLE != 0U)
    /* 函数跳转：调用 app_signal_iq_preproc_reset_tracking()，复位本文件维护的 IQ 预处理跟踪状态。 */
    app_signal_iq_preproc_reset_tracking();
#endif
    /* 函数跳转：调用 app_carrier_sync_reset_to_center()，把载波同步控制电压复位到中心值。 */
    app_carrier_sync_reset_to_center();
}

/* Signal 层统一 block 入口：先扫频更新状态，再根据锁定门控决定是否运行 IQ 分析和 VRFE 闭环。 */
void app_signal_pipeline_process_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt)
{
    app_signal_detect_status_t status;
    uint8_t locked_gate;

    /* 函数跳转：调用 app_signal_sweep_process_block()，把 ADC block 送入扫频状态机，累计 dwell 并推进频点。 */
    app_signal_sweep_process_block(i_buf, q_buf, sample_cnt);
    /* 函数跳转：调用 app_signal_sweep_get_status()，读取扫频状态快照给外部模块/界面使用。 */
    app_signal_sweep_get_status(&status);

    locked_gate = ((status.stage == APP_SIGDET_STAGE_VPP_LOCKED) &&
                   (status.locked != 0U) &&
                   (status.carrier_present != 0U)) ? 1U : 0U;

/* 条件编译判断：判断 `(ADC_IQ_PREPROC_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (ADC_IQ_PREPROC_ENABLE != 0U)
    /* 函数跳转：调用 app_signal_iq_preproc_process_block()，在扫频锁定后执行 IQ 预处理/验证/载波同步相关流程。 */
    app_signal_iq_preproc_process_block(i_buf, q_buf, sample_cnt, locked_gate);
/* 条件编译否则分支：前面的 #if/#elif 都不成立时编译下面代码块。 */
#else
    /* 函数跳转：调用 app_carrier_sync_update()，根据 IQ 预处理得到的残余频偏更新 DAC 控制量。 */
    app_carrier_sync_update(locked_gate, NULL);
#endif
}

/* 兼容旧接口名，当前直接转到统一 pipeline 入口。 */
void app_signal_detect_process_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt)
{
    /* 函数跳转：调用 app_signal_pipeline_process_block()，总入口：先扫频/预处理，再送后级检测。 */
    app_signal_pipeline_process_block(i_buf, q_buf, sample_cnt);
}

/* 获取当前扫频/锁定状态快照。 */
void app_signal_detect_get_status(app_signal_detect_status_t *status_out)
{
    /* 函数跳转：调用 app_signal_sweep_get_status()，读取扫频状态快照给外部模块/界面使用。 */
    app_signal_sweep_get_status(status_out);
}

/* 条件编译判断：判断 `0` 是否成立；成立时才编译下面代码块。 */
#if 0
/*
 * 以下为本次拆分前的旧实现迁移参考，当前不参与编译。
 * 后续确认新模块稳定后，可在获得明确授权后再清理这段历史代码。
 */
#include "app_signal_detect.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "cmsis_os2.h"
#include "app_dds_ctrl.h"
#include "RtosTypes.h"

/* 宏定义说明：APP_SIGDET_MAX_STEPS = 512U；旧版/兼容信号检测扫频 Vpp 表最大频点数，防止数组越界。 */
#define APP_SIGDET_MAX_STEPS 512U
/* 宏定义说明：APP_SIGDET_INVALID_STEP = 0xFFFFU；旧版/兼容信号检测中表示无效频点索引的哨兵值。 */
#define APP_SIGDET_INVALID_STEP 0xFFFFU

typedef struct
{
  uint32_t metric_vpp;                        /* 综合 Vpp 指标，取 I 路和 Q 路 Vpp 的较大值，用于评估信号强度。 */
  uint32_t i_vpp;                             /* I 路 Vpp，即 I 路采样数据的峰峰值，用于分析信号特征和排查硬件问题。 */
  uint32_t q_vpp;                             /* Q 路 Vpp，即 Q 路采样数据的峰峰值，用于分析信号特征和排查硬件问题。 */
} app_signal_detect_vpp_t;

typedef struct
{
  uint8_t carrier_present;                    /* 是否检测到载波存在 */
  uint8_t locked;                             /* 是否锁定 */
  uint32_t estimated_hz;                      /* 估计频率 */
} app_signal_detect_sweep_result_t;   /* 扫频结果，包含载波存在/锁定标志和频率估计 */

typedef struct
{
  app_signal_detect_status_t status;          /* 当前状态，包含阶段、锁定/载波存在标志、频率估计和相关统计信息 */
  uint32_t vpp_table[APP_SIGDET_MAX_STEPS];   /* 扫描过程中每个频点的 Vpp 结果，用于后续分析和边缘估计 */
  uint64_t step_vpp_sum;                      /* 当前频点 dwell 内 metric_vpp 的累加和。 */
  uint64_t step_i_vpp_sum;                    /* 当前频点 dwell 内 I 路 Vpp 的累加和。 */
  uint64_t step_q_vpp_sum;                    /* 当前频点 dwell 内 Q 路 Vpp 的累加和。 */
  uint32_t coarse_estimate_hz;                /* 粗略频率估计 */
  uint16_t step_dwell_count;                  /* 当前频点已累计的 ADC block 数。 */
  uint16_t scan_dwell_blocks;                 /* 每个频点需要累计的 ADC block 数。 */
  uint32_t scan_start_hz;                     /* 扫描起始频率 */
  uint32_t scan_stop_hz;                      /* 扫描停止频率 */
  uint32_t scan_step_hz;                      /* 扫描步进频率 */
  uint8_t inited;                             /* 初始化标志 */
} app_signal_detect_ctx_t;

static app_signal_detect_ctx_t g_sigdet;

static uint16_t app_signal_detect_calc_step_count(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz);
static uint32_t app_signal_detect_step_freq_hz(uint32_t start_hz, uint32_t step_hz, uint16_t step_index);
/* 将估计频率限制在当前配置允许的载波范围内。 */
static uint32_t app_signal_detect_clamp_to_valid_hz(uint32_t hz);
/* 通过 DDS 控制层写入新的 LO 频率。 */
static void app_signal_detect_set_lo(uint32_t lo_hz);
static app_signal_detect_vpp_t app_signal_detect_block_vpp(const uint16_t *i_buf,
                                                           const uint16_t *q_buf,
                                                           uint32_t sample_cnt);

static void app_signal_detect_begin_scan(void);
static void app_signal_detect_begin_sweep(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz, uint8_t stage);
static void app_signal_detect_begin_fine_scan(uint32_t center_hz);
static void app_signal_detect_finish_step(uint32_t avg_vpp, uint32_t avg_i_vpp, uint32_t avg_q_vpp);
static app_signal_detect_sweep_result_t app_signal_detect_eval_current_sweep(void);
static void app_signal_detect_finish_current_sweep(void);
static void app_signal_detect_finish_locked(uint8_t carrier_present, uint8_t locked, uint32_t estimate_hz);
/* 条件编译判断：判断 `(APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
static void app_signal_detect_trace_line(const char *text);
static void app_signal_detect_trace_point(uint32_t lo_hz, uint32_t vpp, uint32_t i_vpp, uint32_t q_vpp);
static void app_signal_detect_trace_sweep_done(const char *name);
#endif

/* 条件编译判断：判断 `(APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
static void app_signal_detect_trace_line(const char *text)
{
  /* 判断：`text != 0`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后调用 print_queue_send_log()：把格式化后的日志文本送入打印队列，最终由串口/日志任务输出。 */
  if (text != 0)
  {
    /* 函数跳转：调用 print_queue_send_log()，把格式化后的日志文本送入打印队列，最终由串口/日志任务输出。 */
    print_queue_send_log(text);
  }
}

static void app_signal_detect_trace_point(uint32_t lo_hz, uint32_t vpp, uint32_t i_vpp, uint32_t q_vpp)
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
  /* 函数跳转：调用 app_signal_detect_trace_line()，旧版/兼容逻辑输出扫频日志行。 */
  app_signal_detect_trace_line(line);
}

static void app_signal_detect_trace_sweep_done(const char *name)
{
  char line[144];

  /* 函数跳转：调用 snprintf()，把当前状态格式化成字符串，供后续日志输出。 */
  (void)snprintf(line,
                 sizeof(line),
                 "vpp_%s_done,est=%lu,peak=%lu,valley=%lu,th=%lu,left=%lu,right=%lu\r\n",
                 name,
                 (unsigned long)g_sigdet.status.estimated_carrier_hz,
                 (unsigned long)g_sigdet.status.peak_vpp_raw,
                 (unsigned long)g_sigdet.status.valley_vpp_raw,
                 (unsigned long)g_sigdet.status.threshold_vpp_raw,
                 (unsigned long)g_sigdet.status.left_edge_hz,
                 (unsigned long)g_sigdet.status.right_edge_hz);
  /* 函数跳转：调用 app_signal_detect_trace_line()，旧版/兼容逻辑输出扫频日志行。 */
  app_signal_detect_trace_line(line);
}
#endif

static uint16_t app_signal_detect_calc_step_count(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz)
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
  /* 判断：`steps_u32 > APP_SIGDET_MAX_STEPS`。含义：确认频点数量是否超过表容量；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
  if (steps_u32 > APP_SIGDET_MAX_STEPS)
  {
    steps_u32 = APP_SIGDET_MAX_STEPS;
  }

  return (uint16_t)steps_u32;
}

/* 根据起始频率、步进频率和步骤索引计算当前扫频点。 */
static uint32_t app_signal_detect_step_freq_hz(uint32_t start_hz, uint32_t step_hz, uint16_t step_index)
{
  return start_hz + ((uint32_t)step_index * step_hz);
}

static uint32_t app_signal_detect_clamp_to_valid_hz(uint32_t hz)
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

static void app_signal_detect_set_lo(uint32_t lo_hz)
{
  AppDdsCmd cmd;

  /* 函数跳转：调用 AppDDS_MakeSetChFreqApplyCmd()，生成 DDS 设置某通道频率并立即应用的命令。 */
  cmd = AppDDS_MakeSetChFreqApplyCmd((uint8_t)APP_SIGDET_DDS_CHANNEL, lo_hz);
  /* 函数跳转：调用 AppDDS_DispatchCmd()，把 DDS 命令派发到 DDS 控制层，由底层完成真正写频。 */
  (void)AppDDS_DispatchCmd(&cmd);
}

/* 统计一个 ADC block 的 I/Q 峰峰值，并取较大一路作为扫频强度指标。 */
static app_signal_detect_vpp_t app_signal_detect_block_vpp(const uint16_t *i_buf,
                                                           const uint16_t *q_buf,
                                                           uint32_t sample_cnt)
{
  app_signal_detect_vpp_t out;
  uint32_t idx;
  uint16_t i_min;
  uint16_t i_max;
  uint16_t q_min;
  uint16_t q_max;

  /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
  memset(&out, 0, sizeof(out));

  /* 判断：`(i_buf == 0) || (q_buf == 0) || (sample_cnt == 0U)`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；判断样本数量是否足够进行当前统计；成立后直接返回/退出当前函数或返回指定结果。 */
  if ((i_buf == 0) || (q_buf == 0) || (sample_cnt == 0U))
  {
    return out;
  }

  i_min = i_buf[0];   i_max = i_buf[0];
  q_min = q_buf[0];   q_max = q_buf[0];

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
static const char *app_signal_detect_stage_name(uint8_t stage)
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

static void app_signal_detect_begin_scan(void)
{
  /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
  memset(&g_sigdet.status, 0, sizeof(g_sigdet.status));
  /* 函数跳转：调用 osKernelGetTickCount()，读取 RTOS tick，用于记录扫频开始/结束或控制更新间隔。 */
  g_sigdet.status.scan_start_tick_ms = osKernelGetTickCount();
  g_sigdet.coarse_estimate_hz = 0UL;

  /* 函数跳转：调用 app_signal_detect_begin_sweep()，旧版/兼容扫频状态机进入粗扫或细扫。 */
  app_signal_detect_begin_sweep(APP_SIGDET_SCAN_START_HZ,
                                APP_SIGDET_SCAN_STOP_HZ,
                                APP_SIGDET_SCAN_STEP_HZ,
                                APP_SIGDET_STAGE_VPP_COARSE_SCAN);
}

static void app_signal_detect_begin_sweep(uint32_t start_hz, uint32_t stop_hz, uint32_t step_hz, uint8_t stage)
{
  uint32_t overall_start_tick = g_sigdet.status.scan_start_tick_ms;

  /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
  memset(&g_sigdet.vpp_table[0], 0, sizeof(g_sigdet.vpp_table));

  g_sigdet.scan_start_hz = start_hz;
  g_sigdet.scan_stop_hz = stop_hz;
  g_sigdet.scan_step_hz = step_hz;
  /* 判断：`g_sigdet.scan_step_hz == 0UL`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；确认步进频率是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
  if (g_sigdet.scan_step_hz == 0UL)
  {
    g_sigdet.scan_step_hz = 1UL;
  }

  g_sigdet.scan_dwell_blocks = APP_SIGDET_DWELL_BLOCKS_PER_STEP;
  /* 判断：`g_sigdet.scan_dwell_blocks == 0U`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
  if (g_sigdet.scan_dwell_blocks == 0U)
  {
    g_sigdet.scan_dwell_blocks = 1U;
  }
  /* 清空当前频点的 dwell 累加器，准备统计新的扫描阶段。 */
  g_sigdet.step_vpp_sum = 0ULL;
  g_sigdet.step_i_vpp_sum = 0ULL;
  g_sigdet.step_q_vpp_sum = 0ULL;
  g_sigdet.step_dwell_count = 0U;

  g_sigdet.status.scanning = 1U;
  g_sigdet.status.carrier_present = 0U;
  g_sigdet.status.locked = 0U;
  g_sigdet.status.stage = stage;
  g_sigdet.status.current_lo_hz = g_sigdet.scan_start_hz;
  g_sigdet.status.estimated_carrier_hz = g_sigdet.scan_start_hz;
  g_sigdet.status.raw_estimate_hz = g_sigdet.scan_start_hz;
  g_sigdet.status.demod_lo_hz = g_sigdet.scan_start_hz;
  g_sigdet.status.correction_hz = 0;
  g_sigdet.status.coarse_estimate_hz = g_sigdet.coarse_estimate_hz;
  g_sigdet.status.current_vpp_raw = 0U;
  g_sigdet.status.current_i_vpp_raw = 0U;
  g_sigdet.status.current_q_vpp_raw = 0U;
  g_sigdet.status.peak_vpp_raw = 0U;
  g_sigdet.status.valley_vpp_raw = 0xFFFFFFFFUL;
  g_sigdet.status.threshold_vpp_raw = 0U;
  g_sigdet.status.left_edge_hz = 0U;
  g_sigdet.status.right_edge_hz = 0U;
  g_sigdet.status.step_index = 0U;
  /* 函数跳转：调用 app_signal_detect_calc_step_count()，旧版/兼容逻辑计算扫频频点数量。 */
  g_sigdet.status.step_count = app_signal_detect_calc_step_count(g_sigdet.scan_start_hz,
                                                                  g_sigdet.scan_stop_hz,
                                                                  g_sigdet.scan_step_hz);
  g_sigdet.status.valley_step_index = APP_SIGDET_INVALID_STEP;
  g_sigdet.status.peak_step_index = 0U;
  g_sigdet.status.scan_start_tick_ms = overall_start_tick;
  g_sigdet.status.scan_finish_tick_ms = 0U;
  /* 函数跳转：调用 app_signal_detect_set_lo()，旧版/兼容逻辑调用 DDS 设置 LO。 */
  app_signal_detect_set_lo(g_sigdet.status.current_lo_hz);

/* 条件编译判断：判断 `(APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
  {
    char line[128];

    /* 函数跳转：调用 snprintf()，把当前状态格式化成字符串，供后续日志输出。 */
    (void)snprintf(line,
                   sizeof(line),
                   "vpp_stage,%s\r\n",
                   /* 函数跳转：调用 app_signal_detect_stage_name()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
                   app_signal_detect_stage_name(stage));
    /* 函数跳转：调用 app_signal_detect_trace_line()，旧版/兼容逻辑输出扫频日志行。 */
    app_signal_detect_trace_line(line);

    /* 函数跳转：调用 snprintf()，把当前状态格式化成字符串，供后续日志输出。 */
    (void)snprintf(line,
                   sizeof(line),
                   "vpp_begin,%lu,%lu,%lu,%u\r\n",
                   (unsigned long)g_sigdet.scan_start_hz,
                   (unsigned long)g_sigdet.scan_stop_hz,
                   (unsigned long)g_sigdet.scan_step_hz,
                   (unsigned)g_sigdet.scan_dwell_blocks);
    /* 函数跳转：调用 app_signal_detect_trace_line()，旧版/兼容逻辑输出扫频日志行。 */
    app_signal_detect_trace_line(line);
    /* 函数跳转：调用 app_signal_detect_trace_line()，旧版/兼容逻辑输出扫频日志行。 */
    app_signal_detect_trace_line("vpp,lo_hz,total_vpp,i_vpp,q_vpp\r\n");
  }
#endif
}

static void app_signal_detect_begin_fine_scan(uint32_t center_hz)
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

  /* 函数跳转：调用 app_signal_detect_begin_sweep()，旧版/兼容扫频状态机进入粗扫或细扫。 */
  app_signal_detect_begin_sweep(start_hz,
                                stop_hz,
                                APP_SIGDET_FINE_STEP_HZ,
                                APP_SIGDET_STAGE_VPP_FINE_SCAN);
}

/* 保存当前频点的平均 Vpp，并更新本轮扫频的峰值和谷值。 */
static void app_signal_detect_finish_step(uint32_t avg_vpp, uint32_t avg_i_vpp, uint32_t avg_q_vpp)
{
  uint16_t idx = g_sigdet.status.step_index;

  /* 判断：`idx < APP_SIGDET_MAX_STEPS`。含义：确认频点数量是否超过表容量；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
  if (idx < APP_SIGDET_MAX_STEPS)
  {
    g_sigdet.vpp_table[idx] = avg_vpp;
  }

  g_sigdet.status.current_vpp_raw = avg_vpp;
  g_sigdet.status.current_i_vpp_raw = avg_i_vpp;
  g_sigdet.status.current_q_vpp_raw = avg_q_vpp;

  /* 判断：`avg_vpp > g_sigdet.status.peak_vpp_raw`。含义：判断曲线边沿、峰值或谷值索引是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
  if (avg_vpp > g_sigdet.status.peak_vpp_raw)
  {
    g_sigdet.status.peak_vpp_raw = avg_vpp;
    g_sigdet.status.peak_step_index = idx;
  }

  /* 判断：`(g_sigdet.status.valley_step_index == APP_SIGDET_INVALID_STEP) || (avg_vpp < g_sigdet.status.valley_vpp_raw)`。含义：判断曲线边沿、峰值或谷值索引是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
  if ((g_sigdet.status.valley_step_index == APP_SIGDET_INVALID_STEP) ||
      (avg_vpp < g_sigdet.status.valley_vpp_raw))
  {
    g_sigdet.status.valley_vpp_raw = avg_vpp;
    g_sigdet.status.valley_step_index = idx;
  }

/* 条件编译判断：判断 `(APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
  /* 函数跳转：调用 app_signal_detect_trace_point()，旧版/兼容逻辑输出单个频点 Vpp 日志。；调用 app_signal_detect_step_freq_hz()，旧版/兼容逻辑由索引换算频率。 */
  app_signal_detect_trace_point(app_signal_detect_step_freq_hz(g_sigdet.scan_start_hz,
                                                               g_sigdet.scan_step_hz,
                                                               idx),
                                avg_vpp,
                                avg_i_vpp,
                                avg_q_vpp);
#endif
}

/*
 * 根据本轮 Vpp 曲线判断是否存在载波，并估计载波频率。
 * 判据：先用 peak/valley 生成动态阈值，再取超过阈值的左右边界；
 * 若有效区内部存在谷点，优先使用该谷点，否则使用左右边界中点。
 */
static app_signal_detect_sweep_result_t app_signal_detect_eval_current_sweep(void)
{
  app_signal_detect_sweep_result_t result;
  uint32_t rise_raw;
  uint32_t threshold_delta;
  uint16_t idx;
  uint16_t first_active = APP_SIGDET_INVALID_STEP;
  uint16_t last_active = APP_SIGDET_INVALID_STEP;
  uint16_t valley_in_active = APP_SIGDET_INVALID_STEP;
  uint32_t valley_in_active_vpp = 0xFFFFFFFFUL;

  /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
  memset(&result, 0, sizeof(result));

  /* 默认返回扫描起点；只有 carrier_present 置 1 时，调用者才应认为估计有效。 */
  result.estimated_hz = g_sigdet.scan_start_hz;

  /* 判断：`g_sigdet.status.valley_step_index == APP_SIGDET_INVALID_STEP`。含义：判断曲线边沿、峰值或谷值索引是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
  if (g_sigdet.status.valley_step_index == APP_SIGDET_INVALID_STEP)
  {
    g_sigdet.status.carrier_present = 0U;
    g_sigdet.status.locked = 0U;
    return result;
  }

  rise_raw = g_sigdet.status.peak_vpp_raw - g_sigdet.status.valley_vpp_raw;
  threshold_delta = rise_raw >> APP_SIGDET_VPP_THRESHOLD_SHIFT;
  /* 判断：`threshold_delta < APP_SIGDET_VPP_MIN_RISE_RAW`。含义：判断 Vpp 起伏/阈值是否达到载波检测要求；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
  if (threshold_delta < APP_SIGDET_VPP_MIN_RISE_RAW)
  {
    threshold_delta = APP_SIGDET_VPP_MIN_RISE_RAW;
  }
  g_sigdet.status.threshold_vpp_raw = g_sigdet.status.valley_vpp_raw + threshold_delta;

  /* 判断：`rise_raw < APP_SIGDET_VPP_MIN_RISE_RAW`。含义：判断 Vpp 起伏/阈值是否达到载波检测要求；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
  if (rise_raw < APP_SIGDET_VPP_MIN_RISE_RAW)
  {
    /* 响应起伏太小：保留 valley 频点便于日志分析，但不判定为载波。 */
    /* 函数跳转：调用 app_signal_detect_step_freq_hz()，旧版/兼容逻辑由索引换算频率。 */
    result.estimated_hz = app_signal_detect_step_freq_hz(g_sigdet.scan_start_hz,
                                                         g_sigdet.scan_step_hz,
                                                         g_sigdet.status.valley_step_index);
    g_sigdet.status.carrier_present = 0U;
    g_sigdet.status.locked = 0U;
    g_sigdet.status.estimated_carrier_hz = result.estimated_hz;
    return result;
  }

  for (idx = 0U; idx < g_sigdet.status.step_count; idx++)
  {
    /* 判断：`g_sigdet.vpp_table[idx] >= g_sigdet.status.threshold_vpp_raw`。含义：判断 Vpp 起伏/阈值是否达到载波检测要求；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (g_sigdet.vpp_table[idx] >= g_sigdet.status.threshold_vpp_raw)
    {
      /* 判断：`first_active == APP_SIGDET_INVALID_STEP`。含义：判断曲线边沿、峰值或谷值索引是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
      if (first_active == APP_SIGDET_INVALID_STEP)
      {
        first_active = idx;
      }
      last_active = idx;
    }
  }

  /* 判断：`first_active == APP_SIGDET_INVALID_STEP`。含义：判断曲线边沿、峰值或谷值索引是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
  if (first_active == APP_SIGDET_INVALID_STEP)
  {
    /* 峰谷差有效但没有频点越过阈值，视为曲线形态不可信。 */
    /* 函数跳转：调用 app_signal_detect_step_freq_hz()，旧版/兼容逻辑由索引换算频率。 */
    result.estimated_hz = app_signal_detect_step_freq_hz(g_sigdet.scan_start_hz,
                                                         g_sigdet.scan_step_hz,
                                                         g_sigdet.status.valley_step_index);
    g_sigdet.status.carrier_present = 0U;
    g_sigdet.status.locked = 0U;
    g_sigdet.status.estimated_carrier_hz = result.estimated_hz;
    return result;
  }

  for (idx = first_active; idx <= last_active; idx++)
  {
    /* 判断：`g_sigdet.vpp_table[idx] < valley_in_active_vpp`。含义：判断曲线边沿、峰值或谷值索引是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
    if (g_sigdet.vpp_table[idx] < valley_in_active_vpp)
    {
      valley_in_active_vpp = g_sigdet.vpp_table[idx];
      valley_in_active = idx;
    }
  }

  /* 函数跳转：调用 app_signal_detect_step_freq_hz()，旧版/兼容逻辑由索引换算频率。 */
  g_sigdet.status.left_edge_hz = app_signal_detect_step_freq_hz(g_sigdet.scan_start_hz,
                                                                 g_sigdet.scan_step_hz,
                                                                 first_active);
  /* 函数跳转：调用 app_signal_detect_step_freq_hz()，旧版/兼容逻辑由索引换算频率。 */
  g_sigdet.status.right_edge_hz = app_signal_detect_step_freq_hz(g_sigdet.scan_start_hz,
                                                                  g_sigdet.scan_step_hz,
                                                                  last_active);

  /* 判断：`(valley_in_active != APP_SIGDET_INVALID_STEP) && (valley_in_active != first_active) && (valley_in_active != last_active)`。含义：判断曲线边沿、峰值或谷值索引是否有效；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
  if ((valley_in_active != APP_SIGDET_INVALID_STEP) &&
      (valley_in_active != first_active) &&
      (valley_in_active != last_active))
  {
    /* 函数跳转：调用 app_signal_detect_step_freq_hz()，旧版/兼容逻辑由索引换算频率。 */
    result.estimated_hz = app_signal_detect_step_freq_hz(g_sigdet.scan_start_hz,
                                                         g_sigdet.scan_step_hz,
                                                         valley_in_active);
  }
  /* 否则分支：上一个 if/else if 条件不成立时走这里，执行备用路径或默认处理。 */
  else
  {
    /* 用拆分除法求边界中点，避免 left + right 在 32 位下溢出风险。 */
    result.estimated_hz =
        (g_sigdet.status.left_edge_hz / 2UL) + (g_sigdet.status.right_edge_hz / 2UL) +
        ((g_sigdet.status.left_edge_hz & 1UL) & (g_sigdet.status.right_edge_hz & 1UL));
  }

  result.carrier_present = 1U;
  result.locked = 1U;
  g_sigdet.status.carrier_present = 1U;
  g_sigdet.status.locked = 0U;
  g_sigdet.status.estimated_carrier_hz = result.estimated_hz;

  return result;
}

/* 粗扫成功后进入细扫；细扫结束或无载波时进入最终锁定/结束处理。 */
static void app_signal_detect_finish_current_sweep(void)
{
  app_signal_detect_sweep_result_t result;
  uint8_t stage = g_sigdet.status.stage;

  /* 函数跳转：调用 app_signal_detect_eval_current_sweep()，旧版/兼容逻辑分析整轮 Vpp 曲线。 */
  result = app_signal_detect_eval_current_sweep();

/* 条件编译判断：判断 `(APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)` 是否成立；成立时才编译下面代码块。 */
#if (APP_SIGDET_VPP_UART_TRACE_ENABLE != 0U)
  /* 函数跳转：调用 app_signal_detect_trace_sweep_done()，旧版/兼容逻辑输出扫频结束摘要。；调用 app_signal_detect_stage_name()，调用该函数进入对应子流程，执行完后返回当前时序继续向下运行。 */
  app_signal_detect_trace_sweep_done(app_signal_detect_stage_name(stage));
#endif

  /* 判断：`(stage == APP_SIGDET_STAGE_VPP_COARSE_SCAN) && (result.carrier_present != 0U)`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；根据当前扫频阶段选择不同流程；确认本轮 Vpp 曲线是否已经判定存在载波；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
  if ((stage == APP_SIGDET_STAGE_VPP_COARSE_SCAN) && (result.carrier_present != 0U))
  {
    g_sigdet.coarse_estimate_hz = result.estimated_hz;
    g_sigdet.status.coarse_estimate_hz = g_sigdet.coarse_estimate_hz;
    /* 函数跳转：调用 app_signal_detect_begin_fine_scan()，旧版/兼容逻辑启动细扫。 */
    app_signal_detect_begin_fine_scan(g_sigdet.coarse_estimate_hz);
    return;
  }

  /* 函数跳转：调用 app_signal_detect_finish_locked()，旧版/兼容逻辑完成最终锁定。 */
  app_signal_detect_finish_locked(result.carrier_present, result.locked, result.estimated_hz);
}

static void app_signal_detect_finish_locked(uint8_t carrier_present, uint8_t locked, uint32_t estimate_hz)
{
  /* 函数跳转：调用 app_signal_detect_clamp_to_valid_hz()，旧版/兼容逻辑把频率夹到有效范围。 */
  uint32_t raw_hz = app_signal_detect_clamp_to_valid_hz(estimate_hz);
  uint32_t final_hz = raw_hz;
  int32_t correction_hz = 0;

  /* 判断：`final_hz != APP_SIGDET_VALID_STOP_HZ`。含义：确认频率是否超出最终有效范围；成立后执行下面大括号中的保护、状态更新或流程跳转。 */
  if (final_hz != APP_SIGDET_VALID_STOP_HZ)
  {
    uint32_t half_fine_step_hz = APP_SIGDET_FINE_STEP_HZ / 2UL;

    /* Edge midpoint can land either on a fine-step grid point or halfway between two grid points.
     * Only the halfway case needs the historical half-step correction; correcting an on-grid
     * estimate caused occasional -25 kHz locks in the baseline matrix.
     */
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
      /* 函数跳转：调用 app_signal_detect_clamp_to_valid_hz()，旧版/兼容逻辑把频率夹到有效范围。 */
      final_hz = app_signal_detect_clamp_to_valid_hz(final_hz);
    }
  }

  correction_hz = (int32_t)final_hz - (int32_t)raw_hz;

  g_sigdet.status.raw_estimate_hz = raw_hz;
  g_sigdet.status.demod_lo_hz = final_hz;
  g_sigdet.status.correction_hz = correction_hz;
  g_sigdet.status.estimated_carrier_hz = final_hz;
  g_sigdet.status.current_lo_hz = final_hz;
  g_sigdet.status.carrier_present = carrier_present;
  g_sigdet.status.locked = (carrier_present != 0U) ? locked : 0U;
  g_sigdet.status.scanning = 0U;
  g_sigdet.status.stage = APP_SIGDET_STAGE_VPP_LOCKED;
  /* 函数跳转：调用 osKernelGetTickCount()，读取 RTOS tick，用于记录扫频开始/结束或控制更新间隔。 */
  g_sigdet.status.scan_finish_tick_ms = osKernelGetTickCount();

  /* 函数跳转：调用 app_signal_detect_set_lo()，旧版/兼容逻辑调用 DDS 设置 LO。 */
  app_signal_detect_set_lo(final_hz);

  /* 判断：`g_uart_mode == UART_MODE_LOG`。含义：判断是否可以安全输出日志；成立后调用 snprintf()：把当前状态格式化成字符串，供后续日志输出。 */
  if (g_uart_mode == UART_MODE_LOG)
  {
    char line[125];
    uint32_t lock_ms = g_sigdet.status.scan_finish_tick_ms - g_sigdet.status.scan_start_tick_ms;
    /* 函数跳转：调用 snprintf()，把当前状态格式化成字符串，供后续日志输出。 */
    int n = snprintf(line,
                     sizeof(line),
                     "sigdet_final,final_lo=%lu,est=%lu,raw=%lu,corr=%ld,lk=%u,ms=%lu\r\n",
                     (unsigned long)g_sigdet.status.current_lo_hz,
                     (unsigned long)g_sigdet.status.estimated_carrier_hz,
                     (unsigned long)g_sigdet.status.raw_estimate_hz,
                     (long)g_sigdet.status.correction_hz,
                     (unsigned)g_sigdet.status.locked,
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
                 (unsigned long)g_sigdet.status.left_edge_hz,
                 (unsigned long)g_sigdet.status.right_edge_hz,
                 (unsigned long)g_sigdet.status.peak_vpp_raw,
                 (unsigned long)g_sigdet.status.valley_vpp_raw,
                 (unsigned long)g_sigdet.status.threshold_vpp_raw,
                 (unsigned)g_sigdet.status.valley_step_index,
                 (unsigned)g_sigdet.status.peak_step_index,
                 (unsigned long)g_sigdet.status.coarse_estimate_hz);
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
                   (unsigned long)g_sigdet.status.estimated_carrier_hz,
                   (unsigned long)g_sigdet.status.peak_vpp_raw,
                   (unsigned long)g_sigdet.status.valley_vpp_raw,
                   (unsigned long)g_sigdet.status.threshold_vpp_raw,
                   (unsigned long)g_sigdet.status.left_edge_hz,
                   (unsigned long)g_sigdet.status.right_edge_hz);
    /* 函数跳转：调用 app_signal_detect_trace_line()，旧版/兼容逻辑输出扫频日志行。 */
    app_signal_detect_trace_line(line);
  }
#endif
}

/* 初始化扫频检测器，并立即从粗扫阶段开始。 */
void app_signal_detect_init(void)
{
  /* 函数跳转：调用 memset()，清零结构体或数组，避免上一阶段残留数据影响当前流程。 */
  memset(&g_sigdet, 0, sizeof(g_sigdet));
  /* 函数跳转：调用 app_signal_detect_begin_scan()，旧版/兼容逻辑从粗扫重新开始。 */
  app_signal_detect_begin_scan();
  g_sigdet.inited = 1U;
}

void app_signal_detect_request_rescan(void)
{
  /* 判断：`g_sigdet.inited == 0U`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后直接返回/退出当前函数或返回指定结果。 */
  if (g_sigdet.inited == 0U)
  {
    return;
  }

  /* 函数跳转：调用 app_signal_detect_begin_scan()，旧版/兼容逻辑从粗扫重新开始。 */
  app_signal_detect_begin_scan();
}

/*
 * 扫频期间的 ADC block 入口。
 * 每个频点累计 scan_dwell_blocks 个 block 后，计算该频点的平均 Vpp，
 * 再切换到下一个 LO 频点或结束本轮扫频。
 */
void app_signal_detect_process_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt)
{
  app_signal_detect_vpp_t vpp;
  uint32_t avg_vpp;
  uint32_t avg_i_vpp;
  uint32_t avg_q_vpp;

  /* 判断：`(g_sigdet.inited == 0U) || (g_sigdet.status.scanning == 0U)`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后直接返回/退出当前函数或返回指定结果。 */
  if ((g_sigdet.inited == 0U) || (g_sigdet.status.scanning == 0U))
  {
    return;
  }

  /* 函数跳转：调用 app_signal_detect_block_vpp()，旧版/兼容逻辑统计 ADC block 的 Vpp。 */
  vpp = app_signal_detect_block_vpp(i_buf, q_buf, sample_cnt);
  g_sigdet.step_vpp_sum += vpp.metric_vpp;
  g_sigdet.step_i_vpp_sum += vpp.i_vpp;
  g_sigdet.step_q_vpp_sum += vpp.q_vpp;
  g_sigdet.step_dwell_count++;

  /* 判断：`g_sigdet.step_dwell_count < g_sigdet.scan_dwell_blocks`。含义：决定是否进入下面的大括号分支；成立后直接返回/退出当前函数或返回指定结果。 */
  if (g_sigdet.step_dwell_count < g_sigdet.scan_dwell_blocks)
  {
    return;
  }

  /* avg_vpp 是当前频点 dwell 内 max(I_Vpp, Q_Vpp) 的平均值。 */
  avg_vpp = (uint32_t)(g_sigdet.step_vpp_sum / g_sigdet.step_dwell_count);
  avg_i_vpp = (uint32_t)(g_sigdet.step_i_vpp_sum / g_sigdet.step_dwell_count);
  avg_q_vpp = (uint32_t)(g_sigdet.step_q_vpp_sum / g_sigdet.step_dwell_count);
  /* 函数跳转：调用 app_signal_detect_finish_step()，旧版/兼容逻辑记录一个频点结果并更新峰谷。 */
  app_signal_detect_finish_step(avg_vpp, avg_i_vpp, avg_q_vpp);

  g_sigdet.step_vpp_sum = 0ULL;
  g_sigdet.step_i_vpp_sum = 0ULL;
  g_sigdet.step_q_vpp_sum = 0ULL;
  g_sigdet.step_dwell_count = 0U;

  /* 判断：`(g_sigdet.status.step_index + 1U) < g_sigdet.status.step_count`。含义：决定是否进入下面的大括号分支；成立后更新相关状态变量/输出字段，然后继续后续流程。 */
  if ((g_sigdet.status.step_index + 1U) < g_sigdet.status.step_count)
  {
    g_sigdet.status.step_index++;
    /* 函数跳转：调用 app_signal_detect_step_freq_hz()，旧版/兼容逻辑由索引换算频率。 */
    g_sigdet.status.current_lo_hz = app_signal_detect_step_freq_hz(g_sigdet.scan_start_hz,
                                                                    g_sigdet.scan_step_hz,
                                                                    g_sigdet.status.step_index);
    /* 函数跳转：调用 app_signal_detect_set_lo()，旧版/兼容逻辑调用 DDS 设置 LO。 */
    app_signal_detect_set_lo(g_sigdet.status.current_lo_hz);
    return;
  }

  /* 函数跳转：调用 app_signal_detect_finish_current_sweep()，旧版/兼容逻辑结束当前扫频阶段。 */
  app_signal_detect_finish_current_sweep();
}

void app_signal_detect_get_status(app_signal_detect_status_t *status_out)
{
  /* 判断：`status_out == 0`。含义：用于防空指针、未初始化、计数为 0 或开关关闭等边界条件；成立后直接返回/退出当前函数或返回指定结果。 */
  if (status_out == 0)
  {
    return;
  }

  *status_out = g_sigdet.status;
}

#endif
