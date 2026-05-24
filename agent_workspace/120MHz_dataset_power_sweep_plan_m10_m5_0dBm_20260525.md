# 120MHz -10/-5/0dBm 数据集测试矩阵

## 执行边界

- 本文件只定义下一轮测试矩阵；未获得放行前不启动仪器测试。
- 每个用例开始前必须执行 `RF OFF + AM OFF + FM OFF + Custom Digital OFF`。
- 每个用例结束后必须再次执行 `RF OFF + AM OFF + FM OFF + Custom Digital OFF`。
- 若发现 ADC 近轨、扫频平台明显打满、识别结果异常大面积增加，应停止继续升功率。

## 矩阵文件

- `D:\VSCodeCMAKEProject\Simple_wireless_IQ_demodulation\agent_workspace\120MHz_dataset_test_matrix_m10_m5_0dBm_20260525.csv`

## 测试参数

- 载波频率：`120000000 Hz`
- 功率档位：`-10 dBm`、`-5 dBm`、`0 dBm`
- 每类每档重复：`3` 次
- 有效锁定范围：`120 MHz ± 300 kHz`
- FM：`10 kHz` 内调制，频偏 `75 kHz`
- AM：`10 kHz` 内调制，深度 `50%`
- ASK：`10 ksym/s`，`loop01`，`RECT`，深度 `100%`
- FSK2：`10 ksym/s`，`loop01`，`RECT`，频偏 `20 kHz`
- BPSK：`10 ksym/s`，`loop01`，`RECT`，差分编码关闭

## 放行后的建议执行命令

先跑每类 `-10 dBm`，确认没有 ADC 打满后再升 `-5 dBm`，最后 `0 dBm`。

```powershell
& 'D:\VSCodeCMAKEProject\Simple_wireless_IQ_demodulation\agent_workspace\run_120MHz_dataset_capture_20260525.ps1' `
  -Tests CW,AM,FM,ASK,FSK2,BPSK `
  -PowerStepsDbm -10,-5,0 `
  -ValidRepeats 3 `
  -CaptureTimeoutSec 160 `
  -OutputTag '120M_m10_m5_0dBm_20260525'
```

## 验收要点

- 汇总 CSV 中 `valid=True` 的记录必须锁在 `120000000 ± 300000 Hz`。
- 每条有效日志应包含 `moddetect: sweep done center=...Hz`。
- 调制类日志应包含 `analyze:` 或 `demod:` 结果行。
- 测试结束后仪器必须为 `RF OFF / MOD OFF`。
