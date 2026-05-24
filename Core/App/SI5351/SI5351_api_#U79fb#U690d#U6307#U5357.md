# SI5351_api 移植指南

记录日期：2026-05-09

## 1. 这个库的定位

`D:\modulecode\SI5351_api` 是从下面这个工程中抽离出来的可复用时钟库：

`D:\VSCodeCMAKEProject\Simple wireless automatic transceiver device\Simple_wireless_automatic_transceiver_deviceV1.0\Core\App\SI5351`

当前库的设计目标是：

- 保留旧版 `basic / pro / promax` 三套调用风格
- 新工程只面对统一包装层接口
- 可在编译期切换版本
- 可在配置头中切换硬件 I2C 或软件 I2C
- 适合直接移植到 STM32CubeMX + HAL 工程

## 2. 目录结构

```text
SI5351_api
├─ Config
│  └─ app_si5351_variant.h
├─ Legacy
│  ├─ basic
│  ├─ common
│  ├─ pro
│  └─ promax
├─ Port
│  ├─ my_IIC.h
│  ├─ si5351_port.c
│  └─ si5351_port.h
└─ Wrapper
   ├─ app_si5351_drv.c
   └─ app_si5351_drv.h
```

各目录职责如下：

- `Config`
  - 统一放版本选择、I2C 端口绑定、参考时钟参数
- `Legacy/basic`
  - 基础款旧接口实现
- `Legacy/pro`
  - `pro` 版旧接口实现
- `Legacy/promax`
  - `promax` 版旧接口实现
- `Legacy/common`
  - 三个变体共用的公共内核
- `Port`
  - 负责适配当前工程的底层读写接口
  - 提供兼容旧代码的 `I2C2_Write_REG()` / `I2C2_Read_REG()`
- `Wrapper`
  - 提供统一上层接口
  - 屏蔽不同变体的调用差异

## 3. 当前默认设计约定

### 3.1 版本

默认编译版本是：

```c
APP_SI5351_SELECTED_VARIANT = APP_SI5351_VARIANT_PROMAX
```

### 3.2 端口

默认端口模式是：

```c
APP_SI5351_PORT_MODE = APP_SI5351_PORT_HAL_I2C
```

默认硬件 I2C 句柄是：

```c
APP_SI5351_HAL_I2C_HANDLE = hi2c1
```

### 3.3 参考时钟

当前库默认假设：

- `XTAL = 25MHz`
- `CLKIN = 10MHz`
- `PLL = 800MHz`
- `APP_SI5351_USE_CLKIN = 1`

也就是当前默认工作模型是：

```text
外部 10MHz -> SI5351 CLKIN -> PLLA/PLLB -> 各路时钟输出
```

这点必须注意：

- 这份库当前默认依赖外部参考时钟
- 如果目标板没有接 `CLKIN`
- 需要改 `Config\app_si5351_variant.h`
- 否则运行状态检查可能报 `CLKIN_LOST`

## 4. 移植到新工程时最少要做什么

### 第一步：复制目录

将整个：

`D:\modulecode\SI5351_api`

复制到目标工程的用户代码目录，例如：

```text
Core\App\SI5351
```

### 第二步：把源文件加入构建系统

至少加入这些 `.c`：

- `Legacy\common\si5351_legacy_common.c`
- `Legacy\basic\SI5351.c`
- `Legacy\pro\SI5351PRO.c`
- `Legacy\promax\SI5351PROMAX.c`
- `Port\si5351_port.c`
- `Wrapper\app_si5351_drv.c`

如果你只保留某一个变体，也可以只加入对应变体源文件，但统一推荐先全部加入，再用宏切换。

### 第三步：加入头文件搜索路径

至少加入：

- `...\SI5351_api\Config`
- `...\SI5351_api\Legacy\basic`
- `...\SI5351_api\Legacy\common`
- `...\SI5351_api\Legacy\pro`
- `...\SI5351_api\Legacy\promax`
- `...\SI5351_api\Port`
- `...\SI5351_api\Wrapper`

### 第四步：确认 HAL 依赖

当前配置头包含：

```c
#include "main.h"
#include "i2c.h"
```

因此目标工程至少要满足：

- 有 `main.h`
- 有 `i2c.h`
- 使用 HAL 库

如果目标工程不用 HAL I2C，只想用软件 I2C，也建议保留 `i2c.h` 头，避免额外改库结构。

## 5. 如何切换版本

统一改：

`D:\modulecode\SI5351_api\Config\app_si5351_variant.h`

可选值：

```c
APP_SI5351_VARIANT_BASIC
APP_SI5351_VARIANT_PRO
APP_SI5351_VARIANT_PROMAX
```

能力差异如下：

- `basic`
  - 支持 `Init + SetFrequency`
  - 不支持强制指定 `PLLA/PLLB`
  - 不支持显式设置驱动强度
