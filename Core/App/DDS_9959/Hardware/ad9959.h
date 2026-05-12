/*
 * AD9959 驱动层头文件
 *
 * 文件定位：
 * - 这一层属于 driver 层
 * - 负责对上层暴露“能做什么”
 * - 不负责具体 GPIO 电平输出，也不直接调用 HAL_GPIO_WritePin()
 *
 * 本文件的职责：
 * 1. 定义 AD9959 对外可调用的接口
 * 2. 定义驱动层会用到的通道宏与寄存器地址宏
 * 3. 约定 app 层、driver 层、port 层的调用边界
 *
 * 当前建议的阅读顺序：
 * 1. 先看 CH0~CH3，理解多通道选择方式
 * 2. 再看 CSR/FR1/CFTW0/ACR/CPOW0 等寄存器地址
 * 3. 最后看 AD9959_Init / AD9959_Set_xxx / AD9959_WriteData 等接口
 */
#ifndef AD9959_H
#define AD9959_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CSR[7:4] 通道使能位。
 *
 * 这些宏不是“通道编号”，而是写入 CSR 寄存器时的通道选择位。
 * 保留与原厂例程一致的写法，方便你直接沿用参考调用风格：
 * AD9959_Set_Fre(CH0, 100000);
 */
#define CH0  ((uint8_t)0x10U)
#define CH1  ((uint8_t)0x20U)
#define CH2  ((uint8_t)0x40U)
#define CH3  ((uint8_t)0x80U)

/* AD9959 常用寄存器地址。
 *
 * 当前版本先保留“最小点频链路”需要的寄存器地址：
 * - CSR：选通道
 * - FR1/FR2：基础工作模式
 * - CFTW0：频率控制字
 * - CPOW0：相位控制字
 * - ACR：幅度控制
 *
 * 如果后面继续做调制、扫频、Profile 切换，再往这里补更多寄存器定义。
 */
#define CSR_ADD    0x00U  /* 通道选择寄存器 */
#define FR1_ADD    0x01U  /* 功能寄存器 1 */
#define FR2_ADD    0x02U  /* 功能寄存器 2 */
#define CFR_ADD    0x03U  /* 通道功能寄存器 */
#define CFTW0_ADD  0x04U  /* 频率转换字寄存器 */
#define CPOW0_ADD  0x05U  /* 相位转换字寄存器 */
#define ACR_ADD    0x06U  /* 幅度控制寄存器 */
#define LSRR_ADD   0x07U  /* 线性扫描斜率寄存器 */
#define RDW_ADD    0x08U  /* 上升增量寄存器 */
#define FDW_ADD    0x09U  /* 下降增量寄存器 */

/* 初始化 AD9959。
 *
 * 当前版本会完成：
 * 1. 调用 port 层做平台准备
 * 2. 设置各控制脚的默认电平
 * 3. 发送复位脉冲
 * 4. 写入 FR1/FR2 初值
 *
 * 这是整个驱动调用链的起点。
 */
void AD9959_Init(void);

/* 通过 UPDATE 引脚脉冲，让前面写入缓冲区的寄存器配置真正生效。
 *
 * 注意：
 * - Set_Fre / Set_Amp / Set_Phase 只是把配置写进寄存器缓冲区
 * - 真正统一生效要靠 IO_Update()
 * - 这也是多通道同步更新的关键
 */
void IO_Update(void);

/* 下面三个接口是当前版本已经可用的“原始控制量接口”。
 *
 * 它们适合底层调试和对照 AD9959 数据手册：
 * - AD9959_Set_Fre() 直接按 Hz 设置频率
 * - AD9959_Set_Amp() 直接按 0~1023 的幅度码设置
 * - AD9959_Set_Phase() 直接按 14 位相位字设置
 *
 * 这些接口应长期保留，因为它们最接近芯片原始控制模型。
 */
void AD9959_Set_Fre(uint8_t channel, uint32_t freq);
void AD9959_Set_Amp(uint8_t channel, uint16_t amp);
void AD9959_Set_Phase(uint8_t channel, uint16_t phase);

/* 按“角度”设置相位。
 *
 * 这是建立在 AD9959_Set_Phase() 之上的友好接口，
 * 方便上层直接写 0/90/180/270 这类直观角度值。
 */
void AD9959_Set_Phase_Deg(uint8_t channel, uint16_t degree);


/* 向 AD9959 某个寄存器连续写入 byte_count 个字节。
 *
 * 这是 driver 层最关键的底层协议函数：
 * 1. 先发送寄存器地址
 * 2. 再发送数据字节流
 * 3. 每个字节按 MSB -> LSB 顺序输出
 *
 * 上层所有 Set_xxx() 最终都会落到这里。
 */
void AD9959_WriteData(uint8_t reg_addr, uint8_t byte_count, const uint8_t *data);

/* 下面三个函数属于“把物理量/控制量转换成寄存器字”的封装接口。
 *
 * 它们仍然属于 driver 层，而不是 port 层，原因是：
 * - 这里已经包含 AD9959 寄存器结构知识
 * - 这里知道频率字、幅度字、相位字该怎么拼
 * - port 层只负责拉高拉低引脚，不负责这些协议细节
 */
void Write_CFTW0(uint32_t freq);
void Write_ACR(uint16_t amp);
void Write_CPOW0(uint16_t phase);

#ifdef __cplusplus
}
#endif

#endif
