/*
 * AD9959 driver 层实现文件
 *
 * 文件定位：
 * - 这一层负责 AD9959 协议级逻辑
 * - 它知道寄存器地址、频率字/相位字/幅度字怎么拼
 * - 它通过 port 层输出软件 SPI 时序，但不直接碰 HAL GPIO
 *
 * 当前版本已经实现：
 * - AD9959_Init()
 * - IO_Update()
 * - AD9959_WriteData()
 * - Write_CFTW0()
 * - Write_ACR()
 * - Write_CPOW0()
 * - AD9959_Set_Fre()
 * - AD9959_Set_Amp()
 * - AD9959_Set_Phase()
 * - AD9959_Set_Phase_Deg()
 *
 * 建议阅读顺序：
 * 1. 先看 AD9959_Init()，理解初始化链路
 * 2. 再看 ad9959_init_levels() 和 ad9959_reset_pulse()，理解默认电平和复位
 * 3. 再看 AD9959_WriteData()，理解软件 SPI 逐位发送
 * 4. 最后看 Write_xxx() 和 AD9959_Set_xxx()，理解“寄存器字封装”和“上层友好接口”
 */
#include "ad9959.h"
#include "ad9959_port.h"

/* FR1 的默认配置数据。
 * 这一组数据直接来自参考例程，含义是：
 * - 使能内部倍频相关配置
 * - 让 AD9959 工作在参考工程预期的系统时钟条件下
 *
 * 为什么用 static const：
 * - static：只在本文件内可见，不暴露给外部
 * - const：运行中不允许被意外改写
 */
static const uint8_t g_ad9959_fr1_data[3] = {0xD0U, 0x00U, 0x00U};

/* FR2 的默认配置数据。
 * 当前版本保留参考例程的默认双向扫描相关配置，
 * 虽然最小点频版本暂时还没真正用到扫描功能，
 * 但先保留与原始初始化行为一致，便于后续继续扩展。
 */
static const uint8_t g_ad9959_fr2_data[2] = {0x00U, 0x00U};

/* 设置 AD9959 所有控制脚的默认电平。
 *
 * 这一段对应原厂例程里的 Intserve()。
 * 作用是把芯片相关引脚先拉到一个已知、稳定的初始状态，
 * 避免上电后由于引脚悬空或前一次程序遗留状态导致行为不确定。
 *
 * 默认状态解释：
 * - PDC = 0：先保持默认功耗/使能状态
 * - CS = 1：空闲时片选拉高
 * - SCLK = 0：软件 SPI 时钟空闲拉低
 * - UPDATE = 0：默认不触发更新
 * - SP0~SP3 = 0：先不切换 Profile
 * - SDIO0~SDIO3 = 0：数据线先清零
 */
static void ad9959_init_levels(void)
{
  ad9959_port_write_pdc(0);
  ad9959_port_write_cs(1);
  ad9959_port_write_sclk(0);
  ad9959_port_write_update(0);
  ad9959_port_write_sp0(0);
  ad9959_port_write_sp1(0);
  ad9959_port_write_sp2(0);
  ad9959_port_write_sp3(0);
  ad9959_port_write_sdio0(0);
  ad9959_port_write_sdio1(0);
  ad9959_port_write_sdio2(0);
  ad9959_port_write_sdio3(0);
}

/* 产生 AD9959 的复位脉冲。
 *
 * 这一段对应原厂例程里的 IntReset()。
 * 复位时序是：
 * 1. RESET 先拉低
 * 2. 等待很短时间
 * 3. RESET 拉高，形成有效脉冲
 * 4. 等待一段时间，保证芯片完成内部复位
 * 5. RESET 再拉低，回到空闲状态
 *
 * 注意：
 * 当前 ad9959_port_delay_us() 已经改为 DWT 微秒延时。
 * 因此这段复位代码现在不仅结构正确，也具备了基础时序能力。
 */
