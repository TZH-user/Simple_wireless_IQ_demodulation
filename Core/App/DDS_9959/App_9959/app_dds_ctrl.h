/*
 * DDS 控制层头文件
 *
 * 文件定位：
 * - 这一层位于 app_ad9959 和更高层调用者之间
 * - 它负责“运行时控制”和“状态缓存”
 * - 它不是底层驱动层，也不是 RTOS 队列层
 *
 * 当前版本目标：
 * 1. 先完成单线程/裸机可用的 DDS 控制入口
 * 2. 先把当前状态缓存起来，再统一 Apply
 * 3. 为后面接 RTOS 队列做好接口准备
 */
#ifndef APP_DDS_CTRL_H
#define APP_DDS_CTRL_H

#include <stdint.h>
#include "cmsis_os.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_DDS_CHANNEL_COUNT 4U
#define APP_DDS_CMD_QUEUE_LEN 16U

/* DDS 控制层状态结构体。
 *
 * 这是“当前系统认为 DDS 处于什么状态”的缓存，
 * 不是一次性的配置请求。
 *
 * 字段说明：
 * - freq_hz[]：4 路当前缓存频率
 * - amp_code[]：4 路当前缓存幅度码
 * - phase_deg[]：4 路当前缓存相位角
 * - selected_ch：当前逻辑选中的通道
 * - hw_ready：底层硬件是否已经初始化
 * - dirty_mask：哪些通道被修改过但还没 Apply
 * - last_err：最近一次操作结果
 */
typedef struct
{
  uint32_t freq_hz[APP_DDS_CHANNEL_COUNT];
  uint16_t amp_code[APP_DDS_CHANNEL_COUNT];
  uint16_t phase_deg[APP_DDS_CHANNEL_COUNT];
  uint8_t selected_ch;
  uint8_t hw_ready;
  uint8_t dirty_mask;
  int32_t global_freq_offset_hz;
  int32_t last_err;
} AppDdsStatus;

/* DDS 控制命令类型。
 *
 * 这组枚举值不是给 AD9959 芯片直接使用的，
 * 而是给更高层任务、串口命令、UI 控件、未来 RTOS 队列使用的。
 *
 * 设计目的：
 * - 把“我要做什么”抽象成命令
 * - 让多个任务只发命令，不直接碰 DDS 控制层内部状态
 * - 为后续 DdsTask + Queue 结构做准备
 */
typedef enum
{
  APP_DDS_CMD_INIT = 0,
  APP_DDS_CMD_SELECT_CH,
  APP_DDS_CMD_SET_FREQ,
  APP_DDS_CMD_SET_AMP,
  APP_DDS_CMD_SET_PHASE_DEG,
  APP_DDS_CMD_APPLY,
  APP_DDS_CMD_SET_CH_FREQ_APPLY,
  APP_DDS_CMD_SET_CH_PHASE_APPLY
} AppDdsCmdType;

/* DDS 控制命令结构体。
 *
 * 这是“一次命令请求”的载体，不是长期状态缓存。
 * 以后如果接入 RTOS 队列，队列里流动的通常就是这种结构体。
 *
 * 字段说明：
 * - type：
 *   命令类型，决定本条命令到底是初始化、选通道、设频率、设幅度、设相位还是提交
 * - u32：
 *   32 位命令参数，当前用于承载频率等较大范围数值
 * - u16：
 *   16 位命令参数，当前用于承载幅度码、相位角等较小范围数值
 * - ch：
 *   通道号参数，当前用于 APP_DDS_CMD_SELECT_CH
 *
 * 说明：
 * - 第一版故意保持简单，不急着做 union
 * - 好处是串口命令、按键事件、算法层调用都容易构造
 */
typedef struct
{
  AppDdsCmdType type;
  uint32_t u32;
  uint16_t u16;
  uint8_t ch;
} AppDdsCmd;

/**
 * @brief 构造一条“初始化 DDS”命令。
 * @param None
 * @retval 返回填充完成的命令结构体
 * @note 当前只有 type 有意义，其余字段会被清零。
 */
AppDdsCmd AppDDS_MakeInitCmd(void);

/**
 * @brief 构造一条“选择通道”命令。
 * @param ch 目标通道号，当前有效范围 0~3
 * @retval 返回填充完成的命令结构体
 */
AppDdsCmd AppDDS_MakeSelectChCmd(uint8_t ch);

/**
 * @brief 构造一条“设置频率”命令。
 * @param freq_hz 目标频率，单位 Hz
 * @retval 返回填充完成的命令结构体
 */
AppDdsCmd AppDDS_MakeSetFreqCmd(uint32_t freq_hz);
AppDdsCmd AppDDS_MakeSetChFreqApplyCmd(uint8_t ch, uint32_t freq_hz);

/**
 * @brief 构造一条“设置幅度码”命令。
 * @param amp_code 目标幅度码，当前按 0~1023 使用
 * @retval 返回填充完成的命令结构体
 */
AppDdsCmd AppDDS_MakeSetAmpCmd(uint16_t amp_code);

/**
 * @brief 构造一条“设置相位角”命令。
 * @param phase_deg 目标相位角，单位度
 * @retval 返回填充完成的命令结构体
 */
AppDdsCmd AppDDS_MakeSetPhaseDegCmd(uint16_t phase_deg);

