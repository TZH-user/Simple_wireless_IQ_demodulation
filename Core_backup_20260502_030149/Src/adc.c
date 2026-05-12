/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc.c
  * @brief   This file provides code for the configuration
  *          of the ADC instances.
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
/* Includes ------------------------------------------------------------------*/
#include "adc.h"

/* USER CODE BEGIN 0 */
#include "tim.h" //鍖呭惈瀹氭椂鍣ㄥご鏂囦欢锛屼互渚垮湪鍥炶皟鍑芥暟涓娇鐢ㄥ畾鏃跺櫒鎺у埗閲囨牱鍛ㄦ湡
#include "cmsis_os2.h"
#include "freertos.h"
#include "RtosTypes.h"
/* I/Q mapping policy (keep this in USER CODE area to avoid CubeMX overwrite):
 *   I path: PC4 -> ADC1_INP4 -> adc1_buf[]
 *   Q path: PB1 -> ADC2_INP5 -> adc2_buf[]
 */
/* USER CODE END 0 */

ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;
DMA_HandleTypeDef hdma_adc1;
DMA_HandleTypeDef hdma_adc2;

/* ADC1 init function */
void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV2;
  hadc1.Init.Resolution = ADC_RESOLUTION_14B;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T6_TRGO;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc1.Init.OversamplingMode = DISABLE;
  hadc1.Init.Oversampling.Ratio = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the ADC multi-mode
  */
  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  sConfig.OffsetSignedSaturation = DISABLE;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}
/* ADC2 init function */
void MX_ADC2_Init(void)
{

  /* USER CODE BEGIN ADC2_Init 0 */

  /* USER CODE END ADC2_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC2_Init 1 */

  /* USER CODE END ADC2_Init 1 */

  /** Common config
  */
  hadc2.Instance = ADC2;
  hadc2.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV2;
  hadc2.Init.Resolution = ADC_RESOLUTION_14B;
  hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc2.Init.LowPowerAutoWait = DISABLE;
  hadc2.Init.ContinuousConvMode = DISABLE;
  hadc2.Init.NbrOfConversion = 1;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  hadc2.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T6_TRGO;
  hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc2.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;
  hadc2.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc2.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc2.Init.OversamplingMode = DISABLE;
  hadc2.Init.Oversampling.Ratio = 1;
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  sConfig.OffsetSignedSaturation = DISABLE;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */

}

static uint32_t HAL_RCC_ADC12_CLK_ENABLED=0;

