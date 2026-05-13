#include "app_signal_pipeline.h"

#include <stddef.h>

#include "app_adc_log.h"
#include "app_carrier_sync.h"
#include "app_signal_iq_track.h"
#include "app_signal_sweep.h"

/* 根据扫频状态构造锁定门控。 */
static uint8_t app_signal_pipeline_build_locked_gate(const app_signal_detect_status_t *status)
{
    /* 判断状态快照指针是否有效。 */
    if (status == NULL)
    {
        return 0U;
    }

    return ((status->stage == APP_SIGDET_STAGE_VPP_LOCKED) &&
            (status->locked != 0U) &&
            (status->carrier_present != 0U)) ? 1U : 0U;
}

/* 处理未锁定 block：只复位锁定后模块，不推进后续业务。 */
static void app_signal_pipeline_process_unlocked_block(void)
{
    /* 复位锁定后的 IQ 跟踪状态。 */
    app_signal_iq_track_reset();
    /* 通知载波同步模块进入失锁路径。 */
    app_carrier_sync_on_unlock();
}

/* 处理锁定 block：先做 IQ 跟踪，再视结果决定是否更新频率闭环。 */
static void app_signal_pipeline_process_locked_block(const uint16_t *i_buf,
                                                     const uint16_t *q_buf,
                                                     uint32_t sample_cnt)
{
    app_signal_iq_track_snapshot_t snapshot;

    /* 先对当前 block 做 IQ 跟踪分析。 */
    app_signal_iq_track_process_locked_block(i_buf, q_buf, sample_cnt);
    /* 再读取最新 IQ 跟踪快照。 */
    app_signal_iq_track_get_snapshot(&snapshot);

    /* 判断当前 block 是否已经产生了新的残余频偏结果。 */
    if (snapshot.result_valid == 0U)
    {
        return;
    }

    /* 输出 IQ 预处理日志。 */
    app_adc_log_iq_preproc_1s(snapshot.active,
                              &snapshot.result,
                              &snapshot.verify);
    /* 输出 I/Q 幅度和绝对相位观测日志。 */
    app_adc_log_iq_amp_phase_1s(snapshot.active,
                                &snapshot.result);
    /* 让载波同步模块处理锁定后的频率闭环和相位观测。 */
    app_carrier_sync_process_locked_block(i_buf,
                                          q_buf,
                                          sample_cnt,
                                          snapshot.adc_mid,
                                          &snapshot.result);

    {
        app_carrier_sync_status_t carrier_status;

        /* 读取载波同步状态并输出日志。 */
        app_carrier_sync_get_status(&carrier_status);
        app_adc_log_carrier_sync_1s(&carrier_status);
    }
}

void app_signal_pipeline_init(void)
{
    /* 初始化扫频模块。 */
    app_signal_sweep_init();
    /* 初始化 IQ 跟踪模块。 */
    app_signal_iq_track_init(APP_IQ_PREPROC_DEFAULT_SAMPLE_RATE_HZ);
    /* 初始化载波同步模块。 */
    app_carrier_sync_init();
}

void app_signal_pipeline_request_rescan(void)
{
    /* 请求扫频模块重新开始扫描。 */
    app_signal_sweep_request_rescan();
    /* 复位锁定后的 IQ 跟踪状态。 */
    app_signal_iq_track_reset();
    /* 将 VRFE 控制量复位到中心电压。 */
    app_carrier_sync_reset_to_center();
}

void app_signal_pipeline_process_block(const uint16_t *i_buf,
                                       const uint16_t *q_buf,
                                       uint32_t sample_cnt)
{
    app_signal_detect_status_t status;
    uint8_t locked_gate;

    /* 先将当前 ADC block 送入扫频状态机。 */
    app_signal_sweep_process_block(i_buf, q_buf, sample_cnt);
    /* 再读取当前扫频状态快照。 */
    app_signal_sweep_get_status(&status);
    /* 输出扫频/锁定状态日志。 */
    app_adc_log_algo_1s(&status);

    locked_gate = app_signal_pipeline_build_locked_gate(&status);

    /* 判断当前是否已进入锁定阶段。 */
    if (locked_gate == 0U)
    {
        app_signal_pipeline_process_unlocked_block();
        return;
    }

    app_signal_pipeline_process_locked_block(i_buf, q_buf, sample_cnt);
}

void app_signal_pipeline_get_status(app_signal_detect_status_t *status_out)
{
    /* 直接转发扫频状态快照。 */
    app_signal_sweep_get_status(status_out);
}
