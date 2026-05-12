/*
 * AD9959 应用层头文件
 *
 * 文件定位：
 * - 这一层属于 app 层
 * - 它建立在 ad9959 driver 层之上
 * - 它不关心寄存器细节，而是关心“当前项目想怎么用 AD9959”
 *
 * 当前文件的职责：
 * 1. 对外暴露本项目使用 AD9959 的应用层入口
 * 2. 把 main.c 和 ad9959.c 之间的“业务策略”隔开
 * 3. 为后面接入 RTOS 任务和算法层预留更稳定的调用边界
 *
 * 一句话理解：
 * - ad9959.c 负责“怎么写芯片寄存器”
 * - app_ad9959.c 负责“当前项目默认怎么配置这颗芯片”
 * - 以后更高层只需要调 app 层，而不要直接散着调 driver 层
 */
#ifndef APP_AD9959_H
#define APP_AD9959_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_AD9959_CHANNEL_COUNT 4U

/* 当前项目默认 4 通道配置常量。
 *
 * 这些宏的目的不是替代运行时配置，
 * 而是把“上电默认策略”集中定义，避免在多个文件里重复写魔法数字。
 */
#define APP_AD9959_DEFAULT_FREQ_HZ   100000U
#define APP_AD9959_DEFAULT_AMP_CODE  1023U
#define APP_AD9959_DEFAULT_PHASE_CH0 0U
#define APP_AD9959_DEFAULT_PHASE_CH1 90U
#define APP_AD9959_DEFAULT_PHASE_CH2 180U
#define APP_AD9959_DEFAULT_PHASE_CH3 270U

/* “保持当前值不变”的哨兵值。
 *
 * 用法举例：
 * - 如果某一路频率不想改，就把对应字段填成 APP_AD9959_KEEP
 * - 应用层会跳过该参数写入，最后只更新真正发生变化的参数
 */
#define APP_AD9959_KEEP ((int32_t)-1)

/* 4 通道批量配置结构体。
 *
 * 设计目的：
 * - 允许一次性描述 4 个通道的频率/幅度/相位配置
 * - 允许某些参数保持不变，不必每次都重写全部 12 个值
 * - 允许“先全部写入，再统一 IO_Update()”
 *
 * 字段说明：
 * - freq_hz[i]：
 *   第 i 路目标频率，单位 Hz；若填 APP_AD9959_KEEP，则该路频率保持不变
 * - amp_code[i]：
 *   第 i 路目标幅度码，当前按 0~1023 使用；若填 APP_AD9959_KEEP，则该路幅度保持不变
 * - phase_deg[i]：
 *   第 i 路目标相位角，单位度；若填 APP_AD9959_KEEP，则该路相位保持不变
 */
typedef struct
{
  int32_t freq_hz[APP_AD9959_CHANNEL_COUNT];
  int32_t amp_code[APP_AD9959_CHANNEL_COUNT];
  int32_t phase_deg[APP_AD9959_CHANNEL_COUNT];
} AppAd9959BatchConfig;

/**
 * @brief 按当前项目默认策略初始化 4 路 AD9959 输出。
 * @param None
 * @retval None
 * @note 当前默认策略是 4 路同频、同幅，且相位依次为 0/90/180/270 度。
 */
void APP_AD9959_InitDefault4Ch(void);

/**
 * @brief 把一个批量配置结构体全部填成“保持不变”。
 * @param cfg 指向待初始化批量配置结构体的指针
 * @retval None
 * @note 调用后，freq_hz/amp_code/phase_deg 的全部字段都会被置为 APP_AD9959_KEEP。
 */
void APP_AD9959_BuildKeepConfig(AppAd9959BatchConfig *cfg);

/**
 * @brief 按批量配置结构体统一写入 4 通道参数，并在最后统一触发 IO_Update()。
 * @param cfg 指向批量配置结构体的指针
 * @retval None
 * @note 配置结构中等于 APP_AD9959_KEEP 的字段会被跳过，不会改动当前硬件状态。
 */
void APP_AD9959_ApplyBatchConfig(const AppAd9959BatchConfig *cfg);

/**
 * @brief 把 4 个通道统一设置为同一频率，并在最后统一触发 IO_Update()。
 * @param freq_hz 4 路统一目标频率，单位 Hz
 * @retval None
 */
void APP_AD9959_SetAllFreq(uint32_t freq_hz);

/**
 * @brief 把 4 个通道配置成同频、同幅、正交相位输出。
 * @param freq_hz 4 路统一频率，单位 Hz
 * @param amp 4 路统一幅度码，当前按 driver 层约定使用 0~1023
 * @retval None
 * @note 相位将被固定设置为 CH0=0 度、CH1=90 度、CH2=180 度、CH3=270 度。
 */
void APP_AD9959_SetQuadPhaseDeg(uint32_t freq_hz, uint16_t amp);

#ifdef __cplusplus
}
#endif

#endif
