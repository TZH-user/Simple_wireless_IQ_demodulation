#include "app_touch_gt9xx.h"

#include "../app_memory_map.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

#define APP_TOUCH_I2C_SCL_PORT TOUCH_SCLK_GPIO_Port
#define APP_TOUCH_I2C_SCL_PIN  TOUCH_SCLK_Pin
#define APP_TOUCH_I2C_SDA_PORT TOUCH_SDA_GPIO_Port
#define APP_TOUCH_I2C_SDA_PIN  TOUCH_SDA_Pin
#define APP_TOUCH_INT_PORT     TOUCH_INT_GPIO_Port
#define APP_TOUCH_INT_PIN      TOUCH_INT_Pin
#define APP_TOUCH_RST_PORT     TOUCH_RST_GPIO_Port
#define APP_TOUCH_RST_PIN      TOUCH_RST_Pin

#define APP_TOUCH_GT9XX_I2C_WADDR 0xBAU
#define APP_TOUCH_GT9XX_I2C_RADDR 0xBBU

#define APP_TOUCH_GT9XX_REG_PRODUCT_ID 0x8140U
#define APP_TOUCH_GT9XX_REG_STATUS     0x814EU

#define APP_TOUCH_MAX_POINTS       5U
#define APP_TOUCH_GT9XX_POINT_SIZE 8U
#define APP_TOUCH_GT9XX_FRAME_SIZE (2U + (APP_TOUCH_MAX_POINTS * APP_TOUCH_GT9XX_POINT_SIZE))
#define APP_TOUCH_POLL_TIMEOUT_MS  20U
#define APP_TOUCH_I2C_DELAY_CYCLES 40U

typedef struct
{
  uint8_t touched;
  uint8_t points;
  uint16_t x;
  uint16_t y;
} App_TouchState;

static App_TouchState g_touch_state = {0};
static uint8_t g_touch_inited = 0U;
static TaskHandle_t g_touch_task_handle = NULL;

static void App_TouchSetIntAsOutput(void);
static void App_TouchSetIntAsExti(void);
static void App_TouchGt9xxReset(void);
static void App_TouchGt9xxReadAndUpdateState(void);

static void App_TouchI2cDelay(void);
static void App_TouchI2cStart(void);
static void App_TouchI2cStop(void);
static uint8_t App_TouchI2cWriteByte(uint8_t data);
static uint8_t App_TouchI2cReadByte(uint8_t ack);
static uint8_t App_TouchI2cWaitAck(void);
static void App_TouchI2cSendAck(void);
static void App_TouchI2cSendNoAck(void);

static HAL_StatusTypeDef App_TouchGt9xxWriteReg(uint16_t reg, const uint8_t *buf, uint16_t len);
static HAL_StatusTypeDef App_TouchGt9xxReadReg(uint16_t reg, uint8_t *buf, uint16_t len);

