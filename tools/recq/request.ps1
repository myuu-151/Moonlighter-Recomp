# Asks the recording queue for an oracle recording and waits for it (tools/recq/README.md).
#
# Submit and wait (from a lane's worktree; the runner builds that worktree's oracle):
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\recq\request.ps1 butler-1 `
#       -Worktree C:\Users\legen\Documents\ml-wt\butler -OracleArgs '-oracle-enemy=GolemButler' `
#       -Expects enemy_GolemButler.jsonl -Timeout 900
# Submit only (-NoWait), then wait in pieces that fit a tool call (exit 124 = not finished yet):
#   ... request.ps1 butler-1 -Wait -WaitTimeoutSec 540
# Withdraw a request that has not started:   ... request.ps1 butler-1 -Cancel
#
# Exit codes: 0 recorded (status ok), 1 failed (see the reason), 2 refused (invalid request, id
# in use), 3 no such request, 124 still waiting when -WaitTimeoutSec ran out.
param(
    [Parameter(Position = 0)][string]$Id = '',
    [string]$OracleArgs = '',
    [string[]]$Expects = @(),
    [string]$Worktree = '',
    [string]$Branch = '',
    [ValidateSet('', 'worktree', 'main')][string]$Oracle = '',
    [int]$Timeout = 0,
    [string]$Done = '',
    [int]$Retries = -1,
    [switch]$Promote,
    [string]$Note = '',
    [switch]$Replace,
    [switch]$NoWait,
    [switch]$Wait,
    [switch]$Cancel,
    [int]$WaitTimeoutSec = 0,
    [int]$PollSec = 5,
    [switch]$Json,
    [string]$Config = ''
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'recq.psm1') -Force -DisableNameChecking
$cfg = Read-RecqConfig $Config
Set-RecqConfig $cfg

# (With -Json only result.json goes to stdout; the progress lines go to stderr.)
function Say([string]$msg) { if ($Json) { [Console]::Error.WriteLine($msg) } else { Write-Output $msg } }
function Fail([int]$code, [string]$msg) { Say "recq: $msg"; exit $code }

