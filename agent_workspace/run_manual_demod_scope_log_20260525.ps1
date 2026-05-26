param(
    [string]$SerialLogDir = "D:\串口调试助手-V3.1\logs",
    [string]$OutputRoot = "",
    [string]$CarrierHint = "按现场当前载频设置；若按题图示例载频 20MHz，则 2FSK 第二频点为 20.020MHz；若现场用 120MHz，则第二频点为 120.020MHz。",
    [ValidateSet("AutoSerial", "ExternalLog")]
    [string]$CaptureMode = "AutoSerial",
    [string]$SerialPortName = "COM14",
    [int]$BaudRate = 1500000,
    [int]$AutoCaptureMaxSec = 120,
    [int]$RequiredModeDebugLines = 2,
    [string]$StartCommand = "",
    [int]$FreshLogSlackSec = 3,
    [switch]$EchoSerialToConsole,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $ScriptDir "manual_demod_test_runs"
}

$RunTag = Get-Date -Format "yyyyMMdd_HHmmss"
$RunDir = Join-Path $OutputRoot ("demod_manual_scope_{0}" -f $RunTag)
$SummaryCsv = Join-Path $RunDir ("demod_manual_scope_summary_{0}.csv" -f $RunTag)
$AttemptsCsv = Join-Path $RunDir ("demod_manual_scope_attempts_{0}.csv" -f $RunTag)
$SummaryMd = Join-Path $RunDir ("demod_manual_scope_summary_{0}.md" -f $RunTag)

$TestCases = @(
    [pscustomobject]@{
        Id = "AM_10k_depth50"
        Kind = "AM"
        Title = "AM 波"
        ExpectedAnalyzeMode = "AM"
        Params = @(
            "调制信号频率: 10 kHz",
            "调幅深度: 50%",
            "载频: $CarrierHint",
            "串口重点: analyze:r0 mode=AM, demod: mode set rx=1, am_dbg, demod_perf, demod_dac",
            "示波器重点: 紫色解调输出是否为干净 10kHz 正弦；观察是否有上下漂移、重影、削顶。"
        )
    }
    [pscustomobject]@{
        Id = "FM_10k_dev75k"
        Kind = "FM"
        Title = "FM 波"
        ExpectedAnalyzeMode = "FM"
        Params = @(
            "调制信号频率: 10 kHz",
            "频偏: 75 kHz",
            "载频: $CarrierHint",
            "串口重点: analyze:r0 mode=FM, demod: mode set rx=3, demod_perf, demod_dac",
            "示波器重点: 紫色解调输出是否为 10kHz 正弦；记录轻微失真、噪声和幅度。"
        )
    }
    [pscustomobject]@{
        Id = "ASK_10kbps_amp0"
        Kind = "ASK"
        Title = "二进制幅度键控 2ASK"
        ExpectedAnalyzeMode = "ASK"
        Params = @(
            "幅度设置: 0/1，低电平幅度设置为 0",
            "码速率: 10 kbps",
            "载频: $CarrierHint",
            "建议码型: 先 1010，再 PN/随机；若只能一种，使用现场默认码型并记录。",
            "串口重点: analyze:r0 mode=ASK, analyze:r3 sym=10000, demod: mode set rx=2, ask_dbg",
            "示波器重点: 紫色输出是否稳定翻转；允许整体反向，重点看漏翻转、误跳变、无输出。"
        )
    }
    [pscustomobject]@{
        Id = "FSK_10kbps_sep20k"
        Kind = "FSK"
        Title = "二进制移频键控 2FSK"
        ExpectedAnalyzeMode = "FSK"
        Params = @(
            "码速率: 10 kbps",
            "跳频频差: 与载波频率相差 20 kHz",
            "频点示例: 20.000MHz / 20.020MHz；若现场载频 120MHz，则 120.000MHz / 120.020MHz",
            "建议码型: 先固定低频、固定高频、1010，再 PN/随机；若只能一种，使用现场默认码型并记录。",
            "串口重点: analyze:r0 mode=FSK, analyze:r3 fsk_sep=20000 sym=10000, demod: mode set rx=4, fsk_dbg, fsk_rng",
            "示波器重点: 紫色输出是否有高低电平；允许整体反向，重点看是否基本无输出或只停在单一电平。"
        )
    }
    [pscustomobject]@{
        Id = "PSK_10kbps_phase180"
        Kind = "PSK"
        Title = "二进制移相键控 2PSK"
        ExpectedAnalyzeMode = "PSK"
        Params = @(
            "码速率 Rc: 10 kbps",
            "相位设置: 180 度",
            "载频: $CarrierHint",
            "建议码型: 先 1010，再 PN/随机；无同步码头时允许整体反向。",
            "串口重点: analyze:r0 mode=PSK, analyze:r3 sym=10000, demod: mode set rx=5, psk_dbg",
            "示波器重点: 紫色输出是否稳定翻转；记录误码、毛刺、长时间跑偏和锁定是否丢失。"
        )
    }
)

function New-SafeFilePart {
    param([string]$Text)

    $safe = $Text
    foreach ($ch in [System.IO.Path]::GetInvalidFileNameChars()) {
        $safe = $safe.Replace([string]$ch, "_")
    }
    return ($safe -replace "\s+", "_")
}

function Format-Bytes {
    param([long]$Bytes)

    if ($Bytes -ge 1MB) {
        return ("{0:N2} MB" -f ($Bytes / 1MB))
    }
    if ($Bytes -ge 1KB) {
        return ("{0:N1} KB" -f ($Bytes / 1KB))
    }
    return ("{0} B" -f $Bytes)
}

function Format-MdCell {
    param([object]$Value)

    if ($null -eq $Value) {
        return ""
    }
    return $Value.ToString().Replace("`r", " ").Replace("`n", " ").Replace("|", "\|")
}

function Add-Count {
    param(
        [object]$Table,
        [string]$Key,
        [int]$Delta = 1
    )

    $Table[$Key] = [int]$Table[$Key] + $Delta
}

function New-CaptureLogPath {
    param(
        [object]$TestCase,
        [int]$Attempt,
        [datetime]$StartTime,
        [datetime]$EndTime,
        [string]$Suffix = "auto"
    )

    $safeName = New-SafeFilePart $TestCase.Id
    $safeSuffix = New-SafeFilePart $Suffix
    $timeTag = "{0}_{1}" -f $StartTime.ToString("yyyyMMdd_HHmmss"), $EndTime.ToString("HHmmss")
    return (Join-Path $RunDir ("{0}_{1}_try{2:00}_{3}.txt" -f $safeName, $timeTag, $Attempt, $safeSuffix))
}

function Get-ModeDebugToken {
    param([string]$Kind)

    switch ($Kind) {
        "AM" { return "am_dbg" }
        "ASK" { return "ask_dbg" }
        "FSK" { return "fsk_dbg" }
        "PSK" { return "psk_dbg" }
        default { return "" }
    }
}

function New-CaptureEvidence {
    return [pscustomobject]@{
        DemodStartCount = 0
        DemodModeSetCount = 0
        DemodPerfCount = 0
        DemodDacCount = 0
        ModeDebugCount = 0
        LastModeDebug = ""
        LastStatusText = ""
    }
}

