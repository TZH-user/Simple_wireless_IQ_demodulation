param(
    [string[]]$Ports = @("60000", "60001", "61234", "61235"),
    [switch]$ScanOnly,
    [switch]$SafeKill
)

$ErrorActionPreference = "Stop"

$knownDebugNames = @(
    "ST-LINK_gdbserver",
    "ST-LINK_gdbserver.exe",
    "arm-none-eabi-gdb",
    "arm-none-eabi-gdb.exe",
    "rtos-proxy",
    "rtos-proxy.exe",
    "openocd",
    "openocd.exe"
)

function Convert-ToPortList {
    param([string[]]$RawPorts)

    $portSet = New-Object System.Collections.Generic.HashSet[int]
    foreach ($rawItem in $RawPorts) {
        foreach ($part in ($rawItem -split ",")) {
            $trimmed = $part.Trim()
            if ([string]::IsNullOrWhiteSpace($trimmed)) {
                continue
            }

            $portNumber = 0
            if (-not [int]::TryParse($trimmed, [ref]$portNumber)) {
                throw "Invalid port value: $trimmed"
            }

            if (($portNumber -lt 1) -or ($portNumber -gt 65535)) {
                throw "Port out of range: $portNumber"
            }

            [void]$portSet.Add($portNumber)
        }
    }

    return @($portSet | Sort-Object)
}

function Get-ProcessSummary {
    param([int]$ProcessId)

    try {
        $proc = Get-Process -Id $ProcessId -ErrorAction Stop
        return [pscustomobject]@{
            ProcessName = $proc.ProcessName
            Path        = $proc.Path
        }
    } catch {
        return [pscustomobject]@{
            ProcessName = "<unknown>"
            Path        = ""
        }
    }
}

function Test-KnownDebugProcess {
    param([string]$ProcessName)

    foreach ($knownName in $knownDebugNames) {
        if ($ProcessName -ieq $knownName) {
            return $true
        }
    }

    return $false
}

function New-PortRecord {
    param(
        [string]$Protocol,
        [string]$LocalAddress,
        [int]$LocalPort,
        [string]$State,
        [int]$ProcessId
    )

    $procInfo = Get-ProcessSummary -ProcessId $ProcessId
    $isKnownDebug = Test-KnownDebugProcess -ProcessName $procInfo.ProcessName

    [pscustomobject]@{
        Index       = 0
        Protocol    = $Protocol
        LocalAddr   = $LocalAddress
        Port        = $LocalPort
        State       = $State
        PID         = $ProcessId
        ProcessName = $procInfo.ProcessName
        SafeDebug   = $isKnownDebug
        Path        = $procInfo.Path
    }
}

function Add-UniquePortRecord {
    param(
        [System.Collections.Generic.List[object]]$Records,
        [object]$Record
    )

    foreach ($existing in $Records) {
        if (($existing.Protocol -eq $Record.Protocol) -and
            ($existing.LocalAddr -eq $Record.LocalAddr) -and
            ($existing.Port -eq $Record.Port) -and
            ($existing.State -eq $Record.State) -and
            ($existing.PID -eq $Record.PID)) {
            return
        }
    }

    $Records.Add($Record)
}

