/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_SIGNAL_DETECT_H
/* 宏定义说明：APP_SIGNAL_DETECT_H = (无显式值)；app_signal_detect.h 的头文件保护宏，避免重复 include。 */
#define APP_SIGNAL_DETECT_H

#include <stdint.h>

/* 条件编译判断：如果该宏已经定义，则编译下面代码块。 */
#ifdef __cplusplus
extern "C" {
#endif

/*
 * 扫频配置：
 *   0 - 实验室频段：有效载波 30~50 MHz，LO 扫描 29~51 MHz
 *   1 - 竞赛频段：有效载波 110~130 MHz，LO 扫描 109~131 MHz
 */
/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_SIGDET_SCAN_PROFILE
/* 宏定义说明：APP_SIGDET_SCAN_PROFILE = 0U；扫频配置档选择；不同档位对应不同有效频段和扫描边界。 */
#define APP_SIGDET_SCAN_PROFILE               0U
#endif

/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_SIGDET_DDS_CHANNEL
/* 宏定义说明：APP_SIGDET_DDS_CHANNEL = 0U；DDS 输出通道号，扫频/锁定时通过该通道设置本振 LO。 */
#define APP_SIGDET_DDS_CHANNEL                0U
#endif

/* 条件编译判断：判断 `!defined(APP_SIGDET_VALID_START_HZ) || !defined(APP_SIGDET_VALID_STOP_HZ) || \` 是否成立；成立时才编译下面代码块。 */
#if !defined(APP_SIGDET_VALID_START_HZ) || !defined(APP_SIGDET_VALID_STOP_HZ) || \
    !defined(APP_SIGDET_SCAN_START_HZ) || !defined(APP_SIGDET_SCAN_STOP_HZ)
/* 条件编译判断：判断 `(APP_SIGDET_SCAN_PROFILE == 0U)` 是否成立；成立时才编译下面代码块。 */
#if (APP_SIGDET_SCAN_PROFILE == 0U)
/* 宏定义说明：APP_SIGDET_VALID_START_HZ = 30000000UL；最终有效载波频率下限，估计结果会被夹到该范围内。 */
#define APP_SIGDET_VALID_START_HZ             30000000UL
/* 宏定义说明：APP_SIGDET_VALID_STOP_HZ = 50000000UL；最终有效载波频率上限，估计结果会被夹到该范围内。 */
#define APP_SIGDET_VALID_STOP_HZ              50000000UL
/* 宏定义说明：APP_SIGDET_SCAN_START_HZ = 29000000UL；扫频实际起始频率，通常比有效频段多留保护边界。 */
#define APP_SIGDET_SCAN_START_HZ              29000000UL
/* 宏定义说明：APP_SIGDET_SCAN_STOP_HZ = 51000000UL；扫频实际停止频率，通常比有效频段多留保护边界。 */
#define APP_SIGDET_SCAN_STOP_HZ               51000000UL
/* 条件编译判断：前面的 #if/#elif 不成立时，再判断 `(APP_SIGDET_SCAN_PROFILE == 1U)` 是否成立；成立才编译下面代码块。 */
#elif (APP_SIGDET_SCAN_PROFILE == 1U)
/* 宏定义说明：APP_SIGDET_VALID_START_HZ = 110000000UL；最终有效载波频率下限，估计结果会被夹到该范围内。 */
#define APP_SIGDET_VALID_START_HZ             110000000UL
/* 宏定义说明：APP_SIGDET_VALID_STOP_HZ = 130000000UL；最终有效载波频率上限，估计结果会被夹到该范围内。 */
#define APP_SIGDET_VALID_STOP_HZ              130000000UL
/* 宏定义说明：APP_SIGDET_SCAN_START_HZ = 109000000UL；扫频实际起始频率，通常比有效频段多留保护边界。 */
#define APP_SIGDET_SCAN_START_HZ              109000000UL
/* 宏定义说明：APP_SIGDET_SCAN_STOP_HZ = 131000000UL；扫频实际停止频率，通常比有效频段多留保护边界。 */
#define APP_SIGDET_SCAN_STOP_HZ               131000000UL
/* 条件编译否则分支：前面的 #if/#elif 都不成立时编译下面代码块。 */
#else
#error "Unsupported APP_SIGDET_SCAN_PROFILE"
#endif
#endif

/* 粗扫步进频率；每个频点统计若干个 ADC block 的 Vpp。 */
/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_SIGDET_SCAN_STEP_HZ
/* 宏定义说明：APP_SIGDET_SCAN_STEP_HZ = 300000UL；粗扫频率步进，决定粗扫速度和粗略分辨率。 */
#define APP_SIGDET_SCAN_STEP_HZ               300000UL
#endif

/* 细扫范围：以粗扫估计值为中心，左右各扩展 APP_SIGDET_FINE_SPAN_HZ。 */
/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_SIGDET_FINE_SPAN_HZ
/* 宏定义说明：APP_SIGDET_FINE_SPAN_HZ = 1200000UL；细扫相对粗扫估计中心的左右半跨度。 */
#define APP_SIGDET_FINE_SPAN_HZ               1200000UL
#endif

/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_SIGDET_FINE_STEP_HZ
/* 宏定义说明：APP_SIGDET_FINE_STEP_HZ = 50000UL；细扫频率步进，决定最终估计/锁定精度。 */
#define APP_SIGDET_FINE_STEP_HZ               50000UL
#endif

/* 每个频点的驻留时间，单位为 ADC 块数；扫频阶段默认 2 块，细扫阶段可调整增加以提升 SNR 和稳定性。 */
/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_SIGDET_DWELL_BLOCKS_PER_STEP
/* 宏定义说明：APP_SIGDET_DWELL_BLOCKS_PER_STEP = 2U；每个 LO 频点停留并累加的 ADC block 数，用于平均 Vpp。 */
#define APP_SIGDET_DWELL_BLOCKS_PER_STEP      2U
#endif

/* 判定一次扫频响应有效所需的最小 Vpp 峰谷差，单位为 ADC 原始码值。 */
/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_SIGDET_VPP_MIN_RISE_RAW
/* 宏定义说明：APP_SIGDET_VPP_MIN_RISE_RAW = 80UL；判断存在载波所需的最小 Vpp 起伏，低于该值认为曲线不明显。 */
#define APP_SIGDET_VPP_MIN_RISE_RAW           80UL
#endif

/* 有效区阈值：valley + max((peak - valley) / 4, APP_SIGDET_VPP_MIN_RISE_RAW)。 */
/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_SIGDET_VPP_THRESHOLD_SHIFT
/* 宏定义说明：APP_SIGDET_VPP_THRESHOLD_SHIFT = 2U；阈值增量右移系数；阈值约等于 valley + rise/2^shift。 */
#define APP_SIGDET_VPP_THRESHOLD_SHIFT        2U
#endif

/* 打开后输出 Vpp 扫频曲线，便于用串口日志画图和排查锁定位置。 */
/* 条件编译判断：如果该宏尚未定义，则进入头文件内容/对应编译块。 */
#ifndef APP_SIGDET_VPP_UART_TRACE_ENABLE
/* 宏定义说明：APP_SIGDET_VPP_UART_TRACE_ENABLE = 0U；Vpp 扫频曲线串口跟踪开关，1 时输出每个频点/阶段日志。 */
#define APP_SIGDET_VPP_UART_TRACE_ENABLE      0U
#endif

/* 宏定义说明：APP_SIGDET_STAGE_VPP_COARSE_SCAN = 0U；状态值：Vpp 粗扫阶段，覆盖全扫描频段寻找大致载波区。 */
#define APP_SIGDET_STAGE_VPP_COARSE_SCAN      0U  /* 粗扫阶段：快速覆盖全频段，找出可能存在载波的频率区间。 */
/* 宏定义说明：APP_SIGDET_STAGE_VPP_FINE_SCAN = 1U；状态值：Vpp 细扫阶段，围绕粗扫估计进一步细化。 */
#define APP_SIGDET_STAGE_VPP_FINE_SCAN        1U  /* 细扫阶段：围绕粗扫结果缩小步进，提高载波估计精度。 */
/* 宏定义说明：APP_SIGDET_STAGE_VPP_LOCKED = 2U；状态值：扫频结束并锁定 LO，后级可以做 IQ/解调。 */
#define APP_SIGDET_STAGE_VPP_LOCKED           2U  /* 锁定阶段：停止扫频，后续可进入 IQ 预处理和调制识别。 */
/* 宏定义说明：APP_SIGDET_STAGE_VPP_SCAN = APP_SIGDET_STAGE_VPP_COARSE_SCAN；兼容旧代码的状态别名，等价于粗扫阶段。 */
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
/* 函数跳转：调用 app_signal_detect_init()，初始化整个信号检测管线。 */
void app_signal_detect_init(void);

/* 请求重新扫频，同时清空 IQ 预处理历史并让 VRFE 回到中心电压。 */
/* 函数跳转：调用 app_signal_detect_request_rescan()，请求信号检测重新开始扫频。 */
void app_signal_detect_request_rescan(void);

/*
 * Signal 层统一 block 入口。
 * AdcTask 只调用这个函数，不直接了解扫频、IQ 预处理或 VRFE 闭环细节。
 */
/* 函数跳转：调用 app_signal_pipeline_process_block()，总入口：先扫频/预处理，再送后级检测。 */
void app_signal_pipeline_process_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt);

/* 兼容旧调用名；内部直接转到 app_signal_pipeline_process_block()。 */
/* 函数跳转：调用 app_signal_detect_process_block()，兼容旧接口的信号检测 block 入口。 */
void app_signal_detect_process_block(const uint16_t *i_buf, const uint16_t *q_buf, uint32_t sample_cnt);

/* 获取当前扫频/锁定状态快照。 */
/* 函数跳转：调用 app_signal_detect_get_status()，读取当前信号检测状态快照。 */
void app_signal_detect_get_status(app_signal_detect_status_t *status_out);

/* 条件编译判断：如果该宏已经定义，则编译下面代码块。 */
#ifdef __cplusplus
}
#endif

#endif /* APP_SIGNAL_DETECT_H */