function Update-CaptureEvidenceFromLine {
    param(
        [object]$Evidence,
        [object]$TestCase,
        [string]$Line
    )

    if ([string]::IsNullOrWhiteSpace($Line)) {
        return
    }

    $text = $Line.Trim()
    if ($text.Contains("demod: start")) {
        $Evidence.DemodStartCount = [int]$Evidence.DemodStartCount + 1
    }
    if ($text.Contains("demod: mode set")) {
        $Evidence.DemodModeSetCount = [int]$Evidence.DemodModeSetCount + 1
    }
    if ($text.Contains("demod_perf:")) {
        $Evidence.DemodPerfCount = [int]$Evidence.DemodPerfCount + 1
    }
    if ($text.Contains("demod_dac:")) {
        $Evidence.DemodDacCount = [int]$Evidence.DemodDacCount + 1
    }

    $debugToken = Get-ModeDebugToken $TestCase.Kind
    if (-not [string]::IsNullOrWhiteSpace($debugToken) -and $text.Contains($debugToken)) {
        $Evidence.ModeDebugCount = [int]$Evidence.ModeDebugCount + 1
        $Evidence.LastModeDebug = $text
    }
}

function Test-CaptureEvidenceReady {
    param(
        [object]$Evidence,
        [object]$TestCase
    )

    if ([int]$Evidence.DemodModeSetCount -le 0) {
        return $false
    }

    if ($TestCase.Kind -eq "FM" -or $RequiredModeDebugLines -le 0) {
        return (([int]$Evidence.DemodPerfCount -gt 0) -or ([int]$Evidence.DemodDacCount -gt 0))
    }

    return (([int]$Evidence.ModeDebugCount -ge $RequiredModeDebugLines) -and
            (([int]$Evidence.DemodPerfCount -gt 0) -or ([int]$Evidence.DemodDacCount -gt 0)))
}

function Get-CaptureEvidenceStatusText {
    param(
        [object]$Evidence,
        [object]$TestCase
    )

    $debugToken = Get-ModeDebugToken $TestCase.Kind
    if ($RequiredModeDebugLines -le 0) {
        $debugPart = "debug_wait=off, perf/dac=$($Evidence.DemodPerfCount)/$($Evidence.DemodDacCount)"
    }
    elseif ([string]::IsNullOrWhiteSpace($debugToken)) {
        $debugPart = "runtime=perf/dac $($Evidence.DemodPerfCount)/$($Evidence.DemodDacCount)"
    }
    else {
        $debugPart = "{0}={1}/{2}, perf/dac={3}/{4}" -f $debugToken,
            $Evidence.ModeDebugCount,
            $RequiredModeDebugLines,
            $Evidence.DemodPerfCount,
            $Evidence.DemodDacCount
    }

    return ("start={0}, mode_set={1}, {2}" -f
        $Evidence.DemodStartCount,
        $Evidence.DemodModeSetCount,
        $debugPart)
}

function Write-CaptureEvidenceStatusIfChanged {
    param(
        [object]$Evidence,
        [object]$TestCase,
        [switch]$Force
    )

    $status = Get-CaptureEvidenceStatusText -Evidence $Evidence -TestCase $TestCase
    if ($Force -or $status -ne $Evidence.LastStatusText) {
        Write-Host ("证据状态: {0}" -f $status) -ForegroundColor DarkCyan
        $Evidence.LastStatusText = $status
    }
}