bool App_TouchInit(void)
{
  uint8_t product_id[4] = {0};

  /* 软件 IIC 空闲态必须保持高电平，先释放总线再做复位时序。 */
  HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(APP_TOUCH_I2C_SDA_PORT, APP_TOUCH_I2C_SDA_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(APP_TOUCH_RST_PORT, APP_TOUCH_RST_PIN, GPIO_PIN_SET);

  App_TouchGt9xxReset();

  if(App_TouchGt9xxReadReg(APP_TOUCH_GT9XX_REG_PRODUCT_ID, product_id, sizeof(product_id)) != HAL_OK) {
    g_touch_inited = 0U;
    return false;
  }

  if(product_id[0] != (uint8_t)'9') {
    g_touch_inited = 0U;
    return false;
  }

  taskENTER_CRITICAL();
  memset(&g_touch_state, 0, sizeof(g_touch_state));
  g_touch_inited = 1U;
  taskEXIT_CRITICAL();

  return true;
}

void App_TouchPoll(void)
{
  /* 保留轮询入口，便于早期联调；主线仍优先走 EXTI 事件驱动。 */
  if(g_touch_inited == 0U) {
    return;
  }

  App_TouchGt9xxReadAndUpdateState();
}

bool App_TouchGetPoint(uint16_t *x, uint16_t *y)
{
  App_TouchState local = {0};

  if((x == NULL) || (y == NULL)) {
    return false;
  }

  taskENTER_CRITICAL();
  local = g_touch_state;
  taskEXIT_CRITICAL();

  *x = local.x;
  *y = local.y;

  return (local.touched != 0U);
}

uint8_t App_TouchGetPointCount(void)
{
  uint8_t count = 0U;

  taskENTER_CRITICAL();
  count = g_touch_state.points;
  taskEXIT_CRITICAL();

  return count;
}

void App_TouchNotifyFromISR(uint16_t gpio_pin)
{
  BaseType_t higher_priority_woken = pdFALSE;

  /* 仅处理触摸中断线，防止与同组 EXTI 其他引脚混淆。 */
  if(gpio_pin != APP_TOUCH_INT_PIN) {
    return;
  }
  if(g_touch_task_handle == NULL) {
    return;
  }

  /*
   * 约束：该 ISR 会调用 FreeRTOS FromISR API，
   * 对应 EXTI NVIC 优先级必须满足 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 约束。
   */
  vTaskNotifyGiveFromISR(g_touch_task_handle, &higher_priority_woken);
  portYIELD_FROM_ISR(higher_priority_woken);
}

void App_TouchTaskRun(void)
{
  g_touch_task_handle = xTaskGetCurrentTaskHandle();

  for(;;) {
    if(g_touch_inited == 0U) {
      (void)App_TouchInit();
    }

    /*
     * 等待 EXTI 通知，超时后也做一次兜底轮询：
     * 1. 防止漏中断后状态长期不更新
     * 2. 保持最小可观测性，便于联调阶段定位硬件问题
     */
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_TOUCH_POLL_TIMEOUT_MS));
    App_TouchPoll();
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  /* 注意：工程内若新增其他 EXTI 外设，需要在这里继续做引脚分发。 */
  App_TouchNotifyFromISR(GPIO_Pin);
}

static void App_TouchSetIntAsOutput(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = APP_TOUCH_INT_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(APP_TOUCH_INT_PORT, &gpio);
}

static void App_TouchSetIntAsExti(void)
{
  GPIO_InitTypeDef gpio = {0};

  gpio.Pin = APP_TOUCH_INT_PIN;
  gpio.Mode = GPIO_MODE_IT_FALLING;
  gpio.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(APP_TOUCH_INT_PORT, &gpio);
}

static void App_TouchGt9xxReset(void)
{
  /*
   * GT9xx 复位阶段需要临时控制 INT 电平，
   * 复位结束后必须切回 EXTI 输入模式，否则中断不会触发。
   */
  App_TouchSetIntAsOutput();

  HAL_GPIO_WritePin(APP_TOUCH_INT_PORT, APP_TOUCH_INT_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(APP_TOUCH_RST_PORT, APP_TOUCH_RST_PIN, GPIO_PIN_SET);
  HAL_Delay(2);

  HAL_GPIO_WritePin(APP_TOUCH_RST_PORT, APP_TOUCH_RST_PIN, GPIO_PIN_RESET);
  HAL_Delay(10);
  HAL_GPIO_WritePin(APP_TOUCH_RST_PORT, APP_TOUCH_RST_PIN, GPIO_PIN_SET);
  HAL_Delay(60);

  App_TouchSetIntAsExti();
  HAL_Delay(10);
}

static void App_TouchGt9xxReadAndUpdateState(void)
{
  uint8_t frame[APP_TOUCH_GT9XX_FRAME_SIZE] = {0};
  uint8_t status = 0U;
  uint8_t points = 0U;
  uint16_t point0_base = 2U;
  uint16_t x = 0U;
  uint16_t y = 0U;
  uint8_t clear = 0U;

  if(App_TouchGt9xxReadReg(APP_TOUCH_GT9XX_REG_STATUS, frame, APP_TOUCH_GT9XX_FRAME_SIZE) != HAL_OK) {
    return;
  }
  status = frame[0];

  /* bit7=1 表示有新数据，未置位则无需读取点数据区。 */
  if((status & 0x80U) == 0U) {
    return;
  }

  points = (uint8_t)(status & 0x0FU);
  if((points >= 1U) && (points <= APP_TOUCH_MAX_POINTS)) {
    x = (uint16_t)(((uint16_t)frame[point0_base + 1U] << 8) | frame[point0_base + 0U]);
    y = (uint16_t)(((uint16_t)frame[point0_base + 3U] << 8) | frame[point0_base + 2U]);

    if(x >= APP_LCD_HOR_RES) {
      x = (uint16_t)(APP_LCD_HOR_RES - 1U);
    }
    if(y >= APP_LCD_VER_RES) {
      y = (uint16_t)(APP_LCD_VER_RES - 1U);
    }

    taskENTER_CRITICAL();
    g_touch_state.touched = 1U;
    g_touch_state.points = points;
    g_touch_state.x = x;
    g_touch_state.y = y;
    taskEXIT_CRITICAL();
  }
  else {
    taskENTER_CRITICAL();
    g_touch_state.touched = 0U;
    g_touch_state.points = 0U;
    taskEXIT_CRITICAL();
  }

  (void)App_TouchGt9xxWriteReg(APP_TOUCH_GT9XX_REG_STATUS, &clear, 1U);
}

static void App_TouchI2cDelay(void)
{
  /*
   * 软件 IIC 延时常数受主频与编译优化影响明显。
   * 这个值只保证当前板卡可用，后续若升到 400kHz 需配合示波器复核波形。
   */
  volatile uint32_t i = 0U;
  for(i = 0U; i < APP_TOUCH_I2C_DELAY_CYCLES; i++) {
    __NOP();
  }
}

static void App_TouchI2cStart(void)
{
  HAL_GPIO_WritePin(APP_TOUCH_I2C_SDA_PORT, APP_TOUCH_I2C_SDA_PIN, GPIO_PIN_SET);
  HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_SET);
  App_TouchI2cDelay();

  HAL_GPIO_WritePin(APP_TOUCH_I2C_SDA_PORT, APP_TOUCH_I2C_SDA_PIN, GPIO_PIN_RESET);
  App_TouchI2cDelay();
  HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_RESET);
  App_TouchI2cDelay();
}