- `pro`
  - 支持 `Init + SetFrequency + 指定 PLL`
  - 不支持显式设置驱动强度
- `promax`
  - 支持 `Init + SetFrequency + 指定 PLL + 驱动强度`

经验建议：

- 新工程如果需要快速稳定移植，优先用 `promax`
- 如果只是兼容旧工程行为，再退回 `pro` 或 `basic`
- 经验仅供参考，不视为绝对结论

## 6. 如何切换硬件 I2C 或软件 I2C

统一改：

`D:\modulecode\SI5351_api\Config\app_si5351_variant.h`

### 6.1 使用硬件 I2C

```c
#define APP_SI5351_PORT_MODE APP_SI5351_PORT_HAL_I2C
#define APP_SI5351_HAL_I2C_HANDLE hi2c1
```

如果你要改成 `I2C2`，只需要把句柄改成：

```c
#define APP_SI5351_HAL_I2C_HANDLE hi2c2
```

前提是目标工程已经在 `CubeMX` 中生成了对应 `hi2c2`。

### 6.2 使用软件 I2C

```c
#define APP_SI5351_PORT_MODE APP_SI5351_PORT_SOFT_I2C
```

然后绑定引脚：

```c
#define APP_SI5351_SOFT_I2C_SCL_PORT ...
#define APP_SI5351_SOFT_I2C_SCL_PIN  ...
#define APP_SI5351_SOFT_I2C_SDA_PORT ...
#define APP_SI5351_SOFT_I2C_SDA_PIN  ...
```

可再按板级速度调整：

```c
#define APP_SI5351_SOFT_I2C_DELAY_CYCLES 40U
```

移植时建议：

- 软件 I2C 的引脚优先放在 `USER CODE` 可控范围内
- 如果复用触摸或其他总线引脚，要自己评估冲突

## 7. 统一包装层接口

头文件：

`D:\modulecode\SI5351_api\Wrapper\app_si5351_drv.h`

常用接口如下：

```c
app_si5351_result_t app_si5351_init_device(void);
app_si5351_result_t app_si5351_apply_output_plan(const app_si5351_output_cfg_t *cfgs, uint8_t count);
app_si5351_result_t app_si5351_enable_outputs(bool enable);
app_si5351_result_t app_si5351_check_ref_status(void);
bool app_si5351_is_clock_ready(void);
```

输出计划结构体：

```c
typedef struct {
    uint8_t channel;
    uint32_t freq_hz;
    app_si5351_pll_t pll;
    app_si5351_drive_t drive;
    bool enable;
} app_si5351_output_cfg_t;
```

推荐调用顺序：

1. `app_si5351_init_device()`
2. `app_si5351_apply_output_plan()`
3. `app_si5351_check_ref_status()`
4. `app_si5351_enable_outputs(true)`

## 8. 最小示例

```c
#include "app_si5351_drv.h"

static const app_si5351_output_cfg_t g_clk_plan[] = {
    {0U, 25000000UL, APP_SI5351_PLL_AUTO, APP_SI5351_DRIVE_DEFAULT, true},
    {1U, 32768000UL, APP_SI5351_PLL_PLLB, APP_SI5351_DRIVE_4MA, true},
};

void board_si5351_startup(void)
{
    if (app_si5351_init_device() != APP_SI5351_RESULT_OK)
    {
        return;
    }

    if (app_si5351_apply_output_plan(g_clk_plan, 2U) != APP_SI5351_RESULT_OK)
    {
        return;
    }

    if (app_si5351_check_ref_status() != APP_SI5351_RESULT_OK)
    {
        return;
    }

    (void)app_si5351_enable_outputs(true);
}
```

## 9. 当前已验证来源

这份库当前直接来源于工程：

`D:\VSCodeCMAKEProject\Simple wireless automatic transceiver device\Simple_wireless_automatic_transceiver_deviceV1.0`

已验证过的能力包括：

- `basic / pro / promax` 编译期切换
- `HAL I2C / 软件 I2C` 端口层切换
- 统一包装层调用
- 外部 `CLKIN` 参考状态检查
- 默认输出计划启动日志

## 10. 移植时最容易忽略的点

- 当前默认依赖外部时钟，不是纯晶振独立模式
- `basic` 版本不能强制绑 `PLLB`
- `basic/pro` 版本不能显式设置驱动强度
- 如果硬件 I2C 没有上拉，可能表现为 `device_not_found`
- 如果 `CLKIN` 没有输入，可能表现为 `clkin_lost` 或时钟不 ready

## 11. 推荐做法

- 先在目标工程里验证 I2C 总线正常
- 再验证 `CLKIN` 是否存在
- 最后再加输出计划

如果要做长期复用，建议把板级默认输出计划单独再抽到目标工程自己的配置头里，不要直接写死在任务文件里。