function Read-SerialCapture {
    param(
        [string]$OutPath,
        [object]$TestCase,
        [string]$PortName,
        [int]$PortBaudRate,
        [int]$MaxSec,
        [string]$CommandToSend = "",
        [switch]$EchoToConsole
    )

    $captureStart = Get-Date
    $serialPort = New-Object System.IO.Ports.SerialPort($PortName, $PortBaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $serialPort.ReadTimeout = 100
    $serialPort.WriteTimeout = 1000
    $serialPort.DtrEnable = $false
    $serialPort.RtsEnable = $false
    $serialPort.Encoding = [System.Text.Encoding]::UTF8

    $utf8NoBom = New-Object System.Text.UTF8Encoding -ArgumentList $false
    $writer = $null
    $timedOut = $false
    $forceStopped = $false
    $evidenceReady = $false
    $evidence = New-CaptureEvidence
    $pendingText = ""

    try {
        $writer = [System.IO.StreamWriter]::new($OutPath, $false, $utf8NoBom)
        $serialPort.Open()
        $serialPort.DiscardInBuffer()

        Write-Host ("串口已打开: {0}, {1} baud" -f $PortName, $PortBaudRate) -ForegroundColor Green
        if (-not [string]::IsNullOrWhiteSpace($CommandToSend)) {
            $serialPort.WriteLine($CommandToSend)
            Write-Host ("已发送启动命令: {0}" -f $CommandToSend) -ForegroundColor DarkCyan
        }
        Write-Host "现在触发板子进入任务；脚本会等待真实解调证据，证据齐全后再按 Enter 停止本项采集。" -ForegroundColor Yellow
        Write-Host "证据未齐全时按 Enter 只显示当前缺项；如确需提前结束，按 Q 强制停止。" -ForegroundColor DarkYellow
        Write-CaptureEvidenceStatusIfChanged -Evidence $evidence -TestCase $TestCase -Force
        if ($MaxSec -gt 0) {
            Write-Host ("最长自动采集 {0} 秒，超时会自动停止。" -f $MaxSec) -ForegroundColor DarkYellow
        }

        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        while ($true) {
            $chunk = $serialPort.ReadExisting()
            if (-not [string]::IsNullOrEmpty($chunk)) {
                $writer.Write($chunk)
                $writer.Flush()
                if ($EchoToConsole) {
                    Write-Host -NoNewline $chunk
                }

                $pendingText += $chunk
                $parts = [regex]::Split($pendingText, "\r?\n")
                if ($parts.Count -gt 1) {
                    for ($lineIdx = 0; $lineIdx -lt ($parts.Count - 1); $lineIdx++) {
                        Update-CaptureEvidenceFromLine -Evidence $evidence -TestCase $TestCase -Line $parts[$lineIdx]
                    }
                    $pendingText = $parts[$parts.Count - 1]
                    Write-CaptureEvidenceStatusIfChanged -Evidence $evidence -TestCase $TestCase
                    if (-not $evidenceReady -and (Test-CaptureEvidenceReady -Evidence $evidence -TestCase $TestCase)) {
                        $evidenceReady = $true
                        Write-Host "已看到本项解调运行证据。现在可以继续观察示波器，满意后按 Enter 停止采集。" -ForegroundColor Green
                    }
                }
            }

            if ([Console]::KeyAvailable) {
                $key = [Console]::ReadKey($true)
                if ($key.Key -eq [ConsoleKey]::Enter) {
                    if (Test-CaptureEvidenceReady -Evidence $evidence -TestCase $TestCase) {
                        break
                    }
                    Write-Host "尚未看到完整解调运行证据，继续采集。按 Q 可强制停止。" -ForegroundColor Yellow
                    Write-CaptureEvidenceStatusIfChanged -Evidence $evidence -TestCase $TestCase -Force
                }
                elseif ($key.Key -eq [ConsoleKey]::Q) {
                    $forceStopped = $true
                    Write-Host "用户强制停止本项采集，后续解析会标注缺失项。" -ForegroundColor DarkYellow
                    break
                }
            }

            if ($MaxSec -gt 0 -and $sw.Elapsed.TotalSeconds -ge $MaxSec) {
                $timedOut = $true
                break
            }

            Start-Sleep -Milliseconds 50
        }

        Start-Sleep -Milliseconds 150
        $tail = $serialPort.ReadExisting()
        if (-not [string]::IsNullOrEmpty($tail)) {
            $writer.Write($tail)
            $writer.Flush()
            if ($EchoToConsole) {
                Write-Host -NoNewline $tail
            }
            $pendingText += $tail
        }

        if (-not [string]::IsNullOrWhiteSpace($pendingText)) {
            Update-CaptureEvidenceFromLine -Evidence $evidence -TestCase $TestCase -Line $pendingText
            Write-CaptureEvidenceStatusIfChanged -Evidence $evidence -TestCase $TestCase
        }
    }
    finally {
        if ($writer -ne $null) {
            $writer.Flush()
            $writer.Dispose()
        }
        if ($serialPort.IsOpen) {
            $serialPort.Close()
        }
        $serialPort.Dispose()
    }

    $captureEnd = Get-Date
    $fileInfo = Get-Item -LiteralPath $OutPath
    return [pscustomobject]@{
        Path = $OutPath
        StartTime = $captureStart
        EndTime = $captureEnd
        TimedOut = $timedOut
        ForceStopped = $forceStopped
        EvidenceReady = (Test-CaptureEvidenceReady -Evidence $evidence -TestCase $TestCase)
        EvidenceStatus = (Get-CaptureEvidenceStatusText -Evidence $evidence -TestCase $TestCase)
        Bytes = $fileInfo.Length
    }
}

function Show-TestCase {
    param(
        [object]$TestCase,
        [int]$Index,
        [int]$Total,
        [int]$Attempt
    )

    Write-Host ""
    Write-Host "============================================================" -ForegroundColor DarkCyan
    Write-Host ("[{0}/{1}] {2}  尝试 {3}" -f $Index, $Total, $TestCase.Title, $Attempt) -ForegroundColor Cyan
    Write-Host "请先把射频信号源/自制信号源设置为下面参数：" -ForegroundColor Yellow
    foreach ($item in $TestCase.Params) {
        Write-Host ("  - {0}" -f $item)
    }
    Write-Host ""
    if ($RequiredModeDebugLines -le 0) {
        Write-Host "自动串口模式只等待 demod: mode set + demod_perf/demod_dac；本轮不等待各模式 debug 行。" -ForegroundColor DarkYellow
    }
    else {
        Write-Host "自动串口模式会等到 demod: mode set + demod_perf/demod_dac + 本模式 debug 行；FM 没有 fm_dbg，按 perf/dac 判定。" -ForegroundColor DarkYellow
    }
    if ($CaptureMode -eq "AutoSerial") {
        Write-Host "自动串口模式下脚本会直接保存并解析本项日志；如果串口被占用，可重测或改用 ExternalLog。" -ForegroundColor DarkYellow
    }
    else {
        Write-Host "采集完成后脚本会自动找最新日志；如果自动选择不对，可以手动指定或重测。" -ForegroundColor DarkYellow
    }
}

function Get-LogFilesSince {
    param(
        [datetime]$Since,
        [int]$Limit = 10
    )

    if (-not (Test-Path -LiteralPath $SerialLogDir)) {
        throw "串口日志目录不存在: $SerialLogDir"
    }

    $fresh = @(Get-ChildItem -LiteralPath $SerialLogDir -File -Filter "*.txt" |
        Where-Object { $_.LastWriteTime -ge $Since.AddSeconds(-1 * $FreshLogSlackSec) } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First $Limit)

    return @($fresh)
}

function Get-LatestLogFiles {
    param([int]$Limit = 10)

    if (-not (Test-Path -LiteralPath $SerialLogDir)) {
        throw "串口日志目录不存在: $SerialLogDir"
    }

    return @(Get-ChildItem -LiteralPath $SerialLogDir -File -Filter "*.txt" |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First $Limit)
}

function Select-LogFile {
    param(
        [object[]]$Candidates,
        [datetime]$Since
    )

    Write-Host ""
    if ($Candidates.Count -eq 0) {
        Write-Host ("没有发现本次抓取的新日志。判定条件: LastWriteTime >= {0}" -f $Since.AddSeconds(-1 * $FreshLogSlackSec).ToString("yyyy-MM-dd HH:mm:ss")) -ForegroundColor Red
        Write-Host "这通常说明串口助手没有保存到脚本监控目录，或没有停止保存，或保存到了别的目录。" -ForegroundColor DarkYellow
    }
    else {
        Write-Host "本次新日志候选：" -ForegroundColor Cyan
        for ($i = 0; $i -lt $Candidates.Count; $i++) {
            $f = $Candidates[$i]
            Write-Host ("  [{0}] {1}  {2}  {3}" -f ($i + 1), $f.Name, (Format-Bytes $f.Length), $f.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss"))
        }
    }

    $usingOldList = $false

    while ($true) {
        if ($Candidates.Count -gt 0 -and -not $usingOldList) {
            $prompt = "选择日志: Enter=最新本次日志，数字=指定，M=手动输入路径，R=重测本项，S=跳过本项"
        }
        else {
            $prompt = "选择日志: M=手动输入路径，R=重测本项，S=跳过本项，L=显示最近旧日志"
            if ($usingOldList -and $Candidates.Count -gt 0) {
                $prompt = "选择日志: 数字=选择旧日志，M=手动输入路径，R=重测本项，S=跳过本项"
            }
        }
        $answer = Read-Host $prompt
        $answer = $answer.Trim()

        if ([string]::IsNullOrWhiteSpace($answer)) {
            if ($Candidates.Count -gt 0 -and -not $usingOldList) {
                return [pscustomobject]@{ Action = "Use"; Path = $Candidates[0].FullName }
            }
            Write-Host "没有本次新日志时不会默认选择旧日志，请输入 M/R/S，或输入 L 显示旧日志。" -ForegroundColor DarkYellow
            continue
        }

        if ($answer -match "^[Rr]$") {
            return [pscustomobject]@{ Action = "Retest"; Path = "" }
        }

        if ($answer -match "^[Ss]$") {
            return [pscustomobject]@{ Action = "Skip"; Path = "" }
        }

        if ($answer -match "^[Ll]$") {
            $Candidates = @(Get-LatestLogFiles -Limit 10)
            $usingOldList = $true
            Write-Host ""
            Write-Host "最近旧日志如下。旧日志只建议用于补录，不建议作为当前测试默认结果：" -ForegroundColor DarkYellow
            for ($i = 0; $i -lt $Candidates.Count; $i++) {
                $f = $Candidates[$i]
                Write-Host ("  [{0}] {1}  {2}  {3}" -f ($i + 1), $f.Name, (Format-Bytes $f.Length), $f.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss"))
            }
            continue
        }

        if ($answer -match "^[Mm]$") {
            $manualPath = (Read-Host "输入本项串口日志 txt 完整路径").Trim().Trim('"')
            if (Test-Path -LiteralPath $manualPath) {
                return [pscustomobject]@{ Action = "Use"; Path = $manualPath }
            }
            Write-Host "路径不存在，请重新选择。" -ForegroundColor Red
            continue
        }

        if ($answer -match "^\d+$") {
            $idx = [int]$answer - 1
            if ($idx -ge 0 -and $idx -lt $Candidates.Count) {
                return [pscustomobject]@{ Action = "Use"; Path = $Candidates[$idx].FullName }
            }
        }

        Write-Host "输入无效，请重新输入。" -ForegroundColor Red
    }
}

function Get-LogSummary {
    param([string]$Path)

    $s = [ordered]@{
        LogFile = $Path
        AnalyzeR0Count = 0
        AnalyzeR3Count = 0
        DemodStartCount = 0
        DemodModeSetCount = 0
        DemodPerfCount = 0
        DemodDacCount = 0
        AmDbgCount = 0
        AskDbgCount = 0
        FskDbgCount = 0
        FskRngCount = 0
        PskDbgCount = 0
        AnalyzeMode = ""
        AnalyzeCenterHz = ""
        AnalyzeLowIfHz = ""
        AnalyzeModHz = ""
        AnalyzeDepthPm = ""
        ParamAmDepthPm = ""
        ParamAskDepthPm = ""
        ParamFmDevHz = ""
        ParamFskSepHz = ""
        ParamSymHz = ""
        ParamMask = ""
        ParamConf = ""
        DemodStartAnalyzeModeCode = ""
        DemodStartCenterHz = ""
        DemodStartModHz = ""
        DemodStartSymHz = ""
        DemodStartFskSepHz = ""
        DemodStartLowIfHz = ""
        RxMode = ""
        AnalyzeModeCode = ""
        PerfLastCycles = ""
        PerfMaxCycles = ""
        PerfBudgetCycles = ""
        PerfOver = ""
        PerfBlocks = ""
        PerfOverRatio = ""
        DacPublishOverwrite = ""
        DacHalf = ""
        DacFull = ""
        DacRefresh = ""
        DacLate = ""
        AmClipLast = ""
        AmClipMax = 0
        AmDacMin = ""
        AmDacMax = ""
        AskHighCount = 0
        AskLowCount = 0
        AskLastMean = ""
        AskLastThreshold = ""
        AskLastSpread = ""
        FskHighCount = 0
        FskLowCount = 0
        FskLastMean = ""
        FskLastThreshold = ""
        FskLastSpread = ""
        FskLastDac = ""
        FskRawMinLast = ""
        FskRawMaxLast = ""
        FskSmoothMinLast = ""
        FskSmoothMaxLast = ""
        PskHighCount = 0
        PskLowCount = 0
        PskLastProjection = ""
        PskLastAxisMrad = ""
        PskLastResidualStep = ""
        LastAnalyzeR0 = ""
        LastAnalyzeR3 = ""
        LastDemodStart = ""
        LastDemodModeSet = ""
        LastDemodPerf = ""
        LastDemodDac = ""
        LastModeDebug = ""
    }

    $rxAnalyze0 = "analyze:r0\s+center=(?<center>-?\d+)\s+low_if=(?<lowif>-?\d+)\s+mode=(?<mode>[A-Za-z0-9_]+)\s+mod=(?<mod>-?\d+)\s+depth=(?<depth>-?\d+)"
    $rxAnalyze3 = "analyze:r3.*?am_depth=(?<am>\d+).*?ask_depth=(?<ask>\d+).*?fm_dev=(?<fm>\d+).*?fsk_sep=(?<fsk>\d+).*?sym=(?<sym>\d+).*?pmask=(?<pmask>0x[0-9A-Fa-f]+).*?pconf=(?<pconf>\d+)"
    $rxDemodStart = "demod:\s+start\s+mode=(?<mode>\d+)\s+center=(?<center>\d+)Hz\s+mod=(?<mod>\d+)Hz\s+sym=(?<sym>\d+)Hz\s+fsk_sep=(?<fsk>\d+)Hz\s+low_if=(?<lowif>-?\d+)Hz"
    $rxModeSet = "demod:\s+mode\s+set\s+rx=(?<rx>\d+)\s+analyze=(?<analyze>\d+)"
    $rxPerf = "demod_perf:m=(?<m>\d+),l=(?<last>\d+),x=(?<max>\d+),b=(?<budget>\d+),ov=(?<over>\d+),blk=(?<blocks>\d+)"
    $rxDac = "demod_dac:p=(?<p>\d+),h=(?<h>\d+),f=(?<f>\d+),r=(?<r>\d+),lt=(?<lt>\d+)"
    $rxAm = "am_dbg:.*?d=(?<dmin>-?\d+)\/(?<dmax>-?\d+).*?clip=(?<clip>\d+)"
    $rxAsk = "ask_dbg:.*?m=(?<mean>-?\d+).*?th=(?<th>-?\d+).*?sp=(?<sp>-?\d+).*?d=(?<d>[A-Za-z0-9_]+)"
    $rxFsk = "fsk_dbg:.*?m=(?<mean>-?\d+).*?th=(?<th>-?\d+).*?sp=(?<sp>-?\d+).*?d=(?<d>[A-Za-z0-9_]+)"
    $rxFskRng = "fsk_rng:raw=(?<rawmin>-?\d+)\/(?<rawmax>-?\d+),sm=(?<smmin>-?\d+)\/(?<smmax>-?\d+)"
    $rxPsk = "psk_dbg:.*?pr=(?<pr>-?\d+).*?ax=(?<ax>-?\d+).*?rs=(?<rs>-?\d+).*?d=(?<d>[A-Za-z0-9_]+)"

    Get-Content -LiteralPath $Path -Encoding UTF8 -ReadCount 1000 | ForEach-Object {
        foreach ($line in $_) {
            $text = [string]$line

            if ($text.Contains("analyze:r0")) {
                Add-Count $s "AnalyzeR0Count"
                $s["LastAnalyzeR0"] = $text.Trim()
                if ($text -match $rxAnalyze0) {
                    $s["AnalyzeMode"] = $Matches["mode"]
                    $s["AnalyzeCenterHz"] = $Matches["center"]
                    $s["AnalyzeLowIfHz"] = $Matches["lowif"]
                    $s["AnalyzeModHz"] = $Matches["mod"]
                    $s["AnalyzeDepthPm"] = $Matches["depth"]
                }
            }

            if ($text.Contains("analyze:r3")) {
                Add-Count $s "AnalyzeR3Count"
                $s["LastAnalyzeR3"] = $text.Trim()
                if ($text -match $rxAnalyze3) {
                    $s["ParamAmDepthPm"] = $Matches["am"]
                    $s["ParamAskDepthPm"] = $Matches["ask"]
                    $s["ParamFmDevHz"] = $Matches["fm"]
                    $s["ParamFskSepHz"] = $Matches["fsk"]
                    $s["ParamSymHz"] = $Matches["sym"]
                    $s["ParamMask"] = $Matches["pmask"]
                    $s["ParamConf"] = $Matches["pconf"]
                }
            }

            if ($text.Contains("demod: start")) {
                Add-Count $s "DemodStartCount"
                $s["LastDemodStart"] = $text.Trim()
                if ($text -match $rxDemodStart) {
                    $s["DemodStartAnalyzeModeCode"] = $Matches["mode"]
                    $s["DemodStartCenterHz"] = $Matches["center"]
                    $s["DemodStartModHz"] = $Matches["mod"]
                    $s["DemodStartSymHz"] = $Matches["sym"]
                    $s["DemodStartFskSepHz"] = $Matches["fsk"]
                    $s["DemodStartLowIfHz"] = $Matches["lowif"]
                }
            }

            if ($text.Contains("demod: mode set")) {
                Add-Count $s "DemodModeSetCount"
                $s["LastDemodModeSet"] = $text.Trim()
                if ($text -match $rxModeSet) {
                    $s["RxMode"] = $Matches["rx"]
                    $s["AnalyzeModeCode"] = $Matches["analyze"]
                }
            }

            if ($text.Contains("demod_perf:")) {
                Add-Count $s "DemodPerfCount"
                $s["LastDemodPerf"] = $text.Trim()
                if ($text -match $rxPerf) {
                    $s["PerfLastCycles"] = $Matches["last"]
                    $s["PerfMaxCycles"] = $Matches["max"]
                    $s["PerfBudgetCycles"] = $Matches["budget"]
                    $s["PerfOver"] = $Matches["over"]
                    $s["PerfBlocks"] = $Matches["blocks"]
                    $blocks = [double]$Matches["blocks"]
                    if ($blocks -gt 0) {
                        $s["PerfOverRatio"] = "{0:P2}" -f ([double]$Matches["over"] / $blocks)
                    }
                }
            }

            if ($text.Contains("demod_dac:")) {
                Add-Count $s "DemodDacCount"
                $s["LastDemodDac"] = $text.Trim()
                if ($text -match $rxDac) {
                    $s["DacPublishOverwrite"] = $Matches["p"]
                    $s["DacHalf"] = $Matches["h"]
                    $s["DacFull"] = $Matches["f"]
                    $s["DacRefresh"] = $Matches["r"]
                    $s["DacLate"] = $Matches["lt"]
                }
            }

            if ($text.Contains("am_dbg:")) {
                Add-Count $s "AmDbgCount"
                $s["LastModeDebug"] = $text.Trim()
                if ($text -match $rxAm) {
                    $s["AmDacMin"] = $Matches["dmin"]
                    $s["AmDacMax"] = $Matches["dmax"]
                    $clip = [int]$Matches["clip"]
                    $s["AmClipLast"] = $clip
                    if ($clip -gt [int]$s["AmClipMax"]) {
                        $s["AmClipMax"] = $clip
                    }
                }
            }

            if ($text.Contains("ask_dbg:")) {
                Add-Count $s "AskDbgCount"
                $s["LastModeDebug"] = $text.Trim()
                if ($text -match $rxAsk) {
                    $s["AskLastMean"] = $Matches["mean"]
                    $s["AskLastThreshold"] = $Matches["th"]
                    $s["AskLastSpread"] = $Matches["sp"]
                    if ($Matches["d"].ToLowerInvariant().Contains("high")) {
                        Add-Count $s "AskHighCount"
                    }
                    elseif ($Matches["d"].ToLowerInvariant().Contains("low")) {
                        Add-Count $s "AskLowCount"
                    }
                }
            }

            if ($text.Contains("fsk_dbg:")) {
                Add-Count $s "FskDbgCount"
                $s["LastModeDebug"] = $text.Trim()
                if ($text -match $rxFsk) {
                    $s["FskLastMean"] = $Matches["mean"]
                    $s["FskLastThreshold"] = $Matches["th"]
                    $s["FskLastSpread"] = $Matches["sp"]
                    $s["FskLastDac"] = $Matches["d"]
                    if ($Matches["d"].ToLowerInvariant().Contains("high")) {
                        Add-Count $s "FskHighCount"
                    }
                    elseif ($Matches["d"].ToLowerInvariant().Contains("low")) {
                        Add-Count $s "FskLowCount"
                    }
                }
            }

            if ($text.Contains("fsk_rng:")) {
                Add-Count $s "FskRngCount"
                if ($text -match $rxFskRng) {
                    $s["FskRawMinLast"] = $Matches["rawmin"]
                    $s["FskRawMaxLast"] = $Matches["rawmax"]
                    $s["FskSmoothMinLast"] = $Matches["smmin"]
                    $s["FskSmoothMaxLast"] = $Matches["smmax"]
                }
            }

            if ($text.Contains("psk_dbg:")) {
                Add-Count $s "PskDbgCount"
                $s["LastModeDebug"] = $text.Trim()
                if ($text -match $rxPsk) {
                    $s["PskLastProjection"] = $Matches["pr"]
                    $s["PskLastAxisMrad"] = $Matches["ax"]
                    $s["PskLastResidualStep"] = $Matches["rs"]
                    if ($Matches["d"].ToLowerInvariant().Contains("high")) {
                        Add-Count $s "PskHighCount"
                    }
                    elseif ($Matches["d"].ToLowerInvariant().Contains("low")) {
                        Add-Count $s "PskLowCount"
                    }
                }
            }
        }
    }

    return [pscustomobject]$s
}

function New-WarningText {
    param(
        [object]$TestCase,
        [object]$Summary
    )

    $warnings = New-Object System.Collections.Generic.List[string]

    if ([int]$Summary.DemodStartCount -eq 0) {
        $warnings.Add("未看到 demod: start，本日志可能只是识别阶段，不能判断解调输出。")
    }
    if ([int]$Summary.DemodModeSetCount -eq 0) {
        $warnings.Add("未看到 demod: mode set，解调任务可能没有进入目标模式。")
    }
    if (-not [string]::IsNullOrWhiteSpace($Summary.AnalyzeMode) -and $Summary.AnalyzeMode -ne $TestCase.ExpectedAnalyzeMode) {
        $warnings.Add(("识别模式为 {0}，期望 {1}。" -f $Summary.AnalyzeMode, $TestCase.ExpectedAnalyzeMode))
    }

    switch ($TestCase.Kind) {
        "AM" {
            if ([int]$Summary.AmDbgCount -eq 0) {
                $warnings.Add("未看到 am_dbg，无法判断 AM 削顶和包络 DC 漂移。")
            }
            elseif ([int]$Summary.AmClipMax -gt 0) {
                $warnings.Add(("AM 输出存在削顶计数，clip_max={0}。" -f $Summary.AmClipMax))
            }
        }
        "ASK" {
            if ([int]$Summary.AskDbgCount -eq 0) {
                $warnings.Add("未看到 ask_dbg，无法判断 ASK 双簇判决。")
            }
            elseif ([int]$Summary.AskHighCount -eq 0 -or [int]$Summary.AskLowCount -eq 0) {
                $warnings.Add("ASK debug 只看到单一电平，可能是输入码型、门限或输出链路问题。")
            }
            if ($Summary.ParamSymHz -ne "" -and [int]$Summary.ParamSymHz -ne 10000) {
                $warnings.Add(("ASK 符号率识别为 {0}Hz，不是题目 10000Hz。" -f $Summary.ParamSymHz))
            }
        }
        "FSK" {
            if ([int]$Summary.FskDbgCount -eq 0) {
                $warnings.Add("未看到 fsk_dbg，无法判断 FSK 判决窗口和门限。")
            }
            elseif ([int]$Summary.FskHighCount -eq 0 -or [int]$Summary.FskLowCount -eq 0) {
                $warnings.Add("FSK debug 只看到单一电平，重点查频差识别、低 IF 符号、符号同步窗口。")
            }
            if ($Summary.ParamSymHz -ne "" -and [int]$Summary.ParamSymHz -ne 10000) {
                $warnings.Add(("FSK 符号率识别为 {0}Hz，不是题目 10000Hz。" -f $Summary.ParamSymHz))
            }
            if ($Summary.ParamFskSepHz -ne "" -and [math]::Abs([int]$Summary.ParamFskSepHz - 20000) -gt 2500) {
                $warnings.Add(("FSK 频差识别为 {0}Hz，偏离题目 20000Hz。" -f $Summary.ParamFskSepHz))
            }
        }
        "PSK" {
            if ([int]$Summary.PskDbgCount -eq 0) {
                $warnings.Add("未看到 psk_dbg，无法判断 PSK 载波/轴估计和判决。")
            }
            elseif ([int]$Summary.PskHighCount -eq 0 -or [int]$Summary.PskLowCount -eq 0) {
                $warnings.Add("PSK debug 只看到单一电平，重点查低 IF 补偿、轴估计或符号同步。")
            }
            if ($Summary.ParamSymHz -ne "" -and [int]$Summary.ParamSymHz -ne 10000) {
                $warnings.Add(("PSK 符号率识别为 {0}Hz，不是题目 10000Hz。" -f $Summary.ParamSymHz))
            }
        }
    }

    if ($Summary.PerfOver -ne "" -and $Summary.PerfBlocks -ne "") {
        $blocks = [double]$Summary.PerfBlocks
        if ($blocks -gt 0) {
            $overRatio = [double]$Summary.PerfOver / $blocks
            if ($overRatio -gt 0.05) {
                $warnings.Add(("解调算力超预算比例较高: {0:P2}。" -f $overRatio))
            }
        }
    }

    if ($warnings.Count -eq 0) {
        return "未发现明显日志级异常；以示波器现象为准。"
    }
    return ($warnings -join " ")
}

function New-AttemptRow {
    param(
        [object]$TestCase,
        [int]$Attempt,
        [string]$Status,
        [string]$SourceLog,
        [string]$ArchivedLog,
        [object]$Summary,
        [string]$Observation,
        [string]$Warnings
    )

    return [pscustomobject]@{
        run_tag = $RunTag
        case_id = $TestCase.Id
        mode = $TestCase.Kind
        title = $TestCase.Title
        attempt = $Attempt
        status = $Status
        source_log = $SourceLog
        archived_log = $ArchivedLog
        user_observation = $Observation
        auto_warnings = $Warnings
        analyze_mode = $Summary.AnalyzeMode
        analyze_low_if_hz = $Summary.AnalyzeLowIfHz
        analyze_mod_hz = $Summary.AnalyzeModHz
        param_sym_hz = $Summary.ParamSymHz
        param_fsk_sep_hz = $Summary.ParamFskSepHz
        param_fm_dev_hz = $Summary.ParamFmDevHz
        param_am_depth_pm = $Summary.ParamAmDepthPm
        param_ask_depth_pm = $Summary.ParamAskDepthPm
        rx_mode = $Summary.RxMode
        demod_start_count = $Summary.DemodStartCount
        demod_mode_set_count = $Summary.DemodModeSetCount
        demod_perf_count = $Summary.DemodPerfCount
        perf_last_cycles = $Summary.PerfLastCycles
        perf_max_cycles = $Summary.PerfMaxCycles
        perf_budget_cycles = $Summary.PerfBudgetCycles
        perf_over = $Summary.PerfOver
        perf_blocks = $Summary.PerfBlocks
        perf_over_ratio = $Summary.PerfOverRatio
        dac_publish_overwrite = $Summary.DacPublishOverwrite
        dac_refresh = $Summary.DacRefresh
        am_dbg_count = $Summary.AmDbgCount
        am_clip_max = $Summary.AmClipMax
        ask_dbg_count = $Summary.AskDbgCount
        ask_high_count = $Summary.AskHighCount
        ask_low_count = $Summary.AskLowCount
        fsk_dbg_count = $Summary.FskDbgCount
        fsk_rng_count = $Summary.FskRngCount
        fsk_high_count = $Summary.FskHighCount
        fsk_low_count = $Summary.FskLowCount
        fsk_last_mean = $Summary.FskLastMean
        fsk_last_threshold = $Summary.FskLastThreshold
        fsk_last_spread = $Summary.FskLastSpread
        fsk_raw_last = ("{0}/{1}" -f $Summary.FskRawMinLast, $Summary.FskRawMaxLast)
        psk_dbg_count = $Summary.PskDbgCount
        psk_high_count = $Summary.PskHighCount
        psk_low_count = $Summary.PskLowCount
        psk_last_projection = $Summary.PskLastProjection
        psk_last_axis_mrad = $Summary.PskLastAxisMrad
        psk_last_residual_step = $Summary.PskLastResidualStep
        last_analyze_r0 = $Summary.LastAnalyzeR0
        last_analyze_r3 = $Summary.LastAnalyzeR3
        last_demod_start = $Summary.LastDemodStart
        last_demod_mode_set = $Summary.LastDemodModeSet
        last_demod_perf = $Summary.LastDemodPerf
        last_mode_debug = $Summary.LastModeDebug
    }
}

function Save-Reports {
    param(
        [object[]]$AcceptedRows,
        [object[]]$AttemptRows
    )

    if ($AttemptRows.Count -gt 0) {
        $AttemptRows | Export-Csv -LiteralPath $AttemptsCsv -NoTypeInformation -Encoding UTF8
    }
    if ($AcceptedRows.Count -gt 0) {
        $AcceptedRows | Export-Csv -LiteralPath $SummaryCsv -NoTypeInformation -Encoding UTF8
    }

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# 解调手动测试自动汇总")
    $lines.Add("")
    $lines.Add(("- 生成时间: {0}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")))
    $lines.Add(('- 串口日志目录: `{0}`' -f $SerialLogDir))
    $lines.Add(('- 本次输出目录: `{0}`' -f $RunDir))
    $lines.Add("- 参数来源: 题图 AM/FM/2ASK/2FSK/2PSK 指标。")
    $lines.Add("- 判读说明: 自动判读只基于串口日志关键行，最终仍需结合示波器紫色解调输出和黄色单路基带观察。")
    $lines.Add("")
    $lines.Add("## 已接受测试")
    $lines.Add("")

    if ($AcceptedRows.Count -eq 0) {
        $lines.Add("尚未接受任何测试项。")
    }
    else {
        $lines.Add("| 模式 | 日志 | 现象 | 自动提示 | 关键参数 |")
        $lines.Add("| --- | --- | --- | --- | --- |")
        foreach ($row in $AcceptedRows) {
            $logName = Split-Path -Leaf $row.archived_log
            $paramText = "mode=$($row.analyze_mode); sym=$($row.param_sym_hz); fsk_sep=$($row.param_fsk_sep_hz); low_if=$($row.analyze_low_if_hz); rx=$($row.rx_mode); perf_over=$($row.perf_over_ratio)"
            $lines.Add(('| {0} | `{1}` | {2} | {3} | {4} |' -f `
                (Format-MdCell $row.mode), `
                (Format-MdCell $logName), `
                (Format-MdCell $row.user_observation), `
                (Format-MdCell $row.auto_warnings), `
                (Format-MdCell $paramText)))
        }
    }

    $lines.Add("")
    $lines.Add("## 全部尝试")
    $lines.Add("")
    if ($AttemptRows.Count -eq 0) {
        $lines.Add("尚无尝试记录。")
    }
    else {
        $lines.Add("| 模式 | 尝试 | 状态 | 日志 | 现象 | 自动提示 |")
        $lines.Add("| --- | --- | --- | --- | --- | --- |")
        foreach ($row in $AttemptRows) {
            $logName = if ([string]::IsNullOrWhiteSpace($row.archived_log)) { "" } else { Split-Path -Leaf $row.archived_log }
            $lines.Add(('| {0} | {1} | {2} | `{3}` | {4} | {5} |' -f `
                (Format-MdCell $row.mode), `
                $row.attempt, `
                (Format-MdCell $row.status), `
                (Format-MdCell $logName), `
                (Format-MdCell $row.user_observation), `
                (Format-MdCell $row.auto_warnings)))
        }
    }

    $lines.Add("")
    $lines.Add("## 题图参数清单")
    $lines.Add("")
    foreach ($tc in $TestCases) {
        $lines.Add(("### {0}" -f $tc.Title))
        foreach ($p in $tc.Params) {
            $lines.Add(("- {0}" -f $p))
        }
        $lines.Add("")
    }

    $lines | Set-Content -LiteralPath $SummaryMd -Encoding UTF8
}

function Invoke-OneTestCase {
    param(
        [object]$TestCase,
        [int]$Index,
        [int]$Total,
        [System.Collections.Generic.List[object]]$AcceptedRows,
        [System.Collections.Generic.List[object]]$AttemptRows
    )

    $attempt = 1

    while ($true) {
        Show-TestCase $TestCase $Index $Total $attempt
        $captureStart = Get-Date
        $captureEnd = $captureStart
        $selection = $null
        $activeCaptureMode = $CaptureMode

        if ($activeCaptureMode -eq "AutoSerial") {
            Write-Host ""
            Write-Host ("自动串口采集模式: {0}, {1} baud" -f $SerialPortName, $BaudRate) -ForegroundColor Cyan
            Write-Host "请先关闭串口调试助手或释放同一个 COM 口，否则脚本无法打开串口。" -ForegroundColor Yellow
            Read-Host "设置好信号源、示波器并准备开始后，按 Enter 开始本项串口采集"
            $captureStart = Get-Date
            $safeName = New-SafeFilePart $TestCase.Id
            $capturePath = Join-Path $RunDir ("{0}_try{1:00}_{2}_auto_serial.txt" -f $safeName, $attempt, $captureStart.ToString("yyyyMMdd_HHmmss"))

            try {
                $captureResult = Read-SerialCapture -OutPath $capturePath -TestCase $TestCase -PortName $SerialPortName -PortBaudRate $BaudRate -MaxSec $AutoCaptureMaxSec -CommandToSend $StartCommand -EchoToConsole:$EchoSerialToConsole
                $captureEnd = $captureResult.EndTime
                $selection = [pscustomobject]@{ Action = "Use"; Path = $captureResult.Path; AutoCaptured = $true }
                Write-Host ("本项串口采集完成: {0}, {1}" -f $captureResult.Path, (Format-Bytes $captureResult.Bytes)) -ForegroundColor Green
                Write-Host ("采集证据状态: {0}" -f $captureResult.EvidenceStatus) -ForegroundColor DarkCyan
                if ($captureResult.TimedOut) {
                    Write-Host "注意: 本项达到最长采集时间后自动停止。" -ForegroundColor DarkYellow
                }
                if ($captureResult.ForceStopped) {
                    Write-Host "注意: 本项由用户强制停止，日志可能仍缺少解调运行证据。" -ForegroundColor DarkYellow
                }
                elseif (-not $captureResult.EvidenceReady) {
                    Write-Host "注意: 本项结束时仍未看到完整解调运行证据，建议解析后选择重测。" -ForegroundColor DarkYellow
                }
            }
            catch {
                Write-Host ("自动串口采集失败: {0}" -f $_.Exception.Message) -ForegroundColor Red
                $fallback = (Read-Host "输入 R=重测本项，E=改用外部日志选择，S=跳过本项").Trim()
                if ($fallback -match "^[Rr]$") {
                    $attempt++
                    continue
                }
                elseif ($fallback -match "^[Ss]$") {
                    $selection = [pscustomobject]@{ Action = "Skip"; Path = ""; AutoCaptured = $false }
                }
                else {
                    $activeCaptureMode = "ExternalLog"
                }
            }
        }

        if ($activeCaptureMode -eq "ExternalLog" -and $selection -eq $null) {
            Read-Host "设置好信号源、示波器和串口助手后，按 Enter 记录本项抓取起点"
            $captureStart = Get-Date
            $suggestedLogName = "{0}_try{1:00}_{2}.txt" -f $TestCase.Id, $attempt, $captureStart.ToString("yyyyMMdd_HHmmss")
            Write-Host ""
            Write-Host ("建议本项串口助手保存文件名: {0}" -f $suggestedLogName) -ForegroundColor Cyan
            Write-Host "现在开始/保持串口助手抓取，并触发板子进入任务；采集足够后，在串口助手停止并保存日志。" -ForegroundColor Yellow
            Read-Host "停止串口抓取并保存后，回到这里按 Enter"
            $captureEnd = Get-Date

            $candidates = Get-LogFilesSince -Since $captureStart
            $selection = Select-LogFile -Candidates $candidates -Since $captureStart
        }

        if ($selection.Action -eq "Retest") {
            $attempt++
            continue
        }

        if ($selection.Action -eq "Skip") {
            $emptySummary = [pscustomobject]@{
                AnalyzeMode = ""; AnalyzeLowIfHz = ""; AnalyzeModHz = ""; ParamSymHz = ""; ParamFskSepHz = ""; ParamFmDevHz = ""; ParamAmDepthPm = ""; ParamAskDepthPm = ""; RxMode = "";
                DemodStartCount = 0; DemodModeSetCount = 0; DemodPerfCount = 0; PerfLastCycles = ""; PerfMaxCycles = ""; PerfBudgetCycles = ""; PerfOver = ""; PerfBlocks = ""; PerfOverRatio = "";
                DacPublishOverwrite = ""; DacRefresh = ""; AmDbgCount = 0; AmClipMax = 0; AskDbgCount = 0; AskHighCount = 0; AskLowCount = 0;
                FskDbgCount = 0; FskRngCount = 0; FskHighCount = 0; FskLowCount = 0; FskLastMean = ""; FskLastThreshold = ""; FskLastSpread = ""; FskRawMinLast = ""; FskRawMaxLast = "";
                PskDbgCount = 0; PskHighCount = 0; PskLowCount = 0; PskLastProjection = ""; PskLastAxisMrad = ""; PskLastResidualStep = "";
                LastAnalyzeR0 = ""; LastAnalyzeR3 = ""; LastDemodStart = ""; LastDemodModeSet = ""; LastDemodPerf = ""; LastModeDebug = ""
            }
            $row = New-AttemptRow $TestCase $attempt "skipped" "" "" $emptySummary "" "用户跳过。"
            $AttemptRows.Add($row)
            Save-Reports $AcceptedRows.ToArray() $AttemptRows.ToArray()
            return
        }

        $sourceLog = $selection.Path
        if ($selection.AutoCaptured) {
            $archivedLog = $sourceLog
        }
        else {
            $safeName = New-SafeFilePart $TestCase.Id
            $sourceLeaf = Split-Path -Leaf $sourceLog
            $sourceStem = New-SafeFilePart ([System.IO.Path]::GetFileNameWithoutExtension($sourceLeaf))
            $sourceExt = [System.IO.Path]::GetExtension($sourceLeaf)
            if ([string]::IsNullOrWhiteSpace($sourceExt)) {
                $sourceExt = ".txt"
            }
            $timeTag = "{0}_{1}" -f $captureStart.ToString("yyyyMMdd_HHmmss"), $captureEnd.ToString("HHmmss")
            $archivedLog = Join-Path $RunDir ("{0}_{1}_try{2:00}_{3}{4}" -f $safeName, $timeTag, $attempt, $sourceStem, $sourceExt)
            Copy-Item -LiteralPath $sourceLog -Destination $archivedLog -Force
        }

        Write-Host ""
        Write-Host ("已归档日志: {0}" -f $archivedLog) -ForegroundColor Green
        Write-Host "正在解析关键串口行..."

        $summary = Get-LogSummary -Path $archivedLog
        $warnings = New-WarningText -TestCase $TestCase -Summary $summary

        Write-Host ""
        Write-Host "自动解析摘要：" -ForegroundColor Cyan
        Write-Host ("  analyze_mode={0}, sym={1}, fsk_sep={2}, low_if={3}, rx={4}" -f $summary.AnalyzeMode, $summary.ParamSymHz, $summary.ParamFskSepHz, $summary.AnalyzeLowIfHz, $summary.RxMode)
        Write-Host ("  demod_start={0}, mode_set={1}, perf_lines={2}, mode_dbg={3}" -f $summary.DemodStartCount, $summary.DemodModeSetCount, $summary.DemodPerfCount, $summary.LastModeDebug)
        Write-Host ("  自动提示: {0}" -f $warnings) -ForegroundColor DarkYellow

        $observation = Read-Host "输入本项示波器/输出现象备注"
        $accept = Read-Host "接受这次日志吗？Enter/Y=接受，R=重测本项，S=跳过本项"
        $accept = $accept.Trim()

        if ($accept -match "^[Rr]$") {
            $row = New-AttemptRow $TestCase $attempt "rejected_retest" $sourceLog $archivedLog $summary $observation $warnings
            $AttemptRows.Add($row)
            Save-Reports $AcceptedRows.ToArray() $AttemptRows.ToArray()
            $attempt++
            continue
        }

        if ($accept -match "^[Ss]$") {
            $row = New-AttemptRow $TestCase $attempt "skipped_after_capture" $sourceLog $archivedLog $summary $observation $warnings
            $AttemptRows.Add($row)
            Save-Reports $AcceptedRows.ToArray() $AttemptRows.ToArray()
            return
        }

        $acceptedRow = New-AttemptRow $TestCase $attempt "accepted" $sourceLog $archivedLog $summary $observation $warnings
        $AttemptRows.Add($acceptedRow)
        $AcceptedRows.Add($acceptedRow)
        Save-Reports $AcceptedRows.ToArray() $AttemptRows.ToArray()
        Write-Host ("已接受 {0}，当前汇总已更新。" -f $TestCase.Title) -ForegroundColor Green
        return
    }
}

if ($DryRun) {
    Write-Host "DryRun: 将按以下题图参数逐项测试。"
    for ($i = 0; $i -lt $TestCases.Count; $i++) {
        Show-TestCase $TestCases[$i] ($i + 1) $TestCases.Count 1
    }
    return
}

New-Item -ItemType Directory -Path $RunDir -Force | Out-Null

Write-Host "解调手动测试自动化脚本" -ForegroundColor Cyan
Write-Host ("本次输出目录: {0}" -f $RunDir)
Write-Host ("采集模式: {0}" -f $CaptureMode)
if ($CaptureMode -eq "AutoSerial") {
    Write-Host ("自动串口: {0}, {1} baud" -f $SerialPortName, $BaudRate)
    Write-Host "流程: 显示参数 -> 关闭串口助手并设置仪器 -> 按 Enter 开始脚本采集 -> 触发板子进入任务 -> 脚本等到解调证据齐全 -> 观察示波器后按 Enter 停止 -> 自动解析 -> 输入现象 -> 接受或重测。"
    Write-Host "自动停止规则: Enter 在证据齐全前不会结束采集；需要提前结束时按 Q 强制停止。"
    Write-Host "注意: 自动串口模式会独占 COM 口；如果要继续使用串口助手，请用 -CaptureMode ExternalLog。"
}
else {
    Write-Host ("串口日志目录: {0}" -f $SerialLogDir)
    Write-Host "流程: 显示参数 -> 你设置仪器并按 Enter -> 串口助手抓取 -> 你停止保存 -> 脚本归档/解析 -> 输入现象 -> 接受或重测。"
    Write-Host "注意: 默认只选择本项开始后新生成/更新的日志；没有新日志时不会自动拿旧日志。"
}
Write-Host ""

$acceptedRows = New-Object System.Collections.Generic.List[object]
$attemptRows = New-Object System.Collections.Generic.List[object]

for ($i = 0; $i -lt $TestCases.Count; $i++) {
    Invoke-OneTestCase -TestCase $TestCases[$i] -Index ($i + 1) -Total $TestCases.Count -AcceptedRows $acceptedRows -AttemptRows $attemptRows
}

Save-Reports $acceptedRows.ToArray() $attemptRows.ToArray()

Write-Host ""
Write-Host "全部测试流程结束。" -ForegroundColor Green
Write-Host ("汇总 Markdown: {0}" -f $SummaryMd)
if (Test-Path -LiteralPath $SummaryCsv) {
    Write-Host ("已接受 CSV: {0}" -f $SummaryCsv)
}
if (Test-Path -LiteralPath $AttemptsCsv) {
    Write-Host ("全部尝试 CSV: {0}" -f $AttemptsCsv)
}

