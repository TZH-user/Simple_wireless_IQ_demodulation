/*
 * DDS 控制层实现文件
 *
 * 文件定位：
 * - 这一层负责“运行时控制 DDS”
 * - 它不直接写 AD9959 寄存器
 * - 它通过 app_ad9959.c 提供的批量配置接口，把缓存状态统一提交给底层
 *
 * 当前版本是第一版骨架：
 * - 先不接 RTOS 队列
 * - 先不接 UI/串口命令
 * - 先把状态缓存、选通道、统一 Apply 的流程理顺
 */
#include "app_dds_ctrl.h"
#include "app_ad9959.h"

#include <string.h>

/* 控制层的全局状态缓存。
 *
 * 当前版本采用静态全局变量，原因是：
 * - DDS 作为单一硬件资源，通常只有一个运行时状态
 * - 后面接 RTOS 时，这里也仍然会是控制层共享状态的基础
 */
static AppDdsStatus s_dds;
static osMessageQueueId_t s_dds_rtos_queue = 0;

/* 教学版软件命令队列。
 *
 * 当前还没有真正接入 FreeRTOS QueueHandle_t，
 * 所以先用一个简单环形队列模拟“命令先入队，再由拥有者任务处理”的行为。
 *
 * 你现在最该学到的是：
 * - cmd 变量只是单条命令包
 * - 真正入队后，队列内部会拷贝一份命令内容
 * - 后续调用者就可以安全复用自己的 cmd 变量
 */
typedef struct
{
  AppDdsCmd buffer[APP_DDS_CMD_QUEUE_LEN];
  uint8_t head;
  uint8_t tail;
  uint8_t count;
} AppDdsCmdQueue;

static AppDdsCmdQueue s_dds_cmd_queue;

/* 把幅度码限制到当前 AD9959 driver 约定的范围内。 */
static uint16_t AppDDS_ClampAmp(uint16_t amp_code)
{
  if (amp_code > 1023U)
  {
    return 1023U;
  }

  return amp_code;
}

/* 把相位角限制到 0~359 度。 */
static uint16_t AppDDS_NormalizePhaseDeg(uint16_t phase_deg)
{
  return (uint16_t)(phase_deg % 360U);
}

/* 重置软件命令队列到空状态。 */
static void AppDDS_QueueReset(void)
{
  s_dds_cmd_queue.head = 0U;
  s_dds_cmd_queue.tail = 0U;
  s_dds_cmd_queue.count = 0U;
}

/* 向软件命令队列压入一条命令。
 *
 * 注意：
 * - 这里会复制 *cmd 的内容到队列 buffer 里
 * - 所以调用者后续立刻覆盖自己的 cmd 局部变量也没问题
 */
static int AppDDS_QueuePush(const AppDdsCmd *cmd)
{
  if (s_dds_cmd_queue.count >= APP_DDS_CMD_QUEUE_LEN)
  {
    return -5;
  }

  s_dds_cmd_queue.buffer[s_dds_cmd_queue.tail] = *cmd;
  s_dds_cmd_queue.tail = (uint8_t)((s_dds_cmd_queue.tail + 1U) % APP_DDS_CMD_QUEUE_LEN);
  s_dds_cmd_queue.count++;
  return 0;
}

/* 从软件命令队列弹出一条命令。 */
static int AppDDS_QueuePop(AppDdsCmd *cmd)
{
  if (s_dds_cmd_queue.count == 0U)
  {
    return 1;
  }

  *cmd = s_dds_cmd_queue.buffer[s_dds_cmd_queue.head];
  s_dds_cmd_queue.head = (uint8_t)((s_dds_cmd_queue.head + 1U) % APP_DDS_CMD_QUEUE_LEN);
  s_dds_cmd_queue.count--;
  return 0;
}

/* 构造“初始化 DDS”命令。 */
AppDdsCmd AppDDS_MakeInitCmd(void)
{
  AppDdsCmd cmd;

  memset(&cmd, 0, sizeof(cmd));

  cmd.type = APP_DDS_CMD_INIT;
  return cmd;
}

