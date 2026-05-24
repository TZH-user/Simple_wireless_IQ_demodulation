param(
    [string]$BaseUrl = "http://127.0.0.1:8766",
    [string]$SerialPortName = "COM14",
    [int]$BaudRate = 1500000,
    [int]$TargetFreqHz = 120000000,
    [int]$CenterToleranceHz = 300000,
    [int[]]$PowerStepsDbm = @(-20, -15, -10),
    [int]$ValidRepeats = 3,
    [int]$CaptureTimeoutSec = 90,
    [int]$PostPerfLines = 0,
    [int]$PostDemodSettleMs = 800,
    [string[]]$Tests = @("CW", "AM", "FM", "ASK", "FSK2", "BPSK"),
    [string]$OutputTag = "",
    [switch]$RunAllPowerSteps,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($OutputTag)) {
    $OutputTag = Get-Date -Format "yyyyMMdd_HHmmss"
}
$OutPrefix = if ($DryRun) { "120MHz_dataset_dryrun" } else { "120MHz_dataset_raw" }
$OutDir = Join-Path $ScriptDir ("{0}_{1}" -f $OutPrefix, $OutputTag)
$SummaryPath = Join-Path $OutDir "120MHz_dataset_summary_20260525.csv"
$Programmer = "C:\Users\18152\AppData\Local\stm32cube\bundles\programmer\2.22.0+st.1\bin\STM32_Programmer_CLI.exe"

function Invoke-E8267DGet {
    param([string]$Path)
    $lastError = $null
    for ($try = 1; $try -le 3; $try++) {
        try {
            return Invoke-RestMethod -Uri "$BaseUrl$Path" -Method Get -TimeoutSec 15
        }
        catch {
            $lastError = $_
            Start-Sleep -Milliseconds (300 * $try)
        }
    }
    throw $lastError
}

function Invoke-E8267DPost {
    param([string]$Path, [hashtable]$Body)
    $lastError = $null
    for ($try = 1; $try -le 3; $try++) {
        try {
            return Invoke-RestMethod -Uri "$BaseUrl$Path" -Method Post -ContentType "application/json" -Body ($Body | ConvertTo-Json -Depth 6) -TimeoutSec 15
        }
        catch {
            $lastError = $_
            Start-Sleep -Milliseconds (300 * $try)
        }
    }
    throw $lastError
}

function Set-BasicOutput {
    param(
        [int]$PowerDbm,
        [bool]$RfEnabled,
        [bool]$ModEnabled
    )
    Invoke-E8267DPost "/api/basic" @{
        frequency_hz = $TargetFreqHz
        power_dbm = $PowerDbm
        rf_enabled = $RfEnabled
        mod_enabled = $ModEnabled
    } | Out-Null
}

function Set-RfOff {
    param([int]$PowerDbm)
    Set-BasicOutput $PowerDbm $false $false
}

function Set-RfOn {
    param(
        [int]$PowerDbm,
        [bool]$ModEnabled
    )
    Set-BasicOutput $PowerDbm $true $ModEnabled
}

function Disable-CustomDigital {
    Invoke-E8267DPost "/api/digital/custom" @{
        enabled = $false
        modulation = "BPSK"
        symbol_rate_hz = 10000
        filter_type = "RECT"
        data_mode = "loop01"
        fixed_bits = "01"
        differential_encoding = $false
    } | Out-Null
}

function Disable-AnalogModulation {
    Invoke-E8267DPost "/api/modulation/am" @{
        enabled = $false
        source = "INT"
        depth_percent = 50
        internal_frequency_hz = 10000
        internal_function = "SIN"
    } | Out-Null

    Invoke-E8267DPost "/api/modulation/fm" @{
        enabled = $false
        source = "INT"
        deviation_hz = 75000
        internal_frequency_hz = 10000
        internal_function = "SIN"
    } | Out-Null
}

function Disable-AllModulation {
    Disable-AnalogModulation
    Disable-CustomDigital
}

function Test-RequiresMasterMod {
    param([string]$Modulation)
    return ($Modulation -ne "CW")
}

