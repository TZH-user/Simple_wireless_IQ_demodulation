/*
 * AD9959 应用层实现文件
 *
 * 文件定位：
 * - 这一层位于 main.c 和 ad9959 driver 层之间
 * - 它负责组合调用多个 driver 接口，形成“项目级默认策略”
 * - 它不直接操作 GPIO，也不直接写 AD9959 寄存器地址
 *
 * 当前文件的职责：
 * 1. 提供默认 4 通道初始化策略
 * 2. 提供 4 通道同频设置接口
 * 3. 提供 4 通道正交相位配置接口
 *
 * 当前模块的一个重要设计点：
 * - IO_Update() 仍然放在 app 层统一调用
 * - 不把 IO_Update() 塞回每个 driver 的 Set_xxx() 内部
 * - 这样更有利于多通道同步更新
 */
#include "app_ad9959.h"
#include "ad9959.h"

/* 把 app 层使用的通道索引 0~3，映射成 driver 层需要的 CH0~CH3 掩码。 */
static const uint8_t s_app_ad9959_channel_map[APP_AD9959_CHANNEL_COUNT] =
{
  CH0, CH1, CH2, CH3
};

/* 把任意整型幅度值限制到 driver 层当前接受的 0~1023 范围内。 */
static uint16_t app_ad9959_clamp_amp_code(int32_t amp_code)
{
  if (amp_code < 0)
  {
    return 0U;
  }

  if (amp_code > 1023)
  {
    return 1023U;
  }

  return (uint16_t)amp_code;
}

/* 把任意整型角度值整理成 0~359 度。
 *
 * 当前保持实现简单：
 * - 小于 0 时回到 0 度
 * - 大于等于 360 时取模
 */
static uint16_t app_ad9959_normalize_phase_deg(int32_t phase_deg)
{
  if (phase_deg < 0)
  {
    return 0U;
  }

  return (uint16_t)((uint32_t)phase_deg % 360U);
}

/* 把一个批量配置结构体全部填成“保持不变”。 */
void APP_AD9959_BuildKeepConfig(AppAd9959BatchConfig *cfg)
{
  uint32_t i;

  if (cfg == 0)
  {
    return;
  }

  for (i = 0U; i < APP_AD9959_CHANNEL_COUNT; ++i)
  {
    cfg->freq_hz[i] = APP_AD9959_KEEP;
    cfg->amp_code[i] = APP_AD9959_KEEP;
    cfg->phase_deg[i] = APP_AD9959_KEEP;
  }
}

/* 按批量配置结构体统一写入 4 通道参数，并在最后统一触发一次 UPDATE。
 *
 * 工作流程：
 * 1. 先遍历 4 路频率字段，写入所有需要改动的频率
 * 2. 再遍历 4 路幅度字段，写入所有需要改动的幅度
 * 3. 再遍历 4 路相位字段，写入所有需要改动的相位
 * 4. 如果本次至少改了一个参数，最后统一触发一次 IO_Update()
 *
 * 这样做的好处：
 * - 所有改动尽量在同一次 UPDATE 后一起生效
 * - 某些参数可以保持当前状态不动
 * - 后面扩展到 RTOS 控制层时，这就是很自然的“批量提交接口”
 */
void APP_AD9959_ApplyBatchConfig(const AppAd9959BatchConfig *cfg)
{
  uint32_t i;
  uint8_t changed = 0U;

  if (cfg == 0)
  {
    return;
  }

  for (i = 0U; i < APP_AD9959_CHANNEL_COUNT; ++i)
  {
    if (cfg->freq_hz[i] != APP_AD9959_KEEP)
    {
      AD9959_Set_Fre(s_app_ad9959_channel_map[i], (uint32_t)cfg->freq_hz[i]);
      changed = 1U;
    }
  }

  for (i = 0U; i < APP_AD9959_CHANNEL_COUNT; ++i)
  {
    if (cfg->amp_code[i] != APP_AD9959_KEEP)
    {
      AD9959_Set_Amp(s_app_ad9959_channel_map[i], app_ad9959_clamp_amp_code(cfg->amp_code[i]));
      changed = 1U;
    }
  }

  for (i = 0U; i < APP_AD9959_CHANNEL_COUNT; ++i)
  {
    if (cfg->phase_deg[i] != APP_AD9959_KEEP)
    {
      AD9959_Set_Phase_Deg(s_app_ad9959_channel_map[i], app_ad9959_normalize_phase_deg(cfg->phase_deg[i]));
      changed = 1U;
    }
  }

  if (changed != 0U)
  {
    IO_Update();
  }
}

