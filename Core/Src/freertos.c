/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_dds_ctrl.h" //寮曠敤 AppDdsCmd 绫诲瀷缁撴瀯浣?
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for DisplayTask */
osThreadId_t DisplayTaskHandle;
const osThreadAttr_t DisplayTask_attributes = {
  .name = "DisplayTask",
  .stack_size = 4096 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for TouchTask */
osThreadId_t TouchTaskHandle;
const osThreadAttr_t TouchTask_attributes = {
  .name = "TouchTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for DDSTask */
osThreadId_t DDSTaskHandle;
const osThreadAttr_t DDSTask_attributes = {
  .name = "DDSTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for PrintfTask */
osThreadId_t PrintfTaskHandle;
const osThreadAttr_t PrintfTask_attributes = {
  .name = "PrintfTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for AdcTask */
osThreadId_t AdcTaskHandle;
const osThreadAttr_t AdcTask_attributes = {
  .name = "AdcTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for ModDetectTask */
osThreadId_t ModDetectTaskHandle;
const osThreadAttr_t ModDetectTask_attributes = {
  .name = "ModDetectTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for DemodTask */
osThreadId_t DemodTaskHandle;
const osThreadAttr_t DemodTask_attributes = {
  .name = "DemodTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for SI5351 */
osThreadId_t SI5351Handle;
const osThreadAttr_t SI5351_attributes = {
  .name = "SI5351",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for DDSQueue */
osMessageQueueId_t DDSQueueHandle;
const osMessageQueueAttr_t DDSQueue_attributes = {
  .name = "DDSQueue"
};
/* Definitions for PrintQueue */
osMessageQueueId_t PrintQueueHandle;
const osMessageQueueAttr_t PrintQueue_attributes = {
  .name = "PrintQueue"
};
/* Definitions for ModDetectQueue */
osMessageQueueId_t ModDetectQueueHandle;
const osMessageQueueAttr_t ModDetectQueue_attributes = {
  .name = "ModDetectQueue"
};
/* Definitions for DemodQueue */
osMessageQueueId_t DemodQueueHandle;
const osMessageQueueAttr_t DemodQueue_attributes = {
  .name = "DemodQueue"
};
/* Definitions for AdcFrameReadySem */
osSemaphoreId_t AdcFrameReadySemHandle;
const osSemaphoreAttr_t AdcFrameReadySem_attributes = {
  .name = "AdcFrameReadySem"
};
/* Definitions for SweepBlockReadySem */
osSemaphoreId_t SweepBlockReadySemHandle;
const osSemaphoreAttr_t SweepBlockReadySem_attributes = {
  .name = "SweepBlockReadySem"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDisplayTask(void *argument);
void StartTouchTask(void *argument);
void StartDDSTask(void *argument);
void StartPrintfTask(void *argument);
void StartAdcTask(void *argument);
void StartModDetectTask(void *argument);
void StartDemodTask(void *argument);
void StartSI5351(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of AdcFrameReadySem */
  AdcFrameReadySemHandle = osSemaphoreNew(1, 0, &AdcFrameReadySem_attributes);

  /* creation of SweepBlockReadySem */
  SweepBlockReadySemHandle = osSemaphoreNew(1, 1, &SweepBlockReadySem_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of DDSQueue */
  DDSQueueHandle = osMessageQueueNew (200, sizeof(AppDdsCmd), &DDSQueue_attributes);

  /* creation of PrintQueue */
  PrintQueueHandle = osMessageQueueNew (8, 128, &PrintQueue_attributes);

  /* creation of ModDetectQueue */
  ModDetectQueueHandle = osMessageQueueNew (8, sizeof(uint32_t), &ModDetectQueue_attributes);

  /* creation of DemodQueue */
  DemodQueueHandle = osMessageQueueNew (4, sizeof(uint32_t), &DemodQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  (void)AppDDS_BindRtosQueue(DDSQueueHandle);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of DisplayTask */
  DisplayTaskHandle = osThreadNew(StartDisplayTask, NULL, &DisplayTask_attributes);

  /* creation of TouchTask */
  TouchTaskHandle = osThreadNew(StartTouchTask, NULL, &TouchTask_attributes);

  /* creation of DDSTask */
  DDSTaskHandle = osThreadNew(StartDDSTask, NULL, &DDSTask_attributes);

  /* creation of PrintfTask */
  PrintfTaskHandle = osThreadNew(StartPrintfTask, NULL, &PrintfTask_attributes);

  /* creation of AdcTask */
  AdcTaskHandle = osThreadNew(StartAdcTask, NULL, &AdcTask_attributes);

  /* creation of ModDetectTask */
  ModDetectTaskHandle = osThreadNew(StartModDetectTask, NULL, &ModDetectTask_attributes);

  /* creation of DemodTask */
  DemodTaskHandle = osThreadNew(StartDemodTask, NULL, &DemodTask_attributes);

  /* creation of SI5351 */
  SI5351Handle = osThreadNew(StartSI5351, NULL, &SI5351_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* Keep touch responsive while demod is active. */
  if (TouchTaskHandle != NULL)
  {
    (void)osThreadSetPriority(TouchTaskHandle, osPriorityNormal);
  }
  if (DemodTaskHandle != NULL)
  {
    (void)osThreadSetPriority(DemodTaskHandle, osPriorityBelowNormal);
  }
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDisplayTask */
/**
  * @brief  Function implementing the DisplayTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDisplayTask */
__weak void StartDisplayTask(void *argument)
{
  /* USER CODE BEGIN StartDisplayTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDisplayTask */
}

/* USER CODE BEGIN Header_StartTouchTask */
/**
* @brief Function implementing the TouchTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTouchTask */
__weak void StartTouchTask(void *argument)
{
  /* USER CODE BEGIN StartTouchTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartTouchTask */
}

/* USER CODE BEGIN Header_StartDDSTask */
/**
* @brief Function implementing the DDSTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartDDSTask */
__weak void StartDDSTask(void *argument)
{
  /* USER CODE BEGIN StartDDSTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDDSTask */
}

/* USER CODE BEGIN Header_StartPrintfTask */
/**
* @brief Function implementing the PrintfTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartPrintfTask */
__weak void StartPrintfTask(void *argument)
{
  /* USER CODE BEGIN StartPrintfTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartPrintfTask */
}

/* USER CODE BEGIN Header_StartAdcTask */
/**
* @brief Function implementing the AdcTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartAdcTask */
__weak void StartAdcTask(void *argument)
{
  /* USER CODE BEGIN StartAdcTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartAdcTask */
}

/* USER CODE BEGIN Header_StartModDetectTask */
/**
* @brief Function implementing the ModDetectTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartModDetectTask */
__weak void StartModDetectTask(void *argument)
{
  /* USER CODE BEGIN StartModDetectTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartModDetectTask */
}

/* USER CODE BEGIN Header_StartDemodTask */
/**
* @brief Function implementing the DemodTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartDemodTask */
__weak void StartDemodTask(void *argument)
{
  /* USER CODE BEGIN StartDemodTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDemodTask */
}

/* USER CODE BEGIN Header_StartSI5351 */
/**
* @brief Function implementing the SI5351 thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartSI5351 */
__weak void StartSI5351(void *argument)
{
  /* USER CODE BEGIN StartSI5351 */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartSI5351 */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