function Get-NetstatPortRecords {
    param([int[]]$TargetPorts)

    $records = New-Object System.Collections.Generic.List[object]
    $lines = @(netstat -aon)

    foreach ($line in $lines) {
        $trimmed = $line.Trim()
        if ([string]::IsNullOrWhiteSpace($trimmed)) {
            continue
        }

        $tokens = @($trimmed -split "\s+")
        if ($tokens.Count -lt 4) {
            continue
        }

        $protocol = $tokens[0]
        if (($protocol -ne "TCP") -and ($protocol -ne "UDP")) {
            continue
        }

        $localEndpoint = $tokens[1]
        if ($localEndpoint -notmatch ":(\d+)$") {
            continue
        }

        $localPort = [int]$matches[1]
        if ($TargetPorts -notcontains $localPort) {
            continue
        }

        $localAddress = $localEndpoint -replace ":\d+$", ""

        if ($protocol -eq "TCP") {
            if ($tokens.Count -lt 5) {
                continue
            }

            $state = $tokens[3]
            $processId = [int]$tokens[4]
        } else {
            $state = "-"
            $processId = [int]$tokens[3]
        }

        Add-UniquePortRecord -Records $records -Record (New-PortRecord `
            -Protocol $protocol `
            -LocalAddress $localAddress `
            -LocalPort $localPort `
            -State $state `
            -ProcessId $processId
        )
    }

    return $records
}

function Get-DebugPortOccupants {
    param([int[]]$TargetPorts)

    $records = New-Object System.Collections.Generic.List[object]

    try {
        foreach ($port in $TargetPorts) {
            $tcpItems = Get-NetTCPConnection -LocalPort $port -ErrorAction SilentlyContinue
            foreach ($item in $tcpItems) {
                Add-UniquePortRecord -Records $records -Record (New-PortRecord `
                    -Protocol "TCP" `
                    -LocalAddress $item.LocalAddress `
                    -LocalPort $item.LocalPort `
                    -State $item.State `
                    -ProcessId $item.OwningProcess
                )
            }
        }
    } catch {
        Write-Warning "Get-NetTCPConnection failed: $($_.Exception.Message)"
    }

    try {
        foreach ($port in $TargetPorts) {
            $udpItems = Get-NetUDPEndpoint -LocalPort $port -ErrorAction SilentlyContinue
            foreach ($item in $udpItems) {
                Add-UniquePortRecord -Records $records -Record (New-PortRecord `
                    -Protocol "UDP" `
                    -LocalAddress $item.LocalAddress `
                    -LocalPort $item.LocalPort `
                    -State "-" `
                    -ProcessId $item.OwningProcess
                )
            }
        }
    } catch {
        Write-Warning "Get-NetUDPEndpoint failed: $($_.Exception.Message)"
    }

    try {
        $netstatRecords = @(Get-NetstatPortRecords -TargetPorts $TargetPorts)
        foreach ($item in $netstatRecords) {
            Add-UniquePortRecord -Records $records -Record $item
        }
    } catch {
        Write-Warning "netstat fallback failed: $($_.Exception.Message)"
    }

    $i = 1
    foreach ($record in $records) {
        $record.Index = $i
        $i++
    }

    return $records
}

function Stop-UniqueProcesses {
    param(
        [object[]]$Records,
        [switch]$RequireExtraConfirmForUnknown
    )

    $targets = $Records | Sort-Object PID -Unique

    foreach ($target in $targets) {
        if ($RequireExtraConfirmForUnknown -and -not $target.SafeDebug) {
            Write-Host ""
            Write-Warning "PID $($target.PID) / $($target.ProcessName) is not a known STM32 debug process."
            $confirmUnknown = Read-Host "Type KILL to stop this unknown process, or press Enter to skip"
            if ($confirmUnknown -cne "KILL") {
                Write-Host "Skipped PID $($target.PID)."
                continue
            }
        }

        try {
            Write-Host "Stopping PID $($target.PID) / $($target.ProcessName) ..."
            Stop-Process -Id $target.PID -Force -ErrorAction Stop
            Write-Host "Stopped PID $($target.PID)."
        } catch {
            Write-Warning "Failed to stop PID $($target.PID): $($_.Exception.Message)"
        }
    }
}

$targetPorts = Convert-ToPortList -RawPorts $Ports

Write-Host "STM32 debug port guard"
Write-Host "Target ports: $($targetPorts -join ', ')"
Write-Host ""

$records = @(Get-DebugPortOccupants -TargetPorts $targetPorts)

if ($records.Count -eq 0) {
    Write-Host "No target port occupancy found."
    exit 0
}

Write-Host "Current occupancy:"
$records |
    Select-Object Index, Protocol, LocalAddr, Port, State, PID, ProcessName, SafeDebug |
    Format-Table -AutoSize

if ($ScanOnly) {
    Write-Host "ScanOnly mode: no process will be stopped."
    exit 0
}

if ($SafeKill) {
    $safeRecords = @($records | Where-Object { $_.SafeDebug })
    if ($safeRecords.Count -eq 0) {
        Write-Host "No known STM32 debug process found on target ports."
        exit 0
    }

    Stop-UniqueProcesses -Records $safeRecords
    exit 0
}

Write-Host ""
Write-Host "Options:"
Write-Host "  S       Stop known STM32 debug processes only"
Write-Host "  number  Stop selected row, for example: 1 or 1,3"
Write-Host "  Q       Quit without stopping anything"
$choice = Read-Host "Choose action"

if ([string]::IsNullOrWhiteSpace($choice) -or ($choice -ieq "Q")) {
    Write-Host "No process stopped."
    exit 0
}

if ($choice -ieq "S") {
    $safeRecords = @($records | Where-Object { $_.SafeDebug })
    if ($safeRecords.Count -eq 0) {
        Write-Host "No known STM32 debug process found on target ports."
        exit 0
    }

    Stop-UniqueProcesses -Records $safeRecords
    exit 0
}

$selectedIndexes = New-Object System.Collections.Generic.List[int]
foreach ($part in ($choice -split ",")) {
    $trimmed = $part.Trim()
    if ($trimmed -match "^\d+$") {
        $selectedIndexes.Add([int]$trimmed)
    } else {
        Write-Warning "Invalid selection: $trimmed"
    }
}

if ($selectedIndexes.Count -eq 0) {
    Write-Host "No valid row selected."
    exit 1
}

$selectedRecords = @($records | Where-Object { $selectedIndexes -contains $_.Index })
if ($selectedRecords.Count -eq 0) {
    Write-Host "Selected row was not found."
    exit 1
}

Stop-UniqueProcesses -Records $selectedRecords -RequireExtraConfirmForUnknown