/* 构造“选择通道”命令。 */
AppDdsCmd AppDDS_MakeSelectChCmd(uint8_t ch)
{
  AppDdsCmd cmd;

  memset(&cmd, 0, sizeof(cmd));

  cmd.type = APP_DDS_CMD_SELECT_CH;
  cmd.ch = ch;
  return cmd;
}

/* 构造“设置频率”命令。 */
AppDdsCmd AppDDS_MakeSetFreqCmd(uint32_t freq_hz)
{
  AppDdsCmd cmd;

  memset(&cmd, 0, sizeof(cmd));

  cmd.type = APP_DDS_CMD_SET_FREQ;
  cmd.u32 = freq_hz;
  return cmd;
}

/* 构造“设置幅度码”命令。 */
AppDdsCmd AppDDS_MakeSetChFreqApplyCmd(uint8_t ch, uint32_t freq_hz)
{
  AppDdsCmd cmd;

  memset(&cmd, 0, sizeof(cmd));

  cmd.type = APP_DDS_CMD_SET_CH_FREQ_APPLY;
  cmd.ch = ch;
  cmd.u32 = freq_hz;
  return cmd;
}

AppDdsCmd AppDDS_MakeSetAmpCmd(uint16_t amp_code)
{
  AppDdsCmd cmd;

  memset(&cmd, 0, sizeof(cmd));

  cmd.type = APP_DDS_CMD_SET_AMP;
  cmd.u16 = amp_code;
  return cmd;
}

/* 构造“设置相位角”命令。 */
AppDdsCmd AppDDS_MakeSetPhaseDegCmd(uint16_t phase_deg)
{
  AppDdsCmd cmd;

  memset(&cmd, 0, sizeof(cmd));

  cmd.type = APP_DDS_CMD_SET_PHASE_DEG;
  cmd.u16 = phase_deg;
  return cmd;
}

/* 手动相位微调使用：指定通道设置相位并立即 Apply。 */
AppDdsCmd AppDDS_MakeSetChPhaseApplyCmd(uint8_t ch, uint16_t phase_deg)
{
  AppDdsCmd cmd;

  memset(&cmd, 0, sizeof(cmd));

  cmd.type = APP_DDS_CMD_SET_CH_PHASE_APPLY;
  cmd.ch = ch;
  cmd.u16 = phase_deg;
  return cmd;
}

/* 构造“统一提交缓存参数”命令。 */
AppDdsCmd AppDDS_MakeApplyCmd(void)
{
  AppDdsCmd cmd;

  memset(&cmd, 0, sizeof(cmd));

  cmd.type = APP_DDS_CMD_APPLY;
  return cmd;
}

/* 初始化 DDS 控制层。
 *
 * 当前流程：
 * 1. 先清空状态结构体
 * 2. 建立和 APP_AD9959_InitDefault4Ch() 一致的默认缓存
 * 3. 调用应用层完成底层默认初始化
 * 4. 标记硬件已准备好
 *
 * 说明：
 * - 这里看起来像“写了两次默认值”，其实职责不同
 * - s_dds.xxx[] 是控制层自己的状态缓存
 * - APP_AD9959_InitDefault4Ch() 是真正下发到底层硬件的默认配置
 *
 * 为了避免魔法数字重复，默认参数已经和 app_ad9959.h 里的默认宏统一。
 */