if (-not $Id) { Fail 2 'give the request an id (letters, digits, . _ -), e.g. butler-1' }
if ($Id -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$') { Fail 2 "id '$Id' must be letters, digits, '.', '_' or '-' (at most 64)" }
if (-not (Test-Path -LiteralPath $cfg.QueueDir) -and ($Wait -or $Cancel)) { Fail 3 "no queue folder at $($cfg.QueueDir), so no request $Id" }
Initialize-RecqQueue   # (a request may come before the first runner: it waits in requests/)
$reqPath = QPath "requests/$Id.json"
$runPath = QPath "running/$Id.json"
$doneDir = QPath "done/$Id"
$resultPath = Join-Path $doneDir 'result.json'

if ($Cancel) {
    if (Test-Path -LiteralPath $reqPath) {
        $to = QPath ("cancelled/{0}-{1}.json" -f $Id, (Get-Date).ToString('yyyyMMdd-HHmmss'))
        try { Move-Item -LiteralPath $reqPath -Destination $to -ErrorAction Stop }
        catch { Fail 2 "could not withdraw $Id (did the runner just take it?): $($_.Exception.Message)" }
        Fail 0 "$Id withdrawn (kept as $to)"
    }
    if (Test-Path -LiteralPath $runPath) { Fail 2 "$Id is running; it can't be withdrawn now (it ends by itself; the integrator can stop the runner)" }
    Fail 3 "no pending request $Id"
}

if (-not $Wait) {
    # Submit.
    if (Test-Path -LiteralPath $reqPath) { Fail 2 "$Id is already queued ($reqPath); wait for it with -Wait or withdraw it with -Cancel" }
    if (Test-Path -LiteralPath $runPath) { Fail 2 "$Id is running now; wait for it with -Wait" }
    if (Test-Path -LiteralPath $doneDir) {
        if (-not $Replace) { Fail 2 "$Id was used before (done\$Id); pick a new id (e.g. $Id-2) or pass -Replace to move the old result aside" }
        $old = New-Dir (QPath 'done/_old')
        $to = Join-Path $old ("$Id-" + (Get-Date).ToString('yyyyMMdd-HHmmss'))
        Move-Item -LiteralPath $doneDir -Destination $to
        Say "recq: the earlier done\$Id moved to $to"
    }
    if ($Worktree) {
        if ($Worktree -match '^/([a-zA-Z])(/.*)?$') { $Worktree = "$($Matches[1]):$(if ($Matches[2]) { $Matches[2] } else { '/' })" }   # (Git Bash's /c/...)
        $Worktree = [IO.Path]::GetFullPath($Worktree)
        $top = Invoke-Native $cfg.Git @('-C', $Worktree, 'rev-parse', '--show-toplevel') 60
        if ($top.Code -ne 0) { Fail 2 "$Worktree is not in a git worktree: $($top.Err.Trim())" }
        $Worktree = [IO.Path]::GetFullPath($top.Out.Trim())
        if (-not $Branch) {
            $b = Invoke-Native $cfg.Git @('-C', $Worktree, 'rev-parse', '--abbrev-ref', 'HEAD') 60
            if ($b.Code -eq 0) { $Branch = $b.Out.Trim() }
        }
    }
    $raw = [ordered]@{ id = $Id }
    if ($Branch) { $raw.branch = $Branch }
    if ($Worktree) { $raw.worktree = $Worktree }
    if ($Oracle) { $raw.oracle = $Oracle }
    $raw.args = $OracleArgs
    $raw.expects = [object[]]@($Expects | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    if ($Done) { $raw.done = $Done }
    if ($Timeout -gt 0) { $raw.timeout = $Timeout }
    if ($Retries -ge 0) { $raw.retries = $Retries }
    if ($Promote) { $raw.promote = $true }
    if ($Note) { $raw.note = $Note }
    $raw.submitted = Get-Now
    $raw.submittedBy = "$env:USERNAME@$env:COMPUTERNAME $(if ($Worktree) { Split-Path -Leaf $Worktree } else { (Get-Location).Path })"
    $obj = ($raw | ConvertTo-Json -Depth 5 | ConvertFrom-Json)
    $v = Test-RecqRequest $obj $Id
    if (-not $v.ok) { Fail 2 ("invalid request: " + ($v.errors -join '; ')) }
    Write-JsonFile $reqPath $raw
    Say "recq: queued $Id ($($v.request.args); expects $($v.request.expects -join ', '); done-marker $($v.request.done); oracle $($v.request.oracle)$(if ($v.request.oracle -eq 'worktree') { " from $Worktree" }); timeout $($v.request.timeout) s)"
    if (-not (Test-RunnerAlive)) { Say 'recq: WARNING: no runner is running; the request waits until the integrator starts one (tools\recq\start.ps1)' }
    if ($NoWait) { Say "recq: not waiting; later: request.ps1 $Id -Wait"; exit 0 }
} elseif (-not (Test-Path -LiteralPath $reqPath) -and -not (Test-Path -LiteralPath $runPath) -and -not (Test-Path -LiteralPath $resultPath)) {
    Fail 3 "no request $Id (not queued, not running, no done\$Id\result.json)"
}

# Wait for done/<id>/result.json.
$t0 = Get-Date
$lastLine = ''
while (-not (Test-Path -LiteralPath $resultPath)) {
    $line = ''
    if (Test-Path -LiteralPath $reqPath) {
        $q = @(Get-PendingRequests | ForEach-Object { [IO.Path]::GetFileNameWithoutExtension($_.Name) })
        $pos = [Array]::IndexOf([string[]]$q, $Id) + 1
        $line = "queued$(if ($pos -gt 0) { ", position $pos of $($q.Count)" })"
        if (@(Get-ChildItem -LiteralPath (QPath 'running') -Filter '*.json' -File -ErrorAction SilentlyContinue).Count) { $line += ' (another request is running)' }
    } elseif (Test-Path -LiteralPath $runPath) {
        $line = 'running'
        try {
            $c = Read-JsonFile (QPath 'state/current.json')
            if ($c.id -eq $Id) {
                $n = @(Get-Prop $c 'attempts' @()).Count
                if ($c.current) { $line += ": attempt $($c.current.n)" } elseif ($n) { $line += ": $n attempt(s) done" } else { $line += ': building / preparing' }
            }
        } catch { }
    }
    if (-not (Test-RunnerAlive)) { $line += ' [no runner is running]' }
    if ($line -and $line -ne $lastLine) { Say "recq: $Id $line"; $lastLine = $line }
    if ($WaitTimeoutSec -gt 0 -and ((Get-Date) - $t0).TotalSeconds -ge $WaitTimeoutSec) {
        Say "recq: $Id not finished after $WaitTimeoutSec s; wait again with: request.ps1 $Id -Wait"
        exit 124
    }
    Start-Sleep -Seconds ([Math]::Max(1, $PollSec))
}

$r = Read-JsonFile $resultPath
if ($Json) { [IO.File]::ReadAllText($resultPath) }
else {
    Say "recq: $Id $($r.status)$(if ($r.status -ne 'ok') { " ($($r.reason)): $($r.detail)" }) after $(@($r.attempts).Count) attempt(s)"
    foreach ($a in @($r.attempts)) { if ($a) { Write-Output ("  attempt {0}: {1} - {2}" -f $a.n, $a.reason, $a.detail) } }
    foreach ($f in @($r.files)) { if ($f) { Write-Output ("  {0}  ({1} bytes, sha256 {2})" -f (Join-Path $doneDir $f.name), $f.bytes, $f.sha256.Substring(0, 16)) } }
    if (@($r.otherFiles).Count -and $r.otherFiles[0]) { Write-Output "  also written: $(@($r.otherFiles) -join ', ') (in $(Join-Path $doneDir 'other'))" }
    if (@($r.promoted).Count -and $r.promoted[0]) { Write-Output "  promoted into $($cfg.OracleOutDir): $(@($r.promoted) -join ', ')" }
    Write-Output "  result: $resultPath"
}
exit $(if ($r.status -eq 'ok') { 0 } else { 1 })
