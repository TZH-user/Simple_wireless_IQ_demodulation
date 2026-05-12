#ifndef APP_SIGNAL_DETECT_H
#define APP_SIGNAL_DETECT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 扫频配置：
 *   0 - 实验室频段：有效载波 30~50 MHz，LO 扫描 29~51 MHz
 *   1 - 竞赛频段：有效载波 110~130 MHz，LO 扫描 109~131 MHz
 */
#ifndef APP_SIGDET_SCAN_PROFILE
#define APP_SIGDET_SCAN_PROFILE               0U
#endif

#ifndef APP_SIGDET_DDS_CHANNEL
#define APP_SIGDET_DDS_CHANNEL                0U
#endif

#if !defined(APP_SIGDET_VALID_START_HZ) || !defined(APP_SIGDET_VALID_STOP_HZ) || \
    !defined(APP_SIGDET_SCAN_START_HZ) || !defined(APP_SIGDET_SCAN_STOP_HZ)
#if (APP_SIGDET_SCAN_PROFILE == 0U)
#define APP_SIGDET_VALID_START_HZ             30000000UL
#define APP_SIGDET_VALID_STOP_HZ              50000000UL
#define APP_SIGDET_SCAN_START_HZ              29000000UL
#define APP_SIGDET_SCAN_STOP_HZ               51000000UL
#elif (APP_SIGDET_SCAN_PROFILE == 1U)
#define APP_SIGDET_VALID_START_HZ             110000000UL
#define APP_SIGDET_VALID_STOP_HZ              130000000UL
#define APP_SIGDET_SCAN_START_HZ              109000000UL
#define APP_SIGDET_SCAN_STOP_HZ               131000000UL
#else
#error "Unsupported APP_SIGDET_SCAN_PROFILE"
#endif
#endif

/* 粗扫步进频率；每个频点统计若干个 ADC block 的 Vpp。 */
#ifndef APP_SIGDET_SCAN_STEP_HZ
#define APP_SIGDET_SCAN_STEP_HZ               300000UL
#endif

/* 细扫范围：以粗扫估计值为中心，左右各扩展 APP_SIGDET_FINE_SPAN_HZ。 */
#ifndef APP_SIGDET_FINE_SPAN_HZ
#define APP_SIGDET_FINE_SPAN_HZ               1200000UL
#endif

#ifndef APP_SIGDET_FINE_STEP_HZ
#define APP_SIGDET_FINE_STEP_HZ               50000UL
#endif

/* 每个频点的驻留时间，单位为 ADC 块数；扫频阶段默认 2 块，细扫阶段可调整增加以提升 SNR 和稳定性。 */
#ifndef APP_SIGDET_DWELL_BLOCKS_PER_STEP
#define APP_SIGDET_DWELL_BLOCKS_PER_STEP      2U
#endif

/* 判定一次扫频响应有效所需的最小 Vpp 峰谷差，单位为 ADC 原始码值。 */
#ifndef APP_SIGDET_VPP_MIN_RISE_RAW
#define APP_SIGDET_VPP_MIN_RISE_RAW           80UL
#endif

/* 有效区阈值：valley + max((peak - valley) / 4, APP_SIGDET_VPP_MIN_RISE_RAW)。 */
#ifndef APP_SIGDET_VPP_THRESHOLD_SHIFT
#define APP_SIGDET_VPP_THRESHOLD_SHIFT        2U
#endif

/* 打开后输出 Vpp 扫频曲线，便于用串口日志画图和排查锁定位置。 */
#ifndef APP_SIGDET_VPP_UART_TRACE_ENABLE
#define APP_SIGDET_VPP_UART_TRACE_ENABLE      0U
#endif

