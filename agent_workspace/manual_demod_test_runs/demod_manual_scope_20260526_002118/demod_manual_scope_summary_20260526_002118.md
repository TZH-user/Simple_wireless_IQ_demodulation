# 解调手动测试自动汇总

- 生成时间: 2026-05-26 00:31:43
- 串口日志目录: `D:\串口调试助手-V3.1\logs`
- 本次输出目录: `D:\VSCodeCMAKEProject\Simple_wireless_IQ_demodulation\agent_workspace\manual_demod_test_runs\demod_manual_scope_20260526_002118`
- 参数来源: 题图 AM/FM/2ASK/2FSK/2PSK 指标。
- 判读说明: 自动判读只基于串口日志关键行，最终仍需结合示波器紫色解调输出和黄色单路基带观察。

## 已接受测试

| 模式 | 日志 | 现象 | 自动提示 | 关键参数 |
| --- | --- | --- | --- | --- |
| AM | `AM_10k_depth50_try03_20260526_002758_auto_serial.txt` | 示波器已经有解调输出，但是串口没有收到确认信息 | 未看到 demod: mode set，解调任务可能没有进入目标模式。 未看到 am_dbg，无法判断 AM 削顶和包络 DC 漂移。 | mode=AM; sym=0; fsk_sep=1500; low_if=5014; rx=; perf_over= |

## 全部尝试

| 模式 | 尝试 | 状态 | 日志 | 现象 | 自动提示 |
| --- | --- | --- | --- | --- | --- |
| AM | 2 | rejected_retest | `AM_10k_depth50_try02_20260526_002406_auto_serial.txt` |  | 识别模式为 CW，期望 AM。 未看到 am_dbg，无法判断 AM 削顶和包络 DC 漂移。 |
| AM | 3 | accepted | `AM_10k_depth50_try03_20260526_002758_auto_serial.txt` | 示波器已经有解调输出，但是串口没有收到确认信息 | 未看到 demod: mode set，解调任务可能没有进入目标模式。 未看到 am_dbg，无法判断 AM 削顶和包络 DC 漂移。 |
| FM | 1 | skipped_after_capture | `FM_10k_dev75k_try01_20260526_003029_auto_serial.txt` |  | 未看到 demod: start，本日志可能只是识别阶段，不能判断解调输出。 未看到 demod: mode set，解调任务可能没有进入目标模式。 |
| ASK | 1 | skipped_after_capture | `ASK_10kbps_amp0_try01_20260526_003129_auto_serial.txt` |  | 未看到 demod: start，本日志可能只是识别阶段，不能判断解调输出。 未看到 demod: mode set，解调任务可能没有进入目标模式。 未看到 ask_dbg，无法判断 ASK 双簇判决。 |
| FSK | 1 | skipped_after_capture | `FSK_10kbps_sep20k_try01_20260526_003135_auto_serial.txt` |  | 未看到 demod: start，本日志可能只是识别阶段，不能判断解调输出。 未看到 demod: mode set，解调任务可能没有进入目标模式。 未看到 fsk_dbg，无法判断 FSK 判决窗口和门限。 |
| PSK | 1 | skipped_after_capture | `PSK_10kbps_phase180_try01_20260526_003140_auto_serial.txt` |  | 未看到 demod: start，本日志可能只是识别阶段，不能判断解调输出。 未看到 demod: mode set，解调任务可能没有进入目标模式。 未看到 psk_dbg，无法判断 PSK 载波/轴估计和判决。 |

## 题图参数清单

### AM 波
- 调制信号频率: 10 kHz
- 调幅深度: 50%
- 载频: 按现场当前载频设置；若按题图示例载频 20MHz，则 2FSK 第二频点为 20.020MHz；若现场用 120MHz，则第二频点为 120.020MHz。
- 串口重点: analyze:r0 mode=AM, demod: mode set rx=1, am_dbg, demod_perf, demod_dac
- 示波器重点: 紫色解调输出是否为干净 10kHz 正弦；观察是否有上下漂移、重影、削顶。

### FM 波
- 调制信号频率: 10 kHz
- 频偏: 75 kHz
- 载频: 按现场当前载频设置；若按题图示例载频 20MHz，则 2FSK 第二频点为 20.020MHz；若现场用 120MHz，则第二频点为 120.020MHz。
- 串口重点: analyze:r0 mode=FM, demod: mode set rx=3, demod_perf, demod_dac
- 示波器重点: 紫色解调输出是否为 10kHz 正弦；记录轻微失真、噪声和幅度。

### 二进制幅度键控 2ASK
- 幅度设置: 0/1，低电平幅度设置为 0
- 码速率: 10 kbps
- 载频: 按现场当前载频设置；若按题图示例载频 20MHz，则 2FSK 第二频点为 20.020MHz；若现场用 120MHz，则第二频点为 120.020MHz。
- 建议码型: 先 1010，再 PN/随机；若只能一种，使用现场默认码型并记录。
- 串口重点: analyze:r0 mode=ASK, analyze:r3 sym=10000, demod: mode set rx=2, ask_dbg
- 示波器重点: 紫色输出是否稳定翻转；允许整体反向，重点看漏翻转、误跳变、无输出。

### 二进制移频键控 2FSK
- 码速率: 10 kbps
- 跳频频差: 与载波频率相差 20 kHz
- 频点示例: 20.000MHz / 20.020MHz；若现场载频 120MHz，则 120.000MHz / 120.020MHz
- 建议码型: 先固定低频、固定高频、1010，再 PN/随机；若只能一种，使用现场默认码型并记录。
- 串口重点: analyze:r0 mode=FSK, analyze:r3 fsk_sep=20000 sym=10000, demod: mode set rx=4, fsk_dbg, fsk_rng
- 示波器重点: 紫色输出是否有高低电平；允许整体反向，重点看是否基本无输出或只停在单一电平。

### 二进制移相键控 2PSK
- 码速率 Rc: 10 kbps
- 相位设置: 180 度
- 载频: 按现场当前载频设置；若按题图示例载频 20MHz，则 2FSK 第二频点为 20.020MHz；若现场用 120MHz，则第二频点为 120.020MHz。
- 建议码型: 先 1010，再 PN/随机；无同步码头时允许整体反向。
- 串口重点: analyze:r0 mode=PSK, analyze:r3 sym=10000, demod: mode set rx=5, psk_dbg
- 示波器重点: 紫色输出是否稳定翻转；记录误码、毛刺、长时间跑偏和锁定是否丢失。