function Set-TestSignal {
    param(
        [string]$Modulation,
        [int]$PowerDbm
    )

    Set-RfOff $PowerDbm
    Disable-AllModulation

    switch ($Modulation) {
        "CW" {
            Invoke-E8267DPost "/api/basic" @{
                frequency_hz = $TargetFreqHz
                power_dbm = $PowerDbm
                rf_enabled = $false
                mod_enabled = $false
            } | Out-Null
        }
        "AM" {
            Disable-CustomDigital
            Invoke-E8267DPost "/api/modulation/am" @{
                enabled = $true
                source = "INT"
                depth_percent = 50
                internal_frequency_hz = 10000
                internal_function = "SIN"
            } | Out-Null
        }
        "FM" {
            Disable-CustomDigital
            Invoke-E8267DPost "/api/modulation/fm" @{
                enabled = $true
                source = "INT"
                deviation_hz = 75000
                internal_frequency_hz = 10000
                internal_function = "SIN"
            } | Out-Null
        }
        "ASK" {
            Disable-AnalogModulation
            Invoke-E8267DPost "/api/digital/custom" @{
                enabled = $true
                modulation = "ASK"
                symbol_rate_hz = 10000
                filter_type = "RECT"
                data_mode = "loop01"
                fixed_bits = "01"
                differential_encoding = $false
                ask_depth_percent = 100
            } | Out-Null
        }
        "FSK2" {
            Disable-AnalogModulation
            Invoke-E8267DPost "/api/digital/custom" @{
                enabled = $true
                modulation = "FSK2"
                symbol_rate_hz = 10000
                filter_type = "RECT"
                data_mode = "loop01"
                fixed_bits = "01"
                differential_encoding = $false
                fsk_deviation_hz = 20000
            } | Out-Null
        }
        "BPSK" {
            Disable-AnalogModulation
            Invoke-E8267DPost "/api/digital/custom" @{
                enabled = $true
                modulation = "BPSK"
                symbol_rate_hz = 10000
                filter_type = "RECT"
                data_mode = "loop01"
                fixed_bits = "01"
                differential_encoding = $false
            } | Out-Null
        }
        default {
            throw "Unsupported modulation: $Modulation"
        }
    }
}

function Reset-Mcu {
    & $Programmer -c port=SWD mode=UR reset=HWrst -rst | Out-Null
}

function Read-SerialCapture {
    param([int]$TimeoutSec)

    $sp = New-Object System.IO.Ports.SerialPort($SerialPortName, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $sp.ReadTimeout = 200
    $sp.DtrEnable = $false
    $sp.RtsEnable = $false
    $sp.Open()
    try {
        Start-Sleep -Milliseconds 200
        [void]$sp.ReadExisting()
        Reset-Mcu

        $buf = New-Object System.Text.StringBuilder
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        $sawSweepDoneAt = $null
        $lastDataAt = Get-Date
        $sawSpectrumTail = $false
        $demodPerfCount = 0
        $sawDemodModeAt = $null
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 200
            $chunk = $sp.ReadExisting()
            if ($chunk.Length -gt 0) {
                $lastDataAt = Get-Date
                [void]$buf.Append($chunk)
                $text = $buf.ToString()
                $demodPerfCount += ([regex]::Matches($chunk, "demod_perf:")).Count
                if (($null -eq $sawSweepDoneAt) -and ($text -match "moddetect: sweep done center=\d+Hz")) {
                    $sawSweepDoneAt = Get-Date
                }
                if (($null -eq $sawDemodModeAt) -and ($text -match "demod: mode set")) {
                    $sawDemodModeAt = Get-Date
                }
                if ($text -match "spec:[0-9.]+,[0-9.]+,[-0-9.]+,20[0-9][0-9],3|format:ch0=") {
                    $sawSpectrumTail = $true
                }
                if (($PostPerfLines -le 0) -and
                    ($null -ne $sawDemodModeAt) -and
                    (((Get-Date) - $sawDemodModeAt).TotalMilliseconds -ge $PostDemodSettleMs)) {
                    break
                }
                if ($sawSpectrumTail -and (((Get-Date) - $lastDataAt).TotalSeconds -gt 2)) {
                    break
                }
                if (($PostPerfLines -gt 0) -and ($demodPerfCount -ge $PostPerfLines)) {
                    break
                }
            } elseif ($sawSpectrumTail -and (((Get-Date) - $lastDataAt).TotalSeconds -gt 2)) {
                break
            }
        }
        return $buf.ToString()
    }
    finally {
        if ($sp.IsOpen) {
            $sp.Close()
        }
    }
}

function Get-CenterHz {
    param([string]$Text)
    $matches = [regex]::Matches($Text, "moddetect: sweep done center=(\d+)Hz|moddetect: sweep unlocked center=(\d+)Hz")
    if ($matches.Count -eq 0) {
        return $null
    }
    $m = $matches[$matches.Count - 1]
    if ($m.Groups[1].Success) {
        return [int64]$m.Groups[1].Value
    }
    if ($m.Groups[2].Success) {
        return [int64]$m.Groups[2].Value
    }
    return $null
}

