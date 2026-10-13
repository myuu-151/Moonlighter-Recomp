# Starts the recording queue's runner in the background (tools/recq/README.md):
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\recq\start.ps1 [-Config <json>]
#
# The runner gets no window, Idle priority, and is created through WMI so it is nobody's child: it
# keeps running when the shell (or the Claude session) that started it ends. Does nothing when a
# runner already holds the queue. Removes a STOP file left by `status.ps1 -Stop`.
param(
    [string]$Config = '',
    [int]$WaitSec = 20
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'recq.psm1') -Force -DisableNameChecking
if ($Config) { $Config = [IO.Path]::GetFullPath($Config) }
$cfg = Read-RecqConfig $Config
Set-RecqConfig $cfg
Initialize-RecqQueue

if (Test-RunnerAlive) {
    $hb = $null
    try { $hb = Read-JsonFile (QPath 'state/runner.json') } catch { }
    Write-Host "recq: a runner is already running$(if ($hb) { " (pid $($hb.pid): $($hb.activity))" })"
    exit 0
}
Remove-Item -LiteralPath (QPath 'STOP') -Force -ErrorAction SilentlyContinue

$runner = Join-Path $PSScriptRoot 'runner.ps1'
$argList = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $runner)
if ($Config) { $argList += @('-Config', $Config) }
$cmd = (ConvertTo-ArgString $cfg.PowerShellExe) + ' ' + (($argList | ForEach-Object { ConvertTo-ArgString $_ }) -join ' ')

$started = Get-Date
$runnerPid = $null
try {
    $startup = New-CimInstance -ClassName Win32_ProcessStartup -ClientOnly -Property @{ ShowWindow = [uint16]0; PriorityClass = [uint32]64 }
    $r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cmd; CurrentDirectory = $PSScriptRoot; ProcessStartupInformation = $startup }
    if ($r.ReturnValue -ne 0) { throw "Win32_Process.Create returned $($r.ReturnValue)" }
    $runnerPid = [int]$r.ProcessId
} catch {
    Write-Host "recq: WMI start failed ($($_.Exception.Message)); starting it as a hidden child instead"
    $p = Start-Process -FilePath $cfg.PowerShellExe -ArgumentList (($argList | ForEach-Object { ConvertTo-ArgString $_ }) -join ' ') -WindowStyle Hidden -PassThru
    $runnerPid = $p.Id
}

# Wait for its first heartbeat.
$deadline = $started.AddSeconds($WaitSec)
while ((Get-Date) -lt $deadline) {
    try {
        $hb = Read-JsonFile (QPath 'state/runner.json')
        if ($hb.pid -eq $runnerPid -and (ConvertFrom-IsoTime $hb.heartbeat) -ge $started.AddSeconds(-1)) {
            Write-Host "recq: runner started (pid $runnerPid); queue $($cfg.QueueDir); log $(QPath 'state/runner.log')"
            exit 0
        }
    } catch { }
    if (-not (Get-Process -Id $runnerPid -ErrorAction SilentlyContinue)) { break }
    Start-Sleep -Milliseconds 250
}
Write-Host "recq: the runner (pid $runnerPid) did not report in $WaitSec s; see $(QPath 'state/runner.log')"
exit 1