static void ad9959_reset_pulse(void)
{
  ad9959_port_write_reset(0);
  ad9959_port_delay_us(1);
  ad9959_port_write_reset(1);
  ad9959_port_delay_us(30);
  ad9959_port_write_reset(0);
}

/* 初始化 AD9959。
 *
 * 完整流程：
 * 1. 调用 port 层初始化入口
 * 2. 设置引脚默认电平
 * 3. 发送复位脉冲
 * 4. 写入 FR1 缺省配置
 * 5. 写入 FR2 缺省配置
 *
 * 这里仍然不主动调用 IO_Update()，
 * 目的是把“初始化”和“统一更新生效”两个动作分开，便于教学观察。
 */
void AD9959_Init(void)
{
  ad9959_port_init();
  ad9959_init_levels();
  ad9959_reset_pulse();

  AD9959_WriteData(FR1_ADD, 3U, g_ad9959_fr1_data);
  AD9959_WriteData(FR2_ADD, 2U, g_ad9959_fr2_data);
}

/* 触发 UPDATE 脉冲。
 *
 * AD9959 的一个关键特性是：
 * 串行写寄存器并不总是立刻让配置生效，
 * 需要通过 UPDATE 引脚把缓冲区内容送入活动寄存器。
 *
 * 当前时序：
 * 1. UPDATE 拉低
 * 2. 短延时
 * 3. UPDATE 拉高
 * 4. 再短延时
 * 5. UPDATE 拉低
 */
void IO_Update(void)
{
  ad9959_port_write_update(0);
  ad9959_port_delay_us(2);
  ad9959_port_write_update(1);
  ad9959_port_delay_us(4);
  ad9959_port_write_update(0);
}

/* 使用软件 SPI 方式向 AD9959 连续写寄存器。
 *
 * 参数说明：
 * - reg_addr：要写的寄存器地址
 * - byte_count：本次连续写入的字节数
 * - data：待发送数据缓冲区起始地址
 *
 * 这段代码的工作流程：
 * 1. SCLK 先拉低，准备进入串行发送
 * 2. CS 拉低，选中 AD9959
 * 3. 发送 8 位寄存器地址，按高位到低位输出
 * 4. 再依次发送 byte_count 个数据字节
 * 5. 每个数据字节也按高位到低位输出
 * 6. 所有字节发完后，CS 拉高结束本次传输
 *
 * 关键局部变量作用：
 * - addr：
 *   reg_addr 的可移位副本。因为逐位发送时会反复左移，
 *   所以不能直接改原始输入参数。
 * - value：
 *   当前正在发送的数据字节副本。每发 1 位就左移 1 次。
 * - byte_index：
 *   当前发送到第几个数据字节。
 * - bit_index：
 *   当前发送到某个字节的第几位。
 */
void AD9959_WriteData(uint8_t reg_addr, uint8_t byte_count, const uint8_t *data)
{
  /* 保存寄存器地址的可修改副本。
   * 后面会通过左移把每一位依次送到 SDIO0。
   */
  uint8_t addr = reg_addr;

  /* 当前正在发送的 1 个数据字节副本。 */
  uint8_t value = 0U;

  /* 遍历 data[] 中每个数据字节的索引。 */
  uint8_t byte_index = 0U;

  /* 遍历某个字节内部 8 个 bit 的索引。 */
  uint8_t bit_index = 0U;

  /* 进入传输前先准备时钟和片选状态。 */
  ad9959_port_write_sclk(0);
  ad9959_port_write_cs(0);

  /* 第 1 段：发送寄存器地址。
   * 发送顺序是 MSB -> LSB，也就是最高位先发。
   */
  for (bit_index = 0U; bit_index < 8U; ++bit_index)
  {
    ad9959_port_write_sclk(0);

    /* 取当前最高位送到 SDIO0。
     * 如果 addr 的 bit7 为 1，就输出高电平，否则输出低电平。
     */
    ad9959_port_write_sdio0((addr & 0x80U) != 0U ? 1U : 0U);

    /* 上升沿前后完成 1 bit 的发送。 */
    ad9959_port_write_sclk(1);

    /* 左移 1 位，让下一位进入 bit7 位置，供下一轮发送。 */
    addr <<= 1;
  }

  ad9959_port_write_sclk(0);

  /* 第 2 段：连续发送数据字节流。 */
  for (byte_index = 0U; byte_index < byte_count; ++byte_index)
  {
    /* 取出当前字节副本，后面会对它逐位左移。 */
    value = data[byte_index];

    /* 发送当前字节的 8 个 bit，同样是 MSB -> LSB。 */
    for (bit_index = 0U; bit_index < 8U; ++bit_index)
    {
      ad9959_port_write_sclk(0);
      ad9959_port_write_sdio0((value & 0x80U) != 0U ? 1U : 0U);
      ad9959_port_write_sclk(1);

      /* 左移后，下一位继续进入 bit7 位置。 */
      value <<= 1;
    }

    /* 一个字节结束后，把时钟拉回低电平，准备下一字节。 */
    ad9959_port_write_sclk(0);
  }

  /* 结束本次串行传输。 */
  ad9959_port_write_cs(1);
}

