# The recording queue at a glance, and its control files (tools/recq/README.md).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\recq\status.ps1 [-Json] [-Recent 10]
#   ... status.ps1 -Pause     the runner finishes the current request, then takes no new one
#   ... status.ps1 -Resume    removes PAUSE (and STOP)
#   ... status.ps1 -Stop      the runner exits after the current attempt (a request in the middle of
#                             its retries stays in running/ and goes on when the runner starts again)
param(
    [string]$Config = '',
    [switch]$Json,
    [int]$Recent = 10,
    [switch]$Pause,
    [switch]$Resume,
    [switch]$Stop
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'recq.psm1') -Force -DisableNameChecking
$cfg = Read-RecqConfig $Config
Set-RecqConfig $cfg
Initialize-RecqQueue

if ($Pause) { [IO.File]::WriteAllText((QPath 'PAUSE'), (Get-Now)); Write-Host 'recq: PAUSE written (the runner takes no new request until -Resume)' }
if ($Resume) {
    Remove-Item -LiteralPath (QPath 'PAUSE'), (QPath 'STOP') -Force -ErrorAction SilentlyContinue
    Write-Host 'recq: PAUSE and STOP removed'
}
if ($Stop) { [IO.File]::WriteAllText((QPath 'STOP'), (Get-Now)); Write-Host 'recq: STOP written (the runner exits after the current attempt)' }

$alive = Test-RunnerAlive
$hb = $null
try { $hb = Read-JsonFile (QPath 'state/runner.json') } catch { }
$hbAge = $null
if ($hb) { try { $hbAge = [int]((Get-Date) - (ConvertFrom-IsoTime $hb.heartbeat)).TotalSeconds } catch { } }

$cur = $null
try { if (Test-Path -LiteralPath (QPath 'state/current.json')) { $cur = Read-JsonFile (QPath 'state/current.json') } } catch { }
$swap = $null
try { if (Test-Path -LiteralPath (QPath 'state/swap.json')) { $swap = Read-JsonFile (QPath 'state/swap.json') } } catch { }

$unexpected = @(Get-ChildItem -LiteralPath (QPath 'main-plugin') -Filter 'unexpected-*.dll' -File -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
$pendingFiles = @(Get-ChildItem -LiteralPath (QPath 'requests') -Filter '*.json' -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -like '*.json' })
$saveAge = $cfg.MinRequestAgeSec
$cfg.MinRequestAgeSec = 0
$ordered = @(Get-PendingRequests)
$cfg.MinRequestAgeSec = $saveAge
$pending = @($ordered | ForEach-Object {
        $r = $null; try { $r = Read-JsonFile $_.FullName } catch { }
        [ordered]@{ id = [IO.Path]::GetFileNameWithoutExtension($_.Name); args = (Get-Prop $r 'args' $null); submitted = (Get-Prop $r 'submitted' $_.LastWriteTime.ToString('o')); submittedBy = (Get-Prop $r 'submittedBy' $null) }
    })
$running = @(Get-ChildItem -LiteralPath (QPath 'running') -Filter '*.json' -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -like '*.json' } | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_.Name) })
$results = @(Get-ChildItem -LiteralPath (QPath 'done') -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne '_old' } |
        ForEach-Object { $rp = Join-Path $_.FullName 'result.json'; if (Test-Path -LiteralPath $rp) { Get-Item -LiteralPath $rp } } |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First $Recent | ForEach-Object {
            $r = $null; try { $r = Read-JsonFile $_.FullName } catch { }
            [ordered]@{ id = $_.Directory.Name; status = (Get-Prop $r 'status' '?'); reason = (Get-Prop $r 'reason' $null); detail = (Get-Prop $r 'detail' $null); finished = (Get-Prop $r 'finished' $null); attempts = @(Get-Prop $r 'attempts' @()).Count }
        })

$st = [ordered]@{
    queue      = $cfg.QueueDir
    runner     = [ordered]@{ alive = $alive; pid = (Get-Prop $hb 'pid' $null); heartbeatAgeSec = $hbAge; activity = (Get-Prop $hb 'activity' $null) }
    paused     = (Test-Path -LiteralPath (QPath 'PAUSE'))
    stopping   = (Test-Path -LiteralPath (QPath 'STOP'))
    unexpectedPlugins = [object[]]$unexpected
    pluginSwap = $(if ($swap) { [ordered]@{ id = $swap.id; phase = $swap.phase; since = $swap.time } } else { $null })
    running    = $(if ($running.Count) { [ordered]@{ id = $running[0]; attempts = @(Get-Prop $cur 'attempts' @()).Count; current = (Get-Prop $cur 'current' $null) } } else { $null })
    pending    = [object[]]$pending
    recent     = [object[]]$results
}

if ($Json) { $st | ConvertTo-Json -Depth 8; exit 0 }

$runnerLine = $(if ($alive) { "running (pid $($st.runner.pid), heartbeat $hbAge s ago): $($st.runner.activity)" } else { 'NOT RUNNING' + $(if ($hb) { " (last: pid $($hb.pid), $hbAge s ago: $($hb.activity))" }) + ' -- start it with tools\recq\start.ps1' })
Write-Output "queue:   $($cfg.QueueDir)"
Write-Output "runner:  $runnerLine"
if ($st.paused) { Write-Output 'PAUSED   (status.ps1 -Resume)' }
if ($st.stopping) { Write-Output 'STOPPING (STOP file; status.ps1 -Resume removes it)' }
if ($swap) { Write-Output "plugin:  $($swap.phase) guard for $($swap.id) since $($swap.time) (main's plugin is in $(QPath 'main-plugin'))" }
foreach ($u in $unexpected) { Write-Output "WARNING: a plugin that was neither main's nor a lane's build was found in the game during a swap and put aside: $u (main's went back; see the runner log)" }
if ($st.running) {
    $c = $st.running.current
    Write-Output ("running: {0} ({1} attempt(s) done{2})" -f $st.running.id, $st.running.attempts, $(if ($c) { "; attempt $($c.n) since $($c.started)" } else { '' }))
}
Write-Output "pending: $($pending.Count)$(if ($pending.Count) { ' (oldest first)' })"
foreach ($p in $pending) { Write-Output ("  {0,-28} {1}  {2}" -f $p.id, $p.submitted, $p.args) }
Write-Output "recent results:"
foreach ($r in $results) { Write-Output ("  {0,-28} {1,-7} {2,-16} {3}" -f $r.id, $r.status, $r.reason, $r.finished) }
