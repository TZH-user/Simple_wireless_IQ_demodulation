# 120MHz 联调数据集测试方案

## 目标

采集 `120MHz` 下 `CW / AM / FM / ASK / FSK / BPSK` 六类数据，用于验证当前扫频锁定、调制识别和解调输出链路。测试场地存在随机电台，因此只把锁定到 `120MHz` 附近的数据纳入有效数据集。

## 安全边界

- 仪器距离装置约 `10m`，默认从 `-20 dBm` 开始。
- 自动升级功率最多到 `-10 dBm`。
- `+7 dBm` 不纳入自动测试；只有低功率完全无法锁定、现场确认 ADC 未打满且链路安全时，才人工单独测试。
- 每轮测试结束必须关闭 RF。
- 每轮采集前通过 ST-LINK RST 硬复位单片机，避免上一轮状态残留。

## 测试矩阵

测试矩阵文件：`agent_workspace/120MHz_dataset_test_matrix_20260525.csv`

关键参数：

- 载波频率：`120000000 Hz`
- AM：`10kHz` 内调制，`50%` 深度
- FM：`10kHz` 内调制，`75kHz` 频偏
- ASK：`10ksym/s`，`loop01`，`RECT`，`100%` 深度
- FSK：`10ksym/s`，`loop01`，`RECT`，`20kHz` 频偏
- BPSK：`10ksym/s`，`loop01`，`RECT`，差分编码关闭

## 单轮步骤

1. 启动 E8267D 控制台并读取状态。
2. 设置当前测试类型，RF 保持关闭。
3. 启动串口采集，波特率 `1500000`。
4. 打开 RF。
5. 通过 ST-LINK RST 硬复位单片机。
6. 等待串口输出扫频完成和分析结果。
7. 关闭 RF。
8. 保存原始串口日志。
9. 用有效性规则判断是否纳入数据集。

## 有效性规则

有效数据必须满足：

- 日志包含 `moddetect: sweep done center=...Hz`。
- 或包含 `analyze:r0/r1/r2/r3` 结果行。
- `center_hz` 距离 `120000000Hz` 不超过 `300000Hz`。

无效数据：

- `center=0Hz`。
- `sweep unlocked`。
- 锁定到明显随机电台频点。
- 没有分析结果，且无法确认扫频中心。

## 命名规则

原始日志建议命名：

```text
120MHz_<MOD>_<POWER>dBm_run<N>_<valid|reject>.txt
```

示例：

```text
120MHz_FM_-20dBm_run1_valid.txt
120MHz_FSK_-15dBm_run2_reject.txt
```

## 输出物

- 原始串口日志。
- 汇总 CSV：每轮记录调制类型、功率、锁定中心、识别结果、是否有效。
- 如果日志包含 `spec:` 频谱点，可用现有脚本导出 CSV/XLSX：
  - `analysis_reports/convert_spectrum_log_to_csv.py`
  - `analysis_reports/convert_spectrum_log_to_xlsx.py`

## 现场异常处理

- 若连续两轮锁不到 `120MHz`，先升一级功率。
- 若出现 ADC clip、扫频曲线平台顶到上限、识别结果异常增多，不再升功率，先降回上一档。
- 若 E8267D 控制状态异常，先 RF OFF，再执行 safe restore。