/* 将目标输出频率转换为 32 位频率调谐字 FTW，并写入 CFTW0 寄存器。
 *
 * 参数 freq：
 * - 期望输出频率，单位 Hz
 *
 * 公式来源：
 *   FTW = fout * 2^32 / SYSCLK
 *
 * 当前这里默认采用参考例程的系统时钟条件：
 * - 外部参考时钟 25 MHz
 * - FR1 中设置内部倍频 20
 * - 因此 SYSCLK = 500 MHz
 *
 * 为什么这里用 uint64_t：
 * - 因为 (freq << 32) 很容易超过 32 位整数范围
 * - 先扩展到 64 位可以避免中间乘除过程溢出
 *
 * data[0] ~ data[3] 的含义：
 * - 分别保存 FTW 的高字节到低字节
 * - 发送顺序保持与原厂例程一致：MSB -> LSB
 */
void Write_CFTW0(uint32_t freq)
{
  uint8_t data[4];
  uint64_t ftw;

  ftw = ((uint64_t)freq << 32) / 500000000ULL;

  data[0] = (uint8_t)(ftw >> 24);
  data[1] = (uint8_t)(ftw >> 16);
  data[2] = (uint8_t)(ftw >> 8);
  data[3] = (uint8_t)ftw;

  AD9959_WriteData(CFTW0_ADD, 4U, data);
}

/* 将幅度值转换为 ACR 寄存器数据并写入。
 *
 * 参数 amp：
 * - 期望输出幅度控制值
 * - 当前版本延续原厂例程习惯，范围约定为 0 ~ 1023
 *
 * 关键变量：
 * - data[3]：
 *   将最终 ACR 内容拆分成 3 个字节发送
 * - acr_value：
 *   ACR 寄存器里真正要写入的低 16 位控制值
 *
 * amp &= 0x03FFU 的作用：
 * - 只保留 10 位有效幅度值
 * - 避免调用者传入超范围数据时污染其他控制位
 *
 * amp | 0x1000U 的作用：
 * - 保持与参考例程一致，打开幅度控制相关使能位
 */
void Write_ACR(uint16_t amp)
{
  uint8_t data[3] = {0U, 0U, 0U};
  uint16_t acr_value;

  amp &= 0x03FFU;
  acr_value = (uint16_t)(amp | 0x1000U);

  data[1] = (uint8_t)(acr_value >> 8);
  data[2] = (uint8_t)acr_value;

  AD9959_WriteData(ACR_ADD, 3U, data);
}

/* 将相位值转换为 CPOW0 寄存器数据并写入。
 *
 * 参数 phase：
 * - 相位控制字
 * - 当前版本延续原例程接口，取值范围约定为 0 ~ 16383
 * - 也就是 14 位相位字
 *
 * phase &= 0x3FFFU 的作用：
 * - 只保留 14 位有效相位值
 * - 防止高位脏数据被错误写入寄存器
 *
 * data[0] / data[1]：
 * - 分别保存高字节和低字节
 */
