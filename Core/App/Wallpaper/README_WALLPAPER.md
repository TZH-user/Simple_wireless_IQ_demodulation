# UI Wallpaper 外部 QSPI 资源

## 本次生成的资源

- 显示尺寸：`800 × 480`，格式：`RGB565 little-endian`。
- 原始像素数据：`Assets/wallpaper_800x480.rgb565`，大小 `768,000` 字节（`0xBB800`）。
- 可直接烧写镜像：`Assets/wallpaper_qspi_0x000000.bin`，起始偏移 `0x000000`，大小 `772,096` 字节。
- 预览图：`Assets/wallpaper_preview_800x480.png`。
- FNV-1a 校验：`0xBA9A1BB0`。

## QSPI W25Q64 布局

| 区域 | 地址范围 | 用途 |
|---|---:|---|
| Header 扇区 | `0x000000 - 0x000FFF` | 魔数、尺寸、格式、长度、校验 |
| RGB565 数据 | `0x001000 - 0x0BC7FF` | 800×480 背景像素 |
| 背景保留结束 | `0x0BD000` | 后续数据从此地址之后分配 |
| OCXO/UI 设置 | `0x7FE000 - 0x7FEFFF` | 现有设置记录（本次升为 V9） |
| 扫频校准 | `0x7FF000 - 0x7FFFFF` | 现有 sweep 校准 |

低地址背景区与现有两个末尾设置扇区不重叠。背景默认关闭；外部 Flash 中没有有效资源或校验失败时，UI 自动保留纯色主题且显示 `BG N/A`。

## 烧写说明

将 `wallpaper_qspi_0x000000.bin` 作为 **外部 QSPI Flash** 镜像烧写至偏移 `0x000000`。不要将 `.rgb565` 数组链接进 MCU 内部 Flash。固件仅在 LVGL 启动时加载背景到复用的 SDRAM 启动画面缓冲区。