/**
 * @brief 构造一条“指定通道设置相位并立即 Apply”命令。
 * @param ch 目标逻辑通道号，当前有效范围为 0~3
 * @param phase_deg 目标相位角，单位为度
 * @retval 返回填充完成的命令结构体
 */
AppDdsCmd AppDDS_MakeSetChPhaseApplyCmd(uint8_t ch, uint16_t phase_deg);

/**
 * @brief 构造一条“统一提交缓存参数”命令。
 * @param None
 * @retval 返回填充完成的命令结构体
 * @note 当前只有 type 有意义，其余字段会被清零。
 */
AppDdsCmd AppDDS_MakeApplyCmd(void);

/**
 * @brief 初始化 DDS 控制层。
 * @param None
 * @retval 0 初始化成功；负值表示初始化失败
 * @note 当前版本会建立默认状态缓存，并调用 APP_AD9959_InitDefault4Ch() 完成底层默认初始化。
 */
int AppDDS_Init(void);

/**
 * @brief 选择当前要操作的逻辑通道。
 * @param ch 目标通道号，当前有效范围为 0~3
 * @retval 0 选择成功；-1 表示通道号越界
 */
int AppDDS_SelectChannel(uint8_t ch);

/**
 * @brief 修改当前选中通道的缓存频率。
 * @param freq_hz 目标频率，单位 Hz
 * @retval 0 修改成功
 * @note 当前版本只修改状态缓存，并置脏标记，不立即下发硬件。
 */
int AppDDS_SetFreq(uint32_t freq_hz);

/**
 * @brief 修改当前选中通道的缓存幅度码。
 * @param amp_code 目标幅度码，当前按 0~1023 使用
 * @retval 0 修改成功
 * @note 当前版本只修改状态缓存，并置脏标记，不立即下发硬件。
 */
int AppDDS_SetAmp(uint16_t amp_code);

/**
 * @brief 修改当前选中通道的缓存相位角。
 * @param phase_deg 目标相位角，单位为度
 * @retval 0 修改成功
 * @note 当前版本只修改状态缓存，并置脏标记，不立即下发硬件。
 */
int AppDDS_SetPhaseDeg(uint16_t phase_deg);

void AppDDS_SetGlobalFreqOffsetHz(int32_t offset_hz);
int32_t AppDDS_GetGlobalFreqOffsetHz(void);

/**
 * @brief 把当前缓存的改动统一提交到底层 DDS。
 * @param None
 * @retval 0 提交成功；-2 表示硬件尚未初始化
 * @note 当前版本按“通道级脏标记”工作，某个通道只要任一参数修改过，就会在 Apply 时把该通道的频率/幅度/相位一起提交。
 */
int AppDDS_Apply(void);

/**
 * @brief 获取当前 DDS 状态缓存的只读指针。
 * @param None
 * @retval 指向当前 DDS 状态结构体的只读指针
 */
const AppDdsStatus *AppDDS_GetStatus(void);

/**
 * @brief 按命令结构体统一执行一条 DDS 控制命令。
 * @param cmd 指向待执行命令结构体的指针，不能为空
 * @retval 0 执行成功；负值表示命令参数非法或命令类型不支持
 * @note 这是 RTOS 接入前的过渡接口。后续接入队列后，DdsTask 收到命令后通常会调用本函数。
 */
int AppDDS_ExecuteCmd(const AppDdsCmd *cmd);

/**
 * @brief 绑定 DDS 控制层要使用的 RTOS 消息队列句柄。
 * @param queue_handle CMSIS-RTOS V2 消息队列句柄；传入 0 表示解绑并退回直接执行模式
 * @retval 0 绑定成功
 * @note 当前设计目标是让上层始终优先调用 AppDDS_DispatchCmd()，
 *       然后由本函数决定 Dispatch 是“直接执行”还是“投递到 RTOS 队列”。
 */
int AppDDS_BindRtosQueue(osMessageQueueId_t queue_handle);

/**
 * @brief 投递一条 DDS 控制命令。
 * @param cmd 指向待投递命令结构体的指针，不能为空
 * @retval 0 投递/执行成功；负值表示命令非法或执行失败
 * @note 如果已经通过 AppDDS_BindRtosQueue() 绑定了 RTOS 队列，
 *       本函数会把命令投递进队列；否则退回直接执行模式。
 */
int AppDDS_DispatchCmd(const AppDdsCmd *cmd);

/**
 * @brief 把一条 DDS 命令放入软件命令队列。
 * @param cmd 指向待入队命令结构体的指针，不能为空
 * @retval 0 入队成功；负值表示命令非法或队列已满
 * @note 这是 FreeRTOS 队列引入前的教学版软件队列接口。入队时会复制命令内容，因此调用者后续可以安全复用原 cmd 变量。
 */
int AppDDS_PostCmd(const AppDdsCmd *cmd);

/**
 * @brief 依次处理当前软件命令队列中的全部命令。
 * @param None
 * @retval 0 全部处理成功；负值表示处理过程中出现错误
 * @note 当前非 RTOS 版本里，可把它理解成“手动让 DdsTask 跑一轮”。
 */
int AppDDS_ProcessPending(void);

/**
 * @brief 获取当前软件命令队列中的待处理命令数量。
 * @param None
 * @retval 当前待处理命令数
 */
uint8_t AppDDS_GetPendingCmdCount(void);

#ifdef __cplusplus
}
#endif

#endif
