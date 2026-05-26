/*
 * AD9959 port 层实现文件
 *
 * 工作定位：
 * - 这是最靠近硬件的一层
 * - 负责把 driver 层的“逻辑动作”翻译成具体 GPIO 操作
 * - 所有 AD9959 相关引脚都通过 main.h 中 CubeMX 生成的宏来绑定
 *
 * 为什么这里看不到“变量绑定引脚”：
 * - 因为当前版本使用的是编译期宏绑定，而不是运行期结构体绑定
 * - 例如 AD9959_CS_GPIO_Port / AD9959_CS_Pin 都已经在 main.h 中定义好
 * - 这样写的好处是简单、直观，适合第一版移植教学
 *
 * 当前文件只做两件事：
 * 1. 提供 GPIO 电平输出包装
 * 2. 提供驱动层需要的微秒级短延时
 */
#include "ad9959_port.h"
#include "main.h"

/* 将 driver 层传下来的 0/1 电平语义，
 * 转换成 HAL_GPIO_WritePin() 需要的 GPIO_PinState 枚举。
 *
 * 参数 level：
 * - 0：输出低电平
 * - 非 0：输出高电平
 *
 * 返回值：
 * - GPIO_PIN_RESET：低电平
 * - GPIO_PIN_SET：高电平
 */
static GPIO_PinState ad9959_level(uint8_t level)
{
  return (level != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

/* 初始化 DWT 周期计数器。
 *
 * DWT(Data Watchpoint and Trace) 是 Cortex-M 内核里的调试/跟踪单元，
 * 其中 CYCCNT 会随着 CPU 时钟持续递增。
 *
 * 这里依次做了 3 件事：
 * 1. 打开跟踪调试模块
 * 2. 清零周期计数器
 * 3. 使能周期计数器开始计数
 *
 * 这样后面 ad9959_port_delay_us() 就能基于 CPU 周期数实现微秒延时。
 */
static void ad9959_port_delay_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* port 层初始化入口。
 *
 * 当前版本不重复初始化 GPIO，因为这些工作已经由 CubeMX 生成代码完成。
 * 这里真正负责的是：
 * - 初始化 DWT 周期计数器
 *
 * 这样 driver 层只需要调一次 AD9959_Init()，
 * 就会顺带具备微秒级短延时能力。
 */
void ad9959_port_init(void)
{
  ad9959_port_delay_init();
}

/* 微秒级短延时。
 *
 * 参数 us：
 * - 希望延时的时间，单位是微秒
 *
 * 关键局部变量：
 * - start：
 *   进入延时函数时的 DWT 周期计数起点
 * - ticks：
 *   本次延时目标对应的 CPU 周期数
 *
 * 计算思路：
 * - 先用 SystemCoreClock / 1000000U 得到“每微秒有多少个 CPU 周期”
 * - 再乘以 us，得到本次需要等待的总周期数
 *
 * 这是阻塞式短延时，适合驱动层：
 * - 复位脉冲
 * - UPDATE 脉冲
 * - 软件 SPI 时序等待
 *
 * 它不能替代 RTOS 的任务级延时。
 */
void ad9959_port_delay_us(uint32_t us)
{
  uint32_t start;
  uint32_t ticks;

  if (us == 0U)
  {
    return;
  }

  start = DWT->CYCCNT;
  ticks = us * (SystemCoreClock / 1000000U);

  while ((DWT->CYCCNT - start) < ticks)
  {
  }
}


/* 控制 CS 片选脚。
 * 写寄存器时，driver 层会先拉低 CS，结束后再拉高。
 */
void ad9959_port_write_cs(uint8_t level)
{
  HAL_GPIO_WritePin(AD9959_CS_GPIO_Port, AD9959_CS_Pin, ad9959_level(level));
}

/* 控制软件 SPI 时钟脚 SCLK。
 * driver 层通过反复拉低/拉高这根脚，手工构造串行时序。
 */
void ad9959_port_write_sclk(uint8_t level)
{
  HAL_GPIO_WritePin(AD9959_SCLK_GPIO_Port, AD9959_SCLK_Pin, ad9959_level(level));
}

/* 控制 UPDATE 脚。
 * AD9959 的很多寄存器值在串行写入后不会立刻生效，
 * 需要通过 UPDATE 脉冲把缓冲区内容送入活动寄存器。
 */
void ad9959_port_write_update(uint8_t level)
{
  HAL_GPIO_WritePin(AD9959_UPDATE_GPIO_Port, AD9959_UPDATE_Pin, ad9959_level(level));
}

/* 控制 RESET 复位脚。
 * driver 层会在初始化阶段输出一个复位脉冲。
 */
void ad9959_port_write_reset(uint8_t level)
{
  HAL_GPIO_WritePin(AD9959_RESET_GPIO_Port, AD9959_RESET_Pin, ad9959_level(level));
}

/* 控制 PDC 脚。
 * 当前最小版本只在初始化默认电平阶段把它拉到固定状态。
 */
void ad9959_port_write_pdc(uint8_t level)
{
  HAL_GPIO_WritePin(AD9959_PDC_GPIO_Port, AD9959_PDC_Pin, ad9959_level(level));
}

/* 控制 SP0。
 * 第一版最小点频功能先把这类 Profile 选择脚统一保持在默认状态。
 */
void ad9959_port_write_sp0(uint8_t level)
{
  HAL_GPIO_WritePin(AD9959_SP0_GPIO_Port, AD9959_SP0_Pin, ad9959_level(level));
}

/* 控制 SP1。 */
void ad9959_port_write_sp1(uint8_t level)
{
  HAL_GPIO_WritePin(AD9959_SP1_GPIO_Port, AD9959_SP1_Pin, ad9959_level(level));
}

/* 控制 SP2。 */
void ad9959_port_write_sp2(uint8_t level)
{
  HAL_GPIO_WritePin(AD9959_SP2_GPIO_Port, AD9959_SP2_Pin, ad9959_level(level));
}

/* 控制 SP3。 */
void ad9959_port_write_sp3(uint8_t level)
{
  HAL_GPIO_WritePin(AD9959_SP3_GPIO_Port, AD9959_SP3_Pin, ad9959_level(level));
}

/* 控制 SDIO0。
 * 在当前最小软件 SPI 实现里，真正用于逐位发送数据的就是这根线。
 */
void ad9959_port_write_sdio0(uint8_t level)
{
  HAL_GPIO_WritePin(AD9959_SDIO0_GPIO_Port, AD9959_SDIO0_Pin, ad9959_level(level));
}

/* 控制 SDIO1。
 * 当前版本先保留接口，不在最小写寄存器流程中使用。
 */
void ad9959_port_write_sdio1(uint8_t level)
{
  HAL_GPIO_WritePin(AD9959_SDIO1_GPIO_Port, AD9959_SDIO1_Pin, ad9959_level(level));
}

/* 控制 SDIO2。 */
void ad9959_port_write_sdio2(uint8_t level)
{
  HAL_GPIO_WritePin(AD9959_SDIO2_GPIO_Port, AD9959_SDIO2_Pin, ad9959_level(level));
}

/* 控制 SDIO3。 */
void ad9959_port_write_sdio3(uint8_t level)
{
  HAL_GPIO_WritePin(AD9959_SDIO3_GPIO_Port, AD9959_SDIO3_Pin, ad9959_level(level));
}