void HAL_ADC_MspInit(ADC_HandleTypeDef* adcHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(adcHandle->Instance==ADC1)
  {
  /* USER CODE BEGIN ADC1_MspInit 0 */

  /* USER CODE END ADC1_MspInit 0 */
    /* ADC1 clock enable */
    HAL_RCC_ADC12_CLK_ENABLED++;
    if(HAL_RCC_ADC12_CLK_ENABLED==1){
      __HAL_RCC_ADC12_CLK_ENABLE();
    }

    __HAL_RCC_GPIOC_CLK_ENABLE();
    /**ADC1 GPIO Configuration
    PC4     ------> ADC1_INP4
    */
    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* ADC1 DMA Init */
    /* ADC1 Init */
    hdma_adc1.Instance = DMA1_Stream0;
    hdma_adc1.Init.Request = DMA_REQUEST_ADC1;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma_adc1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_adc1) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(adcHandle,DMA_Handle,hdma_adc1);

  /* USER CODE BEGIN ADC1_MspInit 1 */

  /* USER CODE END ADC1_MspInit 1 */
  }
  else if(adcHandle->Instance==ADC2)
  {
  /* USER CODE BEGIN ADC2_MspInit 0 */

  /* USER CODE END ADC2_MspInit 0 */
    /* ADC2 clock enable */
    HAL_RCC_ADC12_CLK_ENABLED++;
    if(HAL_RCC_ADC12_CLK_ENABLED==1){
      __HAL_RCC_ADC12_CLK_ENABLE();
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**ADC2 GPIO Configuration
    PB1     ------> ADC2_INP5
    */
    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* ADC2 DMA Init */
    /* ADC2 Init */
    hdma_adc2.Instance = DMA2_Stream0;
    hdma_adc2.Init.Request = DMA_REQUEST_ADC2;
    hdma_adc2.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc2.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc2.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc2.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc2.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc2.Init.Mode = DMA_CIRCULAR;
    hdma_adc2.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma_adc2.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_adc2) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(adcHandle,DMA_Handle,hdma_adc2);

  /* USER CODE BEGIN ADC2_MspInit 1 */

  /* USER CODE END ADC2_MspInit 1 */
  }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef* adcHandle)
{

  if(adcHandle->Instance==ADC1)
  {
  /* USER CODE BEGIN ADC1_MspDeInit 0 */

  /* USER CODE END ADC1_MspDeInit 0 */
    /* Peripheral clock disable */
    HAL_RCC_ADC12_CLK_ENABLED--;
    if(HAL_RCC_ADC12_CLK_ENABLED==0){
      __HAL_RCC_ADC12_CLK_DISABLE();
    }

    /**ADC1 GPIO Configuration
    PC4     ------> ADC1_INP4
    */
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_4);

    /* ADC1 DMA DeInit */
    HAL_DMA_DeInit(adcHandle->DMA_Handle);
  /* USER CODE BEGIN ADC1_MspDeInit 1 */

  /* USER CODE END ADC1_MspDeInit 1 */
  }
  else if(adcHandle->Instance==ADC2)
  {
  /* USER CODE BEGIN ADC2_MspDeInit 0 */

  /* USER CODE END ADC2_MspDeInit 0 */
    /* Peripheral clock disable */
    HAL_RCC_ADC12_CLK_ENABLED--;
    if(HAL_RCC_ADC12_CLK_ENABLED==0){
      __HAL_RCC_ADC12_CLK_DISABLE();
    }

    /**ADC2 GPIO Configuration
    PB1     ------> ADC2_INP5
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_1);

    /* ADC2 DMA DeInit */
    HAL_DMA_DeInit(adcHandle->DMA_Handle);
  /* USER CODE BEGIN ADC2_MspDeInit 1 */

  /* USER CODE END ADC2_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
uint16_t adc1_buf[ADC_BUFFER_N];
__attribute__((section(".dma_buffer"))) __attribute__((aligned(32)))
uint16_t adc2_buf[ADC_BUFFER_N];
/* Fixed I/Q mapping:
 *   adc1_buf[] = I path = PC4 / ADC1_INP4
 *   adc2_buf[] = Q path = PB1 / ADC2_INP5
 */
/* 鍙?ADC 鍘熷閲囨牱缂撳啿锛氫笅鏍?[0, ADC_BLOCK_N) 涓哄崐鍧楋紝[ADC_BLOCK_N, ADC_BUFFER_N) 涓哄叏鍧椼€?*/

volatile uint8_t adc1_half_ready = 0U;  // ADC1 鐨勫崐鍧楄浆鎹㈠畬鎴愭爣蹇?
volatile uint8_t adc1_full_ready = 0U;  // ADC1 鐨勫叏鍧楄浆鎹㈠畬鎴愭爣蹇?
volatile uint8_t adc2_half_ready = 0U;  // ADC2 鐨勫崐鍧楄浆鎹㈠畬鎴愭爣蹇?
volatile uint8_t adc2_full_ready = 0U;  // ADC2 鐨勫叏鍧楄浆鎹㈠畬鎴愭爣蹇?
volatile uint8_t adc_block_ready_mask = 0U; // bit0=鍗婂潡鏁版嵁鍑嗗濂? bit1=鍏ㄥ潡鏁版嵁鍑嗗濂?
volatile uint32_t adc_overrun_cnt = 0U; // ADC 鏁版嵁鍧楄瑕嗙洊鐨勬鏁?
volatile uint32_t adc_isr_half_cnt = 0U;  // ADC 鍗婂潡杞崲瀹屾垚鐨勪腑鏂鏁?
volatile uint32_t adc_isr_full_cnt = 0U;  // ADC 鍏ㄥ潡杞崲瀹屾垚鐨勪腑鏂鏁?
volatile uint32_t adc_task_half_cnt = 0U; // ADC 鍗婂潡鏁版嵁琚换鍔″鐞嗙殑娆℃暟
volatile uint32_t adc_task_full_cnt = 0U; // ADC 鍏ㄥ潡鏁版嵁琚换鍔″鐞嗙殑娆℃暟

/* 浠呭綋 ADC1 涓?ADC2 鐨勫悓涓€鍒嗗潡閮藉氨缁椂鎵嶅彂甯冧俊鍙烽噺锛岄伩鍏嶅弻閫氶亾鏁版嵁閿欎綅銆?*/
static inline void adc_try_publish_block_from_isr(void) //灏濊瘯浠?ADC 涓柇鍙戝竷鏁版嵁鍧楀噯澶囧ソ鐨勪俊鍙?
{
    if ((adc1_half_ready != 0U) && (adc2_half_ready != 0U)) //涓や釜 ADC 鐨勫崐鍧楄浆鎹㈠畬鎴?
    {
        adc1_half_ready = 0U;
        adc2_half_ready = 0U;
        if ((adc_block_ready_mask & 0x01U) != 0U) //涓婁竴涓崐鍧楄繕娌¤浠诲姟澶勭悊
        {
            adc_overrun_cnt++;
        }
        adc_block_ready_mask |= 0x01U;
        adc_isr_half_cnt++;
        (void)osSemaphoreRelease(AdcFrameReadySemHandle);
        /* ISR 鍙仛鍙戝竷锛屼笉鍋氶噸澶勭悊锛涘疄闄呮惉杩愬湪 AdcTask 涓畬鎴愩€?*/
    }

    if ((adc1_full_ready != 0U) && (adc2_full_ready != 0U)) //涓や釜 ADC 鐨勫叏鍧楄浆鎹㈠畬鎴?
    {
        adc1_full_ready = 0U;
        adc2_full_ready = 0U;
        if ((adc_block_ready_mask & 0x02U) != 0U)
        {
            adc_overrun_cnt++;
        }
        adc_block_ready_mask |= 0x02U;
        adc_isr_full_cnt++;
        (void)osSemaphoreRelease(AdcFrameReadySemHandle);
    }
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)  // ADC 鍗婂潡杞崲瀹屾垚鍥炶皟鍑芥暟
{
    if (hadc->Instance == ADC1)
    {
        adc1_half_ready = 1U;
    }
    else if (hadc->Instance == ADC2)
    {
        adc2_half_ready = 1U;
    }

    adc_try_publish_block_from_isr();
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)  // ADC 鍏ㄥ潡杞崲瀹屾垚鍥炶皟鍑芥暟
{
    if (hadc->Instance == ADC1)
    {
        adc1_full_ready = 1U;
    }
    else if (hadc->Instance == ADC2)
    {
        adc2_full_ready = 1U;
    }

    adc_try_publish_block_from_isr();
}
/* USER CODE END 1 */