int AppDDS_Init(void)
{
  memset(&s_dds, 0, sizeof(s_dds));  // 先清零状态结构体
  // 再建立默认缓存，和 APP_AD9959_InitDefault4Ch() 里下发的默认配置一致
  s_dds.freq_hz[0] = APP_AD9959_DEFAULT_FREQ_HZ;
  s_dds.freq_hz[1] = APP_AD9959_DEFAULT_FREQ_HZ;
  s_dds.freq_hz[2] = APP_AD9959_DEFAULT_FREQ_HZ;
  s_dds.freq_hz[3] = APP_AD9959_DEFAULT_FREQ_HZ;

  s_dds.amp_code[0] = APP_AD9959_STARTUP_CH0_AMP_CODE;
  s_dds.amp_code[1] = APP_AD9959_STARTUP_CH1_AMP_CODE;
  s_dds.amp_code[2] = APP_AD9959_STARTUP_CH2_AMP_CODE;
  s_dds.amp_code[3] = APP_AD9959_STARTUP_CH3_AMP_CODE;

  s_dds.phase_deg[0] = APP_AD9959_DEFAULT_PHASE_CH0;
  s_dds.phase_deg[1] = APP_AD9959_DEFAULT_PHASE_CH1;
  s_dds.phase_deg[2] = APP_AD9959_DEFAULT_PHASE_CH2;
  s_dds.phase_deg[3] = APP_AD9959_DEFAULT_PHASE_CH3;
  // 默认选中通道 0，默认硬件未准备好，默认无脏通道，默认无错误
  s_dds.selected_ch = 0U;
  s_dds.hw_ready = 0U;
  s_dds.dirty_mask = 0U;
  s_dds.last_err = 0;
  AppDDS_QueueReset();  // 初始化软件命令队列

  APP_AD9959_InitDefault4Ch();
  // 标记硬件已准备好，允许后续 Apply 提交到底层
  s_dds.hw_ready = 1U;
  return 0;
}

/* 选择当前逻辑通道。
 *
 * 注意：
 * - 这里只改控制层缓存
 * - 不直接写硬件
 */
int AppDDS_SelectChannel(uint8_t ch)
{
  if (ch >= APP_DDS_CHANNEL_COUNT)
  {
    s_dds.last_err = -1;
    return -1;
  }

  s_dds.selected_ch = ch;
  s_dds.last_err = 0;
  return 0;
}

/* 修改当前选中通道的频率缓存，并置脏标记。 */
int AppDDS_SetFreq(uint32_t freq_hz)
{
  uint8_t ch = s_dds.selected_ch;

  s_dds.freq_hz[ch] = freq_hz;
  s_dds.dirty_mask = (uint8_t)(s_dds.dirty_mask | (uint8_t)(1U << ch));
  s_dds.last_err = 0;
  return 0;
}

/* 修改当前选中通道的幅度缓存，并置脏标记。 */
int AppDDS_SetAmp(uint16_t amp_code)
{
  uint8_t ch = s_dds.selected_ch;

  s_dds.amp_code[ch] = AppDDS_ClampAmp(amp_code);
  s_dds.dirty_mask = (uint8_t)(s_dds.dirty_mask | (uint8_t)(1U << ch));
  s_dds.last_err = 0;
  return 0;
}

/* 修改当前选中通道的相位缓存，并置脏标记。 */
int AppDDS_SetPhaseDeg(uint16_t phase_deg)
{
  uint8_t ch = s_dds.selected_ch;

  s_dds.phase_deg[ch] = AppDDS_NormalizePhaseDeg(phase_deg);
  s_dds.dirty_mask = (uint8_t)(s_dds.dirty_mask | (uint8_t)(1U << ch));
  s_dds.last_err = 0;
  return 0;
}

/* 把所有脏通道的缓存统一提交到底层 DDS。
 *
 * 当前版本使用 AppAd9959BatchConfig 作为“本次提交请求”：
 * 1. 先把所有字段置为 APP_AD9959_KEEP
 * 2. 再只填入脏通道当前缓存的频率/幅度/相位
 * 3. 最后统一调用 APP_AD9959_ApplyBatchConfig()
 *
 * 这一版采用“通道级脏标记”而不是“参数级脏标记”。
 * 也就是说：
 * - 某个通道只要有一个参数变了
 * - Apply 时就把这个通道当前缓存的 3 个参数一起提交
 */