static void App_TouchI2cStop(void)
{
  HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_RESET);
  App_TouchI2cDelay();
  HAL_GPIO_WritePin(APP_TOUCH_I2C_SDA_PORT, APP_TOUCH_I2C_SDA_PIN, GPIO_PIN_RESET);
  App_TouchI2cDelay();

  HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_SET);
  App_TouchI2cDelay();
  HAL_GPIO_WritePin(APP_TOUCH_I2C_SDA_PORT, APP_TOUCH_I2C_SDA_PIN, GPIO_PIN_SET);
  App_TouchI2cDelay();
}

static uint8_t App_TouchI2cWaitAck(void)
{
  HAL_GPIO_WritePin(APP_TOUCH_I2C_SDA_PORT, APP_TOUCH_I2C_SDA_PIN, GPIO_PIN_SET);
  App_TouchI2cDelay();

  HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_SET);
  App_TouchI2cDelay();

  if(HAL_GPIO_ReadPin(APP_TOUCH_I2C_SDA_PORT, APP_TOUCH_I2C_SDA_PIN) != GPIO_PIN_RESET) {
    HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_RESET);
    App_TouchI2cDelay();
    return 0U;
  }

  HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_RESET);
  App_TouchI2cDelay();
  return 1U;
}

static void App_TouchI2cSendAck(void)
{
  HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_RESET);
  App_TouchI2cDelay();
  HAL_GPIO_WritePin(APP_TOUCH_I2C_SDA_PORT, APP_TOUCH_I2C_SDA_PIN, GPIO_PIN_RESET);
  App_TouchI2cDelay();

  HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_SET);
  App_TouchI2cDelay();

  HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(APP_TOUCH_I2C_SDA_PORT, APP_TOUCH_I2C_SDA_PIN, GPIO_PIN_SET);
  App_TouchI2cDelay();
}

static void App_TouchI2cSendNoAck(void)
{
  HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_RESET);
  App_TouchI2cDelay();
  HAL_GPIO_WritePin(APP_TOUCH_I2C_SDA_PORT, APP_TOUCH_I2C_SDA_PIN, GPIO_PIN_SET);
  App_TouchI2cDelay();

  HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_SET);
  App_TouchI2cDelay();

  HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_RESET);
  App_TouchI2cDelay();
}

