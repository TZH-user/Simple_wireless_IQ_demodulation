#ifndef APP_SIGNAL_STATUS_H
#define APP_SIGNAL_STATUS_H

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

/* DDS 输出通道编号。 */
#ifndef APP_SIGDET_DDS_CHANNEL
#define APP_SIGDET_DDS_CHANNEL                0U
#endif

/*
 * 频段边界。
 * 说明：
 *   - VALID_* 是判定最终结果是否落在允许频段内的范围。
 *   - SCAN_*  是实际 LO 扫频覆盖范围。
 */
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

/* 粗扫步进频率。 */
#ifndef APP_SIGDET_SCAN_STEP_HZ
#define APP_SIGDET_SCAN_STEP_HZ               300000UL
#endif

/* 细扫总跨度，以粗扫中心为中心左右扩展。 */
#ifndef APP_SIGDET_FINE_SPAN_HZ
#define APP_SIGDET_FINE_SPAN_HZ               1200000UL
#endif

/* 细扫步进频率。 */
#ifndef APP_SIGDET_FINE_STEP_HZ
#define APP_SIGDET_FINE_STEP_HZ               50000UL
#endif

/* 每个频点停留的 ADC block 数。 */
#ifndef APP_SIGDET_DWELL_BLOCKS_PER_STEP
#define APP_SIGDET_DWELL_BLOCKS_PER_STEP      2U
#endif

/* Vpp 最小有效抬升门限。 */
#ifndef APP_SIGDET_VPP_MIN_RISE_RAW
#define APP_SIGDET_VPP_MIN_RISE_RAW           80UL
#endif

/* 阈值缩放：threshold = valley + max((peak - valley) / 4, MIN_RISE)。 */
#ifndef APP_SIGDET_VPP_THRESHOLD_SHIFT
#define APP_SIGDET_VPP_THRESHOLD_SHIFT        2U
#endif

/* 是否输出扫频 Vpp trace 日志。 */
#ifndef APP_SIGDET_VPP_UART_TRACE_ENABLE
#define APP_SIGDET_VPP_UART_TRACE_ENABLE      0U
#endif

/* 粗扫阶段标识。 */
#define APP_SIGDET_STAGE_VPP_COARSE_SCAN      0U
/* 细扫阶段标识。 */
#define APP_SIGDET_STAGE_VPP_FINE_SCAN        1U
/* 锁定阶段标识。 */
#define APP_SIGDET_STAGE_VPP_LOCKED           2U
/* 兼容旧别名。 */
#define APP_SIGDET_STAGE_VPP_SCAN             APP_SIGDET_STAGE_VPP_COARSE_SCAN

/*
 * 扫频/锁定状态快照。
 * 说明：
 *   - 本结构体继续保留原名字，避免 UI 和日志联动范围扩大。
 *   - Sweep 负责更新这些字段，Pipeline 和 UI 只读取它们。
 */
typedef struct
{
    uint8_t scanning;                /* 1 表示当前仍处于粗扫或细扫阶段。 */
    uint8_t carrier_present;         /* 1 表示扫频结果判断存在有效载波。 */
    uint8_t stage;                   /* 当前阶段：粗扫、细扫或锁定。 */
    uint8_t locked;                  /* 1 表示最终 LO 已锁定到目标频点。 */
    uint32_t current_lo_hz;          /* 当前 DDS/LO 频点。 */
    uint32_t estimated_carrier_hz;   /* 对外显示的载波估计频率。 */
    uint32_t raw_estimate_hz;        /* 半步修正前的原始估计值。 */
    uint32_t demod_lo_hz;            /* 实际写入 DDS 的解调 LO 频率。 */
    int32_t correction_hz;           /* demod_lo_hz - raw_estimate_hz。 */
    uint32_t coarse_estimate_hz;     /* 粗扫结果，用作细扫中心。 */
    uint32_t current_vpp_raw;        /* 当前频点的综合 Vpp 平均值。 */
    uint32_t current_i_vpp_raw;      /* 当前频点的 I 路 Vpp 平均值。 */
    uint32_t current_q_vpp_raw;      /* 当前频点的 Q 路 Vpp 平均值。 */
    uint32_t peak_vpp_raw;           /* 本轮扫频中的最大综合 Vpp。 */
    uint32_t valley_vpp_raw;         /* 本轮扫频中的最小综合 Vpp。 */
    uint32_t threshold_vpp_raw;      /* 当前使用的有效阈值。 */
    uint32_t left_edge_hz;           /* 左边界频点。 */
    uint32_t right_edge_hz;          /* 右边界频点。 */
    uint32_t scan_start_tick_ms;     /* 扫频开始时间。 */
    uint32_t scan_finish_tick_ms;    /* 扫频结束或锁定时间。 */
    uint16_t step_index;             /* 当前扫频步索引。 */
    uint16_t step_count;             /* 当前总步数。 */
    uint16_t valley_step_index;      /* 谷值对应的步索引。 */
    uint16_t peak_step_index;        /* 峰值对应的步索引。 */
} app_signal_detect_status_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_SIGNAL_STATUS_H */