int AppDDS_Apply(void)
{
  AppAd9959BatchConfig cfg;
  uint32_t ch;
  // 如果硬件还没准备好，就返回错误，不提交任何配置
  if (s_dds.hw_ready == 0U)
  {
    s_dds.last_err = -2;
    return -2;
  }
  // 如果没有任何脏通道，就不提交，直接返回成功
  if (s_dds.dirty_mask == 0U)
  {
    s_dds.last_err = 0;
    return 0;
  }
  // 构建批量配置结构体，先把全部字段置为“保持不变”
  APP_AD9959_BuildKeepConfig(&cfg);
  // 再把脏通道的当前缓存值填入批量配置结构体
  for (ch = 0U; ch < APP_DDS_CHANNEL_COUNT; ++ch)
  {
    if ((s_dds.dirty_mask & (uint8_t)(1U << ch)) != 0U)
    {
      cfg.freq_hz[ch] = (int32_t)s_dds.freq_hz[ch];
      cfg.amp_code[ch] = (int32_t)s_dds.amp_code[ch];
      cfg.phase_deg[ch] = (int32_t)s_dds.phase_deg[ch];
    }
  }
  // 最后统一提交批量配置到底层
  APP_AD9959_ApplyBatchConfig(&cfg);
  // 提交完成后，清除脏标记
  s_dds.dirty_mask = 0U;
  s_dds.last_err = 0;
  return 0;
}

/* 返回当前 DDS 状态缓存的只读指针。 */
const AppDdsStatus *AppDDS_GetStatus(void)
{
  return &s_dds;
}

/* 统一执行一条 DDS 控制命令。
 *
 * 设计目的：
 * - 在真正接 RTOS 队列之前，先把“命令 -> 控制层函数”的映射固定下来
 * - 以后无论命令来自串口、UI 还是算法层，都可以复用这一条分发路径
 *
 * 参数说明：
 * - cmd：
 *   指向待执行命令结构体的指针。
 *   调用者需要提前把 type 以及对应字段填好。
 *
 * 返回值说明：
 * - 0：
 *   命令执行成功
 * - -3：
 *   cmd 指针为空
 * - -4：
 *   命令类型不支持或未识别
 *
 * 当前命令字段约定：
 * - APP_DDS_CMD_INIT         ：忽略 u32/u16/ch
 * - APP_DDS_CMD_SELECT_CH    ：使用 ch
 * - APP_DDS_CMD_SET_FREQ     ：使用 u32，单位 Hz
 * - APP_DDS_CMD_SET_AMP      ：使用 u16，幅度码
 * - APP_DDS_CMD_SET_PHASE_DEG：使用 u16，单位度
 * - APP_DDS_CMD_APPLY        ：忽略 u32/u16/ch
 */
int AppDDS_ExecuteCmd(const AppDdsCmd *cmd)
{ // 先检查 cmd 参数是否合法
  if (cmd == 0)
  {
    s_dds.last_err = -3;
    return -3;
  }
  // 根据命令类型分发到对应的控制层函数
  switch (cmd->type)
  {
    case APP_DDS_CMD_INIT:
      return AppDDS_Init();

    case APP_DDS_CMD_SELECT_CH:
      return AppDDS_SelectChannel(cmd->ch);

    case APP_DDS_CMD_SET_FREQ:
      return AppDDS_SetFreq(cmd->u32);

    case APP_DDS_CMD_SET_AMP:
      return AppDDS_SetAmp(cmd->u16);

    case APP_DDS_CMD_SET_PHASE_DEG:
      return AppDDS_SetPhaseDeg(cmd->u16);

    case APP_DDS_CMD_APPLY:
      return AppDDS_Apply();

    case APP_DDS_CMD_SET_CH_FREQ_APPLY:
    {
      int ret;

      ret = AppDDS_SelectChannel(cmd->ch);
      if (ret != 0)
      {
        return ret;
      }

      ret = AppDDS_SetFreq(cmd->u32);
      if (ret != 0)
      {
        return ret;
      }

      return AppDDS_Apply();
    }
    //返回值 -4 表示命令类型不支持或未识别
    case APP_DDS_CMD_SET_CH_PHASE_APPLY:
    {
      int ret;

      /* 先切换到命令指定的逻辑通道。 */
      ret = AppDDS_SelectChannel(cmd->ch);
      if (ret != 0)
      {
        return ret;
      }

      /* 再写入目标相位角。 */
      ret = AppDDS_SetPhaseDeg(cmd->u16);
      if (ret != 0)
      {
        return ret;
      }

      /* 最后统一 Apply，保证相位修改真正下发到 AD9959。 */
      return AppDDS_Apply();
    }

    default:
      s_dds.last_err = -4;
      return -4;
  }
}