/* 把 4 个通道配置成同频、同幅、0/90/180/270 度的正交输出。
 *
 * 参数说明：
 * - freq_hz：4 路统一频率，单位 Hz
 * - amp：4 路统一幅度码，当前按 driver 层约定使用 0~1023
 *
 * 工作流程：
 * 1. 先给 4 路写入同样的频率
 * 2. 再给 4 路写入同样的幅度
 * 3. 最后给 4 路分别写入 0/90/180/270 度相位
 * 4. 所有配置写完后统一触发一次 IO_Update()
 *
 * 这样做的目的：
 * - 让 4 路相位关系在一次 UPDATE 后统一建立
 * - 便于你用示波器 XY 模式观察相位关系
 */
void APP_AD9959_SetQuadPhaseDeg(uint32_t freq_hz, uint16_t amp)
{
  AppAd9959BatchConfig cfg;

  APP_AD9959_BuildKeepConfig(&cfg);

  cfg.freq_hz[0] = (int32_t)freq_hz;
  cfg.freq_hz[1] = (int32_t)freq_hz;
  cfg.freq_hz[2] = (int32_t)freq_hz;
  cfg.freq_hz[3] = (int32_t)freq_hz;

  cfg.amp_code[0] = (int32_t)amp;
  cfg.amp_code[1] = (int32_t)amp;
  cfg.amp_code[2] = (int32_t)amp;
  cfg.amp_code[3] = (int32_t)amp;

  cfg.phase_deg[0] = APP_AD9959_DEFAULT_PHASE_CH0;
  cfg.phase_deg[1] = APP_AD9959_DEFAULT_PHASE_CH1;
  cfg.phase_deg[2] = APP_AD9959_DEFAULT_PHASE_CH2;
  cfg.phase_deg[3] = APP_AD9959_DEFAULT_PHASE_CH3;

  APP_AD9959_ApplyBatchConfig(&cfg);
}

/* 按当前项目默认策略初始化 4 通道 AD9959 输出。
 *
 * 当前默认策略：
 * - 先完成 AD9959 基础初始化
 * - 再把 4 路配置成 100 kHz
 * - 幅度统一为 1023
 * - 相位统一设为 0/90/180/270 度
 *
 * 这相当于“项目上电默认配置”。
 * 后面如果你想改默认频率、默认相位或默认幅度，优先改这里，而不是直接改 main.c。
 */
void APP_AD9959_InitDefault4Ch(void)
{
  AD9959_Init();
  APP_AD9959_SetQuadPhaseDeg(APP_AD9959_DEFAULT_FREQ_HZ, APP_AD9959_STARTUP_AMP_CODE);
}

/* 把 4 个通道统一设置为相同频率。
 *
 * 参数：
 * - freq_hz：4 路统一频率，单位 Hz
 *
 * 这是一个对批量配置接口的简单封装。
 * 它适合“只想改 4 路频率，其他参数保持不变”的场景。
 */
void APP_AD9959_SetAllFreq(uint32_t freq_hz)
{
  AppAd9959BatchConfig cfg;

  APP_AD9959_BuildKeepConfig(&cfg);

  cfg.freq_hz[0] = (int32_t)freq_hz;
  cfg.freq_hz[1] = (int32_t)freq_hz;
  cfg.freq_hz[2] = (int32_t)freq_hz;
  cfg.freq_hz[3] = (int32_t)freq_hz;

  APP_AD9959_ApplyBatchConfig(&cfg);
}