static uint8_t App_TouchI2cWriteByte(uint8_t data)
{
  uint8_t bit = 0U;

  for(bit = 0U; bit < 8U; bit++) {
    HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_RESET);
    if((data & 0x80U) != 0U) {
      HAL_GPIO_WritePin(APP_TOUCH_I2C_SDA_PORT, APP_TOUCH_I2C_SDA_PIN, GPIO_PIN_SET);
    }
    else {
      HAL_GPIO_WritePin(APP_TOUCH_I2C_SDA_PORT, APP_TOUCH_I2C_SDA_PIN, GPIO_PIN_RESET);
    }

    App_TouchI2cDelay();
    HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_SET);
    App_TouchI2cDelay();

    data <<= 1;
  }

  HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(APP_TOUCH_I2C_SDA_PORT, APP_TOUCH_I2C_SDA_PIN, GPIO_PIN_SET);

  return App_TouchI2cWaitAck();
}

static uint8_t App_TouchI2cReadByte(uint8_t ack)
{
  uint8_t bit = 0U;
  uint8_t data = 0U;

  HAL_GPIO_WritePin(APP_TOUCH_I2C_SDA_PORT, APP_TOUCH_I2C_SDA_PIN, GPIO_PIN_SET);

  for(bit = 0U; bit < 8U; bit++) {
    data <<= 1;
    HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_SET);
    App_TouchI2cDelay();

    if(HAL_GPIO_ReadPin(APP_TOUCH_I2C_SDA_PORT, APP_TOUCH_I2C_SDA_PIN) == GPIO_PIN_SET) {
      data |= 1U;
    }

    HAL_GPIO_WritePin(APP_TOUCH_I2C_SCL_PORT, APP_TOUCH_I2C_SCL_PIN, GPIO_PIN_RESET);
    App_TouchI2cDelay();
  }

  if(ack != 0U) {
    App_TouchI2cSendAck();
  }
  else {
    App_TouchI2cSendNoAck();
  }

  return data;
}

static HAL_StatusTypeDef App_TouchGt9xxWriteReg(uint16_t reg, const uint8_t *buf, uint16_t len)
{
  uint16_t i = 0U;

  if((buf == NULL) && (len > 0U)) {
    return HAL_ERROR;
  }

  App_TouchI2cStart();
  if(App_TouchI2cWriteByte(APP_TOUCH_GT9XX_I2C_WADDR) == 0U) {
    App_TouchI2cStop();
    return HAL_ERROR;
  }
  if(App_TouchI2cWriteByte((uint8_t)(reg >> 8)) == 0U) {
    App_TouchI2cStop();
    return HAL_ERROR;
  }
  if(App_TouchI2cWriteByte((uint8_t)reg) == 0U) {
    App_TouchI2cStop();
    return HAL_ERROR;
  }

  for(i = 0U; i < len; i++) {
    if(App_TouchI2cWriteByte(buf[i]) == 0U) {
      App_TouchI2cStop();
      return HAL_ERROR;
    }
  }

  App_TouchI2cStop();
  return HAL_OK;
}

static HAL_StatusTypeDef App_TouchGt9xxReadReg(uint16_t reg, uint8_t *buf, uint16_t len)
{
  uint16_t i = 0U;

  if((buf == NULL) || (len == 0U)) {
    return HAL_ERROR;
  }

  App_TouchI2cStart();
  if(App_TouchI2cWriteByte(APP_TOUCH_GT9XX_I2C_WADDR) == 0U) {
    App_TouchI2cStop();
    return HAL_ERROR;
  }
  if(App_TouchI2cWriteByte((uint8_t)(reg >> 8)) == 0U) {
    App_TouchI2cStop();
    return HAL_ERROR;
  }
  if(App_TouchI2cWriteByte((uint8_t)reg) == 0U) {
    App_TouchI2cStop();
    return HAL_ERROR;
  }

  App_TouchI2cStart();
  if(App_TouchI2cWriteByte(APP_TOUCH_GT9XX_I2C_RADDR) == 0U) {
    App_TouchI2cStop();
    return HAL_ERROR;
  }

  for(i = 0U; i < len; i++) {
    buf[i] = App_TouchI2cReadByte((i < (len - 1U)) ? 1U : 0U);
  }

  App_TouchI2cStop();
  return HAL_OK;
}
