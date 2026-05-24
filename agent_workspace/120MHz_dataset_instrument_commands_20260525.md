# 120MHz 数据集仪器控制命令

本文件用于现场联调时逐条执行，避免临场手写长命令。所有发射从 `-20 dBm` 起步，只有锁定失败且确认链路安全时才升到 `-15 dBm`、`-10 dBm`。暂不使用 `+7 dBm`。

## 0. 变量

```powershell
$base = "http://127.0.0.1:8766"
$freq = 120000000
$power = -20
```

## 1. 前置状态读取

```powershell
Invoke-RestMethod -Uri "$base/api/status" -Method Get
Invoke-RestMethod -Uri "$base/api/digital/status" -Method Get
Invoke-RestMethod -Uri "$base/api/logs" -Method Get
```

## 2. 统一复位到已知状态

```powershell
Invoke-RestMethod -Uri "$base/api/preset/default" -Method Post
Invoke-RestMethod -Uri "$base/api/basic" -Method Post -ContentType "application/json" -Body (@{
  frequency_hz = $freq
  power_dbm = $power
  rf_enabled = $false
  mod_enabled = $true
} | ConvertTo-Json)
Invoke-RestMethod -Uri "$base/api/digital/custom" -Method Post -ContentType "application/json" -Body (@{
  enabled = $false
  modulation = "BPSK"
  symbol_rate_hz = 10000
  filter_type = "RECT"
  data_mode = "loop01"
  fixed_bits = "01"
  differential_encoding = $false
} | ConvertTo-Json)
```

## 3. 通用 RF 开关

打开 RF 前确认串口日志已经开始采集，并确认当前功率变量 `$power` 没有被误设为高功率。

```powershell
Invoke-RestMethod -Uri "$base/api/basic" -Method Post -ContentType "application/json" -Body (@{
  frequency_hz = $freq
  power_dbm = $power
  rf_enabled = $true
  mod_enabled = $true
} | ConvertTo-Json)
```

每轮结束必须关闭 RF：

```powershell
Invoke-RestMethod -Uri "$base/api/basic" -Method Post -ContentType "application/json" -Body (@{
  frequency_hz = $freq
  power_dbm = $power
  rf_enabled = $false
  mod_enabled = $true
} | ConvertTo-Json)
```

## 4. CW 配置

```powershell
Invoke-RestMethod -Uri "$base/api/basic" -Method Post -ContentType "application/json" -Body (@{
  frequency_hz = $freq
  power_dbm = $power
  rf_enabled = $false
  mod_enabled = $false
} | ConvertTo-Json)
Invoke-RestMethod -Uri "$base/api/digital/custom" -Method Post -ContentType "application/json" -Body (@{ enabled = $false; modulation = "BPSK" } | ConvertTo-Json)
```

## 5. AM 配置

```powershell
Invoke-RestMethod -Uri "$base/api/basic" -Method Post -ContentType "application/json" -Body (@{
  frequency_hz = $freq
  power_dbm = $power
  rf_enabled = $false
  mod_enabled = $true
} | ConvertTo-Json)
Invoke-RestMethod -Uri "$base/api/digital/custom" -Method Post -ContentType "application/json" -Body (@{ enabled = $false; modulation = "BPSK" } | ConvertTo-Json)
Invoke-RestMethod -Uri "$base/api/modulation/am" -Method Post -ContentType "application/json" -Body (@{
  enabled = $true
  source = "INT"
  depth_percent = 50
  internal_frequency_hz = 10000
  internal_function = "SIN"
} | ConvertTo-Json)
```

## 6. FM 配置

```powershell
Invoke-RestMethod -Uri "$base/api/basic" -Method Post -ContentType "application/json" -Body (@{
  frequency_hz = $freq
  power_dbm = $power
  rf_enabled = $false
  mod_enabled = $true
} | ConvertTo-Json)
Invoke-RestMethod -Uri "$base/api/digital/custom" -Method Post -ContentType "application/json" -Body (@{ enabled = $false; modulation = "BPSK" } | ConvertTo-Json)
Invoke-RestMethod -Uri "$base/api/modulation/fm" -Method Post -ContentType "application/json" -Body (@{
  enabled = $true
  source = "INT"
  deviation_hz = 75000
  internal_frequency_hz = 10000
  internal_function = "SIN"
} | ConvertTo-Json)
```

## 7. ASK 配置

```powershell
Invoke-RestMethod -Uri "$base/api/basic" -Method Post -ContentType "application/json" -Body (@{
  frequency_hz = $freq
  power_dbm = $power
  rf_enabled = $false
  mod_enabled = $true
} | ConvertTo-Json)
Invoke-RestMethod -Uri "$base/api/digital/custom" -Method Post -ContentType "application/json" -Body (@{
  enabled = $true
  modulation = "ASK"
  symbol_rate_hz = 10000
  filter_type = "RECT"
  data_mode = "loop01"
  fixed_bits = "01"
  differential_encoding = $false
  ask_depth_percent = 100
} | ConvertTo-Json)
```

## 8. FSK 配置

```powershell
Invoke-RestMethod -Uri "$base/api/basic" -Method Post -ContentType "application/json" -Body (@{
  frequency_hz = $freq
  power_dbm = $power
  rf_enabled = $false
  mod_enabled = $true
} | ConvertTo-Json)
Invoke-RestMethod -Uri "$base/api/digital/custom" -Method Post -ContentType "application/json" -Body (@{
  enabled = $true
  modulation = "FSK2"
  symbol_rate_hz = 10000
  filter_type = "RECT"
  data_mode = "loop01"
  fixed_bits = "01"
  differential_encoding = $false
  fsk_deviation_hz = 20000
} | ConvertTo-Json)
```

## 9. BPSK 配置

```powershell
Invoke-RestMethod -Uri "$base/api/basic" -Method Post -ContentType "application/json" -Body (@{
  frequency_hz = $freq
  power_dbm = $power
  rf_enabled = $false
  mod_enabled = $true
} | ConvertTo-Json)
Invoke-RestMethod -Uri "$base/api/digital/custom" -Method Post -ContentType "application/json" -Body (@{
  enabled = $true
  modulation = "BPSK"
  symbol_rate_hz = 10000
  filter_type = "RECT"
  data_mode = "loop01"
  fixed_bits = "01"
  differential_encoding = $false
} | ConvertTo-Json)
```

## 10. ST-LINK 硬复位

```powershell
& 'C:\Users\18152\AppData\Local\stm32cube\bundles\programmer\2.22.0+st.1\bin\STM32_Programmer_CLI.exe' -c port=SWD mode=UR reset=HWrst -rst
```

## 11. 安全恢复

如发现状态异常，立即执行：

```powershell
Invoke-RestMethod -Uri "$base/api/basic" -Method Post -ContentType "application/json" -Body (@{
  frequency_hz = $freq
  power_dbm = $power
  rf_enabled = $false
  mod_enabled = $true
} | ConvertTo-Json)
Invoke-RestMethod -Uri "$base/api/safe-restore" -Method Post
```
