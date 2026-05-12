/*
 * AD9959 port 层头文件
 *
 * 文件定位：
 * - 这一层是 driver 层和硬件平台之间的适配层
 * - 它向 driver 层提供“平台原语”
 * - 它把具体 MCU、具体引脚、具体 HAL 调用隔离起来
 *
 * 注意边界：
 * - 这一层不应该知道 AD9959 的寄存器地址
 * - 这一层也不应该知道频率字、相位字如何计算
 * - 这一层只关心“把某根脚拉高/拉低”和“等待一小段时间”
 */
#ifndef AD9959_PORT_H
#define AD9959_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 平台初始化接口。
 *
 * 当前版本的职责：
 * 1. 不重复初始化 GPIO
 * 2. 只完成与 AD9959 驱动相关的运行时准备工作
 *
 * 目前这里主要用于：
 * - 初始化 DWT 周期计数器
 * - 为微秒级短延时提供基础
 */
void ad9959_port_init(void);

/* 下面这些函数统一使用 level = 0/1 作为输入。
 *
 * 这样 driver 层不用关心 HAL 的 GPIO_PIN_SET / GPIO_PIN_RESET 枚举，
 * 只需要表达“我要高电平”还是“我要低电平”。
 */

/* 片选信号。
 * 写寄存器时先拉低，写完后再拉高。
 */
void ad9959_port_write_cs(uint8_t level);

/* 软件 SPI 时钟信号。
 * driver 层通过控制这根脚，手工产生 SCLK 时序。
 */
void ad9959_port_write_sclk(uint8_t level);

/* UPDATE 脉冲输出。
 * 只有在该引脚产生有效脉冲后，前面写入的寄存器配置才会真正生效。
 */
void ad9959_port_write_update(uint8_t level);

/* AD9959 硬件复位脚。 */
void ad9959_port_write_reset(uint8_t level);

/* PDC 功耗/电源控制相关引脚。
 *
 * 当前最小点频版本只在初始化阶段设置默认状态，
 * 先不展开更复杂的低功耗或关断控制。
 */
void ad9959_port_write_pdc(uint8_t level);

/* Profile 选择脚。
 * 在更复杂的模式中可用于切换 Profile。
 * 当前最小点频版本先统一置 0，先不做复杂切换。
 */
void ad9959_port_write_sp0(uint8_t level);
void ad9959_port_write_sp1(uint8_t level);
void ad9959_port_write_sp2(uint8_t level);
void ad9959_port_write_sp3(uint8_t level);

/* 数据引脚。
 * 当前最小写寄存器版本只真正使用 SDIO0 输出串行数据，
 * 但先把 4 根线的接口都留出来，便于后续扩展并行/其他模式。
 */
void ad9959_port_write_sdio0(uint8_t level);
void ad9959_port_write_sdio1(uint8_t level);
void ad9959_port_write_sdio2(uint8_t level);
void ad9959_port_write_sdio3(uint8_t level);

/* 微秒级短延时接口。
 *
 * 当前版本采用 DWT 周期计数器实现。
 * 它适合驱动层短时序等待，不是 RTOS 的任务级延时。
 */
void ad9959_port_delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif
