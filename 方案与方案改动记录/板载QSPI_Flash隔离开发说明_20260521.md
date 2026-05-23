# 板载 QSPI Flash 隔离开发说明

日期：2026-05-21

## 目标

在当前测试工程内引入 FK743M5-XIH6 核心板板载 W25Q64 QSPI Flash 的最小可验证能力，但不把 Flash 自测挂入任何主线任务。

## 当前边界

- V1 只做 `JEDEC ID`、初始化、普通读、页写、4K 擦除、64K 擦除和指定 scratch 区自测。
- 未启用 memory-mapped、LittleFS/FatFS、DMA/MDMA 或 XIP。
- 自测必须由应用层显式调用 `app_board_flash_self_test(scratch_addr, &result)`。
- `scratch_addr` 必须由用户确认为空闲区域；函数内部不会自动选择擦除地址。

## 分层

- 应用层：`app_board_flash.h/.c`
- W25Q64 指令层：`board_flash_w25q64.h/.c`
- QSPI HAL 适配层：`board_flash_qspi_port.h/.c`
- 外设初始化：`quadspi.h/.c`

业务层只应 include `app_board_flash.h`，不要直接 include 内部 W25Q64 或 QSPI port 头文件。

## 硬件参数

- Flash：W25Q64，8MB，JEDEC ID `0xEF4017`
- QSPI 引脚：
  - `PF10 -> QUADSPI_CLK`
  - `PF8 -> QUADSPI_BK1_IO0`
  - `PF9 -> QUADSPI_BK1_IO1`
  - `PF7 -> QUADSPI_BK1_IO2`
  - `PF6 -> QUADSPI_BK1_IO3`
  - `PG6 -> QUADSPI_BK1_NCS`
- 初始 QSPI 分频：`ClockPrescaler = 15`，先低速验证。

## 验证状态

- Debug 构建已通过。
- 尚未进行上板读 ID 和 scratch sector 自测。