function Get-DetectedMode {
    param([string]$Text)
    $m = [regex]::Matches($Text, "mode=([A-Z0-9_]+)")
    if ($m.Count -eq 0) {
        return ""
    }
    return $m[$m.Count - 1].Groups[1].Value
}

function Test-ValidLog {
    param([string]$Text)
    $center = Get-CenterHz $Text
    if ($null -eq $center) {
        return $false
    }
    if ($center -eq 0) {
        return $false
    }
    if ([Math]::Abs($center - $TargetFreqHz) -gt $CenterToleranceHz) {
        return $false
    }
    return ($Text -match "moddetect: sweep done center=\d+Hz|analyze:r0|analyze:r1|analyze:r2|analyze:r3|analyze: result")
}

function Append-Summary {
    param(
        [string]$RunId,
        [string]$Modulation,
        [int]$PowerDbm,
        [Nullable[int64]]$CenterHz,
        [string]$DetectedMode,
        [bool]$Valid,
        [string]$RawLogPath,
        [string]$Notes
    )

    if (-not (Test-Path $SummaryPath)) {
        "run_id,modulation,power_dbm,target_freq_hz,center_hz,center_error_hz,detected_mode,valid,raw_log_path,notes" | Set-Content -Path $SummaryPath -Encoding UTF8
    }

    $centerText = ""
    $errText = ""
    if ($null -ne $CenterHz) {
        $centerText = [string]$CenterHz
        $errText = [string]($CenterHz - $TargetFreqHz)
    }
    $safeNotes = $Notes.Replace('"', '""')
    $line = '"{0}","{1}",{2},{3},"{4}","{5}","{6}","{7}","{8}","{9}"' -f $RunId, $Modulation, $PowerDbm, $TargetFreqHz, $centerText, $errText, $DetectedMode, $Valid, $RawLogPath, $safeNotes
    Add-Content -Path $SummaryPath -Value $line -Encoding UTF8
}

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

Write-Host "Read E8267D status..."
Invoke-E8267DGet "/api/status" | ConvertTo-Json -Depth 5
Invoke-E8267DGet "/api/digital/status" | ConvertTo-Json -Depth 5

Write-Host "Preset default, RF stays OFF..."
if (-not $DryRun) {
    Invoke-E8267DPost "/api/preset/default" @{} | Out-Null
    Disable-AllModulation
    Set-RfOff $PowerStepsDbm[0]
}

$tests = $Tests

foreach ($mod in $tests) {
    $totalValidCount = 0
    foreach ($power in $PowerStepsDbm) {
        $attempt = 1
        $validCount = 0
        while (($validCount -lt $ValidRepeats) -and ($attempt -le $ValidRepeats)) {
            $runId = "{0}_120M_{1}dBm_run{2}" -f $mod, $power, $attempt
            Write-Host "=== $runId ==="
            $logPath = Join-Path $OutDir ("{0}.txt" -f $runId)

            if ($DryRun) {
                "DRYRUN $runId" | Set-Content -Path $logPath -Encoding UTF8
                Append-Summary $runId $mod $power $null "" $false $logPath "dry-run"
                $attempt++
                continue
            }

            $text = ""
            $valid = $false
            $center = $null
            $mode = ""
            try {
                Set-TestSignal $mod $power
                Set-RfOn $power (Test-RequiresMasterMod $mod)
                $text = Read-SerialCapture $CaptureTimeoutSec
                $valid = Test-ValidLog $text
                $center = Get-CenterHz $text
                $mode = Get-DetectedMode $text
            }
            finally {
                Disable-AllModulation
                Set-RfOff $power
            }

            $suffix = if ($valid) { "valid" } else { "reject" }
            $finalLogPath = Join-Path $OutDir ("{0}_{1}.txt" -f $runId, $suffix)
            $text | Set-Content -Path $finalLogPath -Encoding UTF8
            Append-Summary $runId $mod $power $center $mode $valid $finalLogPath ""
            if ($valid) {
                $validCount++
                $totalValidCount++
            }
            $attempt++
        }
        if (($RunAllPowerSteps.IsPresent -eq $false) -and ($validCount -ge $ValidRepeats)) {
            break
        }
        Write-Host "$mod valid count at $power dBm: $validCount, try next power if available."
    }
    Write-Host "$mod final valid count: $totalValidCount"
}

if (-not $DryRun) {
    Disable-AllModulation
}
Set-RfOff $PowerStepsDbm[0]
Write-Host "Summary: $SummaryPath"