#define APP_SIGDET_STAGE_VPP_COARSE_SCAN      0U  /* 粗扫阶段：快速覆盖全频段，找出可能存在载波的频率区间。 */
#define APP_SIGDET_STAGE_VPP_FINE_SCAN        1U  /* 细扫阶段：围绕粗扫结果缩小步进，提高载波估计精度。 */
#define APP_SIGDET_STAGE_VPP_LOCKED           2U  /* 锁定阶段：停止扫频，后续可进入 IQ 预处理和调制识别。 */
#define APP_SIGDET_STAGE_VPP_SCAN             APP_SIGDET_STAGE_VPP_COARSE_SCAN  /* 兼容旧代码的扫频阶段别名。 */

typedef struct
{
  uint8_t scanning;                /* 1 表示正在粗扫/细扫；锁定或结束后清 0。 */
  uint8_t carrier_present;         /* Vpp 曲线峰谷差和有效边沿满足门限后置 1。 */
  uint8_t stage;                   /* 当前阶段：粗扫、细扫或锁定。 */
  uint8_t locked;                  /* 载波确认并写入最终 LO 后置 1。 */
  uint32_t current_lo_hz;          /* 当前 DDS/LO 频点；扫频时随 step 更新，锁定后等于 demod_lo_hz。 */
  uint32_t estimated_carrier_hz;   /* 对外显示的载波估计频率，锁定后等于 demod_lo_hz。 */
  uint32_t raw_estimate_hz;        /* 半步校正前的频率估计值。 */
  uint32_t demod_lo_hz;            /* 校正后实际写入 DDS 的解调 LO 频率。 */
  int32_t correction_hz;           /* demod_lo_hz - raw_estimate_hz。 */
  uint32_t coarse_estimate_hz;     /* 粗扫结果，作为细扫中心频率。 */
  uint32_t current_vpp_raw;        /* 当前频点 dwell 内 max(I_Vpp, Q_Vpp) 的平均值。 */
  uint32_t current_i_vpp_raw;      /* 当前频点 dwell 内 I 路 Vpp 平均值。 */
  uint32_t current_q_vpp_raw;      /* 当前频点 dwell 内 Q 路 Vpp 平均值。 */
  uint32_t peak_vpp_raw;           /* 本轮扫频中最大的 current_vpp_raw。 */
  uint32_t valley_vpp_raw;         /* 本轮扫频中最小的 current_vpp_raw。 */
  uint32_t threshold_vpp_raw;      /* valley + max((peak - valley) / 4, APP_SIGDET_VPP_MIN_RISE_RAW)。 */
  uint32_t left_edge_hz;           /* Vpp 首次超过 threshold 的左边界频点。 */
  uint32_t right_edge_hz;          /* Vpp 最后一次超过 threshold 的右边界频点。 */
  uint32_t scan_start_tick_ms;     /* 扫频开始时的 RTOS tick。 */
  uint32_t scan_finish_tick_ms;    /* 扫频结束或锁定时的 RTOS tick。 */
  uint16_t step_index;             /* 当前扫频点索引。 */
  uint16_t step_count;             /* 本轮扫频点总数。 */
  uint16_t valley_step_index;      /* valley_vpp_raw 对应的扫频点索引。 */
  uint16_t peak_step_index;        /* peak_vpp_raw 对应的扫频点索引。 */
} app_signal_detect_status_t;

/*
 * 初始化 Signal 门面层。
 * 当前会依次初始化：
 *   - 扫频状态机
 *   - OCXO VRFE 慢速闭环
 *   - IQ 预处理残余频偏分析上下文
 */
void app_signal_detect_init(void);

/* 请求重新扫频，同时清空 IQ 预处理历史并让 VRFE 回到中心电压。 */
void app_signal_detect_request_rescan(void);

/*
 * Signal 层统一 block 入口。
 * AdcTask 只调用这个函数，不直接了解扫频、IQ 预处理或 VRFE 闭环细节。
 */
void app_signal_pipeline_process_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt);

/* 兼容旧调用名；内部直接转到 app_signal_pipeline_process_block()。 */
void app_signal_detect_process_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt);

/* 获取当前扫频/锁定状态快照。 */
void app_signal_detect_get_status(app_signal_detect_status_t *status_out);

#ifdef __cplusplus
}
#endif

#endif /* APP_SIGNAL_DETECT_H */
