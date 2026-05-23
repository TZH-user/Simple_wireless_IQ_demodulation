@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "PS1_FILE=%SCRIPT_DIR%stm32_debug_port_guard.ps1"

if not exist "%PS1_FILE%" (
    echo ERROR: PowerShell script not found:
    echo   %PS1_FILE%
    echo.
    pause
    exit /b 1
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PS1_FILE%" %*
set "EXIT_CODE=%ERRORLEVEL%"

echo.
echo Script exited with code %EXIT_CODE%.
pause
exit /b %EXIT_CODE%
