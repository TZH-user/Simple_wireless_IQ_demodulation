param(
    [ValidateSet("AutoSerial", "ExternalLog")]
    [string]$CaptureMode = "AutoSerial",
    [string]$SerialPortName = "COM14",
    [int]$BaudRate = 1500000,
    [int]$AutoCaptureMaxSec = 90,
    [string]$CarrierHint = "Use the current carrier. Example: 20MHz carrier -> 2FSK second tone 20.020MHz; 120MHz carrier -> 120.020MHz.",
    [switch]$EchoSerialToConsole,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ManualScript = Join-Path $ScriptDir "run_manual_demod_scope_log_20260525.ps1"

if (!(Test-Path -LiteralPath $ManualScript)) {
    throw "Cannot find manual demod script: $ManualScript"
}

Write-Host ""
Write-Host "H743 low-compute phase-0 acceptance helper" -ForegroundColor Cyan
Write-Host ""
Write-Host "Purpose:" -ForegroundColor Yellow
Write-Host "  1. Verify phase 0 only added switches/log fields and did not replace demod paths."
Write-Host "  2. Capture low-load logs: demod: mode set, demod_perf, demod_dac."
Write-Host "  3. Do not wait for am_dbg/ask_dbg/fsk_dbg/psk_dbg because RX_DEMOD_DEBUG_PROFILE is NONE."
Write-Host ""
Write-Host "Before starting:" -ForegroundColor Yellow
Write-Host "  - Flash the current firmware."
Write-Host "  - Use DC coupling on the oscilloscope and record high-Z or 50 ohm load."
Write-Host "  - Keep detailed per-mode debug and point-by-point spectrum logs disabled."
Write-Host "  - Judge demod output against 100 mVpp at 50 ohm load."
Write-Host ""

$argsList = @(
    "-CaptureMode", $CaptureMode,
    "-SerialPortName", $SerialPortName,
    "-BaudRate", $BaudRate,
    "-AutoCaptureMaxSec", $AutoCaptureMaxSec,
    "-RequiredModeDebugLines", 0,
    "-CarrierHint", $CarrierHint
)

if ($EchoSerialToConsole) {
    $argsList += "-EchoSerialToConsole"
}

if ($DryRun) {
    $argsList += "-DryRun"
}

& powershell -ExecutionPolicy Bypass -File $ManualScript @argsList