void Write_CPOW0(uint16_t phase)
{
  uint8_t data[2];

  phase &= 0x3FFFU;

  data[0] = (uint8_t)(phase >> 8);
  data[1] = (uint8_t)phase;

  AD9959_WriteData(CPOW0_ADD, 2U, data);
}

/* 设置指定通道的输出频率。
 *
 * 工作流程：
 * 1. 先写 CSR，告诉 AD9959 当前要操作哪个通道
 * 2. 再写 CFTW0，把该通道对应的频率控制字送进去
 *
 * 参数：
 * - channel：通道选择位，通常传 CH0/CH1/CH2/CH3
 * - freq：目标频率，单位 Hz
 *
 * 注意：
 * - 这里只是把寄存器值写入缓冲区
 * - 什么时候真正生效，仍由上层决定何时调用 IO_Update()
 */
void AD9959_Set_Fre(uint8_t channel, uint32_t freq)
{
  uint8_t channel_data[1] = {channel};

  AD9959_WriteData(CSR_ADD, 1U, channel_data);
  Write_CFTW0(freq);
}

/* 设置指定通道的输出幅度。
 *
 * 工作流程：
 * 1. 先写 CSR 选中通道
 * 2. 再写 ACR 幅度控制寄存器
 *
 * 参数：
 * - channel：通道选择位，通常传 CH0/CH1/CH2/CH3
 * - amp：幅度控制值，当前约定范围 0 ~ 1023
 *
 * 这是“原始幅度码接口”。
 * 如果后面你希望按 mVpp 这类物理量来控制，建议额外封装一个更高层的友好接口，
 * 但不要替代当前这个底层稳定接口。
 */
void AD9959_Set_Amp(uint8_t channel, uint16_t amp)
{
  uint8_t channel_data[1] = {channel};

  AD9959_WriteData(CSR_ADD, 1U, channel_data);
  Write_ACR(amp);
}

/* 设置指定通道的输出相位字。
 *
 * 工作流程：
 * 1. 先写 CSR 选中通道
 * 2. 再写 CPOW0 相位寄存器
 *
 * 参数：
 * - channel：通道选择位，通常传 CH0/CH1/CH2/CH3
 * - phase：相位控制字，当前约定范围 0 ~ 16383，4096 对应 90 度，8192 对应 180 度，12288 对应 270 度
 *
 * 当前版本把它保留为“底层原始接口”：
 * - 直接传入相位字
 * - 适合和数据手册、寄存器值一一对照
 *
 * 如果你想按 0/90/180/270 度来写，请用 AD9959_Set_Phase_Deg()。
 */
void AD9959_Set_Phase(uint8_t channel, uint16_t phase)
{
  uint8_t channel_data[1] = {channel};

  AD9959_WriteData(CSR_ADD, 1U, channel_data);
  Write_CPOW0(phase);
}

/* 按“角度”设置指定通道的输出相位。
 *
 * 参数：
 * - channel：通道选择位，通常传 CH0/CH1/CH2/CH3
 * - degree：角度值，单位是度，约定范围 0 ~ 360
 *
 * 工作流程：
 * 1. 先把角度限制到 0~359
 * 2. 再换算成 14 位相位字
 * 3. 最后复用 AD9959_Set_Phase() 完成真正的寄存器写入
 */
void AD9959_Set_Phase_Deg(uint8_t channel, uint16_t degree)
{
  uint16_t degree_norm;
  uint16_t phase_word;

  degree_norm = (uint16_t)(degree % 360U);
  phase_word = (uint16_t)((((uint32_t)degree_norm * 16384U) + 180U) / 360U);
  phase_word &= 0x3FFFU;

  AD9959_Set_Phase(channel, phase_word);
}