/* 绑定或解绑 DDS 控制层使用的 RTOS 消息队列。
 *
 * 工作流程：
 * 1. 记录队列句柄
 * 2. 后续 AppDDS_DispatchCmd() 根据该句柄决定走“直接执行”还是“投递到队列”
 *
 * 设计目的：
 * - 让上层始终只认 AppDDS_DispatchCmd()
 * - 非 RTOS 演示阶段可以不绑定队列，保持直接执行
 * - RTOS 阶段绑定队列后，不需要改上层调用风格
 */
int AppDDS_BindRtosQueue(osMessageQueueId_t queue_handle)
{
  s_dds_rtos_queue = queue_handle;
  return 0;
}

/* 投递一条 DDS 控制命令。
 *
 * 当前阶段说明：
 * - 如果已经绑定 RTOS 队列，则优先投递到真正的 CMSIS-RTOS 消息队列
 * - 如果尚未绑定 RTOS 队列，则退回为直接执行模式
 *
 * 设计价值：
 * - 先让上层形成“发命令”的习惯
 * - 以后即使底层从软件队列切到 RTOS 队列，上层接口也不需要改
 */
int AppDDS_DispatchCmd(const AppDdsCmd *cmd)
{
  osStatus_t os_ret;
  // 先检查 cmd 参数是否合法
  if (cmd == 0)
  {
    s_dds.last_err = -3;
    return -3;
  }
  // 如果已经绑定了 RTOS 队列，就投递到真正的 CMSIS-RTOS 消息队列
  if (s_dds_rtos_queue != 0)
  {
    os_ret = osMessageQueuePut(s_dds_rtos_queue, cmd, 0U, 0U);
    if (os_ret != osOK)
    {
      s_dds.last_err = -6;
      return -6;  // 投递失败，返回错误码 -6
    }

    s_dds.last_err = 0; // 投递成功，返回 0
    return 0;
  }

  return AppDDS_ExecuteCmd(cmd);
}

/* 把一条命令放入软件命令队列。 */
int AppDDS_PostCmd(const AppDdsCmd *cmd)
{
  int ret;
  // 先检查 cmd 参数是否合法
  if (cmd == 0)
  {
    s_dds.last_err = -3;
    return -3;
  }
  // 目前版本直接执行命令，不真正入队
  ret = AppDDS_QueuePush(cmd);
  if (ret != 0)
  {
    s_dds.last_err = ret;
    return ret;
  }
  // 现在直接执行命令，不真正入队
  s_dds.last_err = 0;
  return 0;
}

/* 依次处理当前软件命令队列中的全部命令。
 *
 * 当前版本相当于“手动驱动一次 DdsTask 消费队列”。
 * 后续接入 RTOS 后，本函数的角色会自然转移给真正的 DdsTask 主循环。
 */
int AppDDS_ProcessPending(void)
{
  AppDdsCmd cmd;
  int ret;

  while (s_dds_cmd_queue.count != 0U)
  {
    ret = AppDDS_QueuePop(&cmd);
    if (ret != 0)
    {
      s_dds.last_err = ret;
      return ret;
    }

    ret = AppDDS_ExecuteCmd(&cmd);
    if (ret != 0)
    {
      s_dds.last_err = ret;
      return ret;
    }
  }

  s_dds.last_err = 0;
  return 0;
}

/* 读取当前待处理命令数量。 */
uint8_t AppDDS_GetPendingCmdCount(void)
{
  return s_dds_cmd_queue.count;
}
