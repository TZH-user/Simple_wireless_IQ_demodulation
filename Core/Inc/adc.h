/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc.h
  * @brief   This file contains all the function prototypes for
  *          the adc.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __ADC_H__
#define __ADC_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern ADC_HandleTypeDef hadc1;

extern ADC_HandleTypeDef hadc2;

/* USER CODE BEGIN Private defines */
/* 半块样本数：任务按 half/full 两段处理，单段长度固定为 ADC_BLOCK_N。 */
#define ADC_BLOCK_N      4096U
/* DMA 总长度：双缓冲模式下等于 2 * ADC_BLOCK_N。 */
#define ADC_BUFFER_N     (ADC_BLOCK_N * 2U)
/* 兼容旧代码命名，实际长度与 ADC_BUFFER_N 保持一致。 */
#define sample_count     ADC_BUFFER_N
/* USER CODE END Private defines */

void MX_ADC1_Init(void);
void MX_ADC2_Init(void);

/* USER CODE BEGIN Prototypes */
extern uint16_t adc1_buf[ADC_BUFFER_N];
extern uint16_t adc2_buf[ADC_BUFFER_N];

/* ISR 标志位：由 ADC half/full 回调置位，成对到齐后再发布给任务。 */
extern volatile uint8_t adc1_half_ready;
extern volatile uint8_t adc1_full_ready;
extern volatile uint8_t adc2_half_ready;
extern volatile uint8_t adc2_full_ready;

/* 任务消费掩码：bit0=half 块就绪，bit1=full 块就绪。 */
extern volatile uint8_t adc_block_ready_mask;
/* 覆盖计数：当上一次块尚未被任务取走又来了新块时递增。 */
extern volatile uint32_t adc_overrun_cnt;
/* 调试计数：用于验证 ISR 发布和任务消费是否一一对应。 */
extern volatile uint32_t adc_isr_half_cnt;
extern volatile uint32_t adc_isr_full_cnt;
extern volatile uint32_t adc_task_half_cnt;
extern volatile uint32_t adc_task_full_cnt;

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc);
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __ADC_H__ */

