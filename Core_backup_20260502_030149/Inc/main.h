/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define AD9959_SDIO2_Pin GPIO_PIN_5
#define AD9959_SDIO2_GPIO_Port GPIOI
#define AD9959_UPDATE_Pin GPIO_PIN_10
#define AD9959_UPDATE_GPIO_Port GPIOC
#define AD9959_SP2_Pin GPIO_PIN_11
#define AD9959_SP2_GPIO_Port GPIOC
#define AD9959_SP3_Pin GPIO_PIN_14
#define AD9959_SP3_GPIO_Port GPIOH
#define AD9959_SP1_Pin GPIO_PIN_12
#define AD9959_SP1_GPIO_Port GPIOC
#define AD9959_RESET_Pin GPIO_PIN_2
#define AD9959_RESET_GPIO_Port GPIOD
#define AD9959_AUX_PH13_Pin GPIO_PIN_13
#define AD9959_AUX_PH13_GPIO_Port GPIOH
#define AD9959_PDC_Pin GPIO_PIN_8
#define AD9959_PDC_GPIO_Port GPIOC
#define AD9959_SP0_Pin GPIO_PIN_9
#define AD9959_SP0_GPIO_Port GPIOC
#define TOUCH_RST_Pin GPIO_PIN_10
#define TOUCH_RST_GPIO_Port GPIOI
#define TOUCH_INT_Pin GPIO_PIN_11
#define TOUCH_INT_GPIO_Port GPIOI
#define TOUCH_INT_EXTI_IRQn EXTI15_10_IRQn
#define TOUCH_SDA_Pin GPIO_PIN_7
#define TOUCH_SDA_GPIO_Port GPIOG
#define TOUCH_SCLK_Pin GPIO_PIN_3
#define TOUCH_SCLK_GPIO_Port GPIOG
#define AD9959_SCLK_Pin GPIO_PIN_10
#define AD9959_SCLK_GPIO_Port GPIOH
#define AD9959_SDIO0_Pin GPIO_PIN_11
#define AD9959_SDIO0_GPIO_Port GPIOH
#define AD9959_SDIO1_Pin GPIO_PIN_9
#define AD9959_SDIO1_GPIO_Port GPIOH
#define AD9959_CS_Pin GPIO_PIN_12
#define AD9959_CS_GPIO_Port GPIOH
#define LCD_BL_Pin GPIO_PIN_6
#define LCD_BL_GPIO_Port GPIOH
#define AD9959_SDIO3_Pin GPIO_PIN_8
#define AD9959_SDIO3_GPIO_Port GPIOH
#define AD9959_AUX_PH7_Pin GPIO_PIN_7
#define AD9959_AUX_PH7_GPIO_Port GPIOH

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
