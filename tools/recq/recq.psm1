# Unattended recording queue: the runner's machinery (docs/recording_queue_plan.md, tools/recq/README.md).
#
# Windows PowerShell 5.1. Everything that touches the game copy goes through this module, one
# request at a time: the oracle build of a request's worktree, the plugin swap, Steam, the game run
# (through tools/oracle_launch.ps1, on a hidden desktop), collecting the recorded files, backups.
# Every path comes from the config, so the tests (tools/recq/tests/run_tests.ps1) drive the same
# code against a fake game, a fake Steam and throwaway oracle projects.
#
# Queue folder layout (QueueDir):
#   requests/<id>.json    pending requests, written by agents (request.ps1)
#   running/<id>.json     the request being run (moved here unchanged)
#   done/<id>/            its result: result.json last, the recorded files, LogOutput.log, build.log,
#                         attempts/<n>/ (each attempt's logs; a failed attempt's files), other/
#   builds/<id>/          the request's oracle build (src/ copy without deploy targets, out/)
#   main-plugin/          main's MoonlighterOracle.dll while a build or a swap is in progress
#   stash/current/        a copy of data/oracle during a run (main's recordings go back after it)
#   state/                runner.json (heartbeat), runner.log, runner.lock, current.json (the running
#                         request's attempts), swap.json (plugin guard), stash.json (data/oracle copy)
#   rejected/, cancelled/ request files the runner refused / agents withdrew
#   PAUSE, STOP           control files (status.ps1 -Pause / -Resume / -Stop)

$ErrorActionPreference = 'Stop'
$script:Utf8 = New-Object System.Text.UTF8Encoding($false)
$script:Cfg = $null
$script:LockStream = $null
$script:LastHeartbeat = [datetime]::MinValue
$script:Activity = ''
$script:RunnerVersion = 2
$script:ErrorCounts = @{}
$script:ModuleDir = $PSScriptRoot

# Settings that point at the real game: a config file must give each one itself, so a test config
# can never fall back onto the real game copy, its plugin or main's recordings.
$script:PathKeys = @('QueueDir', 'GameDir', 'GameExe', 'OracleOutDir', 'BackupDir', 'SteamExe', 'SteamLog')

# ---------------------------------------------------------------------------------------------
# Config

function Get-RecqDefaultConfig {
    [ordered]@{
        QueueDir               = 'C:/Users/legen/Documents/moonlighter-recq'
        GameDir                = 'C:/Users/legen/Documents/moonlighter/Moonlighter'
        GameExe                = 'C:/Users/legen/Documents/moonlighter/Moonlighter/Moonlighter.exe'
        OracleOutDir           = 'C:/Users/legen/Documents/moonlighter/data/oracle'
        BackupDir              = 'C:/Users/legen/Documents/moonlighter-oracle-backup'
        PlayerLog              = (Join-Path $env:USERPROFILE 'AppData/LocalLow/11BitStudios/Moonlighter/Player.log')
        # tools/oracle_launch.ps1: the game on a hidden desktop, windowed 640x360, with a timeout.
        Launcher               = (Join-Path (Split-Path -Parent $script:ModuleDir) 'oracle_launch.ps1')
        PowerShellExe          = (Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe')
        DesktopName            = 'MoonlighterOracle'
        GamePriority           = 'Normal'      # (Idle priority caused frame hitches that broke recordings)
        PluginRelPath          = 'BepInEx/plugins/MoonlighterOracle.dll'
        BepInExLogRelPath      = 'BepInEx/LogOutput.log'
        RefDirs                = @('BepInEx/core', 'Moonlighter_Data/Managed')
        SteamCheck             = $true
        SteamExe               = 'C:/Program Files (x86)/Steam/steam.exe'
        SteamArgs              = '-silent'
        SteamLog               = 'C:/Program Files (x86)/Steam/logs/connection_log.txt'
        SteamProcessName       = 'steam'
        # The log goes Connecting -> Connected -> Logging On -> Logged On within a second; a bare
        # "Connected" also shows up in failed logons ('Try another CM'), so Logged On is the test.
        SteamConnectedStates   = @('Logged On')
        SteamConnectTimeoutSec = 300
        SteamSettleSec         = 15
        Git                    = 'C:/Program Files/Git/cmd/git.exe'
        Dotnet                 = 'dotnet'
        OracleProjectRelPath   = 'oracle/MoonlighterOracle'
        BuildTimeoutSec        = 900
        DefaultTimeoutSec      = 900
        MaxTimeoutSec          = 7200
        MaxRetries             = 2
        StartupWindowSec       = 30
        DoneGraceSec           = 60
        LauncherGraceSec       = 60
        RetryDelaySec          = 30
        PollSec                = 15
        RunPollMs              = 1000
        HeartbeatSec           = 30
        RunnerPriority         = 'Idle'
        FailedMarkers          = @('FAILED')
        # 'auto': an oracle that knows -oracle-out=<dir> writes each run into its own folder
        # (out/<id>/<n>) and main's data/oracle is never touched; an older one writes into
        # data/oracle, which is copied before the run and put back after it. 'never': always the latter.
        RedirectOutput         = 'auto'
        MinRequestAgeSec       = 2
        MaxRunnerErrors        = 3
    }
}

# Built-in defaults, or a JSON file. A file must name every path itself (or say
# "UseDefaultPaths": true to take the real ones and change only other settings).
function Read-RecqConfig([string]$Path) {
    $cfg = Get-RecqDefaultConfig
    if ($Path) {
        $file = Read-JsonFile $Path
        $useDefaults = [bool](Get-Prop $file 'UseDefaultPaths' $false)
        if (-not $useDefaults) {
            foreach ($k in $script:PathKeys) {
                if (-not $file.PSObject.Properties[$k]) { throw "config $Path must set $k (paths never fall back to the real game's unless UseDefaultPaths is true)" }
            }
        }
        foreach ($p in $file.PSObject.Properties) {
            if ($p.Name -eq 'UseDefaultPaths' -or $p.Name -like '_*') { continue }
            if (-not $cfg.Contains($p.Name)) { throw "config $Path has an unknown key: $($p.Name)" }
            $cfg[$p.Name] = $p.Value
        }
    }
    foreach ($k in @('QueueDir', 'OracleOutDir', 'BackupDir')) { $cfg[$k] = [IO.Path]::GetFullPath($cfg[$k]) }
    $cfg['GameDir'] = [IO.Path]::GetFullPath($cfg.GameDir)
    $cfg['GameExe'] = [IO.Path]::GetFullPath($cfg.GameExe)
    $cfg['PluginPath'] = [IO.Path]::GetFullPath((Join-Path $cfg.GameDir $cfg.PluginRelPath))
    $cfg['BepInExLog'] = Join-Path $cfg.GameDir $cfg.BepInExLogRelPath
    $cfg['GameProcessName'] = [IO.Path]::GetFileNameWithoutExtension($cfg.GameExe)
    $cfg
}

function Set-RecqConfig($cfg) { $script:Cfg = $cfg }

function QPath([string]$rel) { [IO.Path]::GetFullPath((Join-Path $script:Cfg.QueueDir $rel)) }

# ---------------------------------------------------------------------------------------------
# Small helpers

function Get-Prop($obj, [string]$name, $default = $null) {
    if ($null -eq $obj) { return $default }
    if ($obj -is [System.Collections.IDictionary]) {
        if ($obj.Contains($name)) { return $obj[$name] }
        return $default
    }
    if ($obj.PSObject.Properties[$name]) { return $obj.$name }
    $default
}

# A JSON object (PSCustomObject) as an ordered dictionary, one level deep.
function ConvertTo-Ordered($obj) {
    $d = [ordered]@{}
    if ($null -eq $obj) { return $d }
    if ($obj -is [System.Collections.IDictionary]) { foreach ($k in $obj.Keys) { $d[$k] = $obj[$k] }; return $d }
    foreach ($p in $obj.PSObject.Properties) { $d[$p.Name] = $p.Value }
    $d
}

function Get-Now { (Get-Date).ToString('o') }

function ConvertFrom-IsoTime([string]$s) {
    [datetime]::Parse($s, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind)
}

function Read-JsonFile([string]$Path) {
    for ($i = 0; ; $i++) {
        try { return ([IO.File]::ReadAllText($Path) | ConvertFrom-Json) }
        catch [System.IO.FileNotFoundException], [System.IO.DirectoryNotFoundException] { throw }
        catch [System.IO.IOException] { if ($i -ge 20) { throw }; Start-Sleep -Milliseconds 100 }
        catch [System.UnauthorizedAccessException] { if ($i -ge 20) { throw }; Start-Sleep -Milliseconds 100 }   # (while a writer replaces it)
    }
}

# Writes a temp file and moves it over the target: readers never see half a file.
function Write-JsonFile([string]$Path, $obj) {
    $json = $obj | ConvertTo-Json -Depth 20
    $tmp = "$Path.tmp"
    [IO.File]::WriteAllText($tmp, $json, $script:Utf8)
    for ($i = 0; ; $i++) {
        try {
            if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($tmp, $Path, [NullString]::Value) }
            else { [IO.File]::Move($tmp, $Path) }
            return
        } catch [System.IO.IOException] { if ($i -ge 20) { throw }; Start-Sleep -Milliseconds 100 }
        catch [System.UnauthorizedAccessException] { if ($i -ge 20) { throw }; Start-Sleep -Milliseconds 100 }
    }
}

function Get-Sha256([string]$Path) {
    if (-not $Path -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function New-Dir([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { [void](New-Item -ItemType Directory -Path $Path -Force) }
    $Path
}

# Copies through a temp file next to the target and swaps it in, so the target is always either
# the old file or the whole new one (a crash never leaves half a plugin or half a recording).
function Copy-FileAtomic([string]$Src, [string]$Dst) {
    $tmp = "$Dst.recq-tmp"
    [IO.File]::Copy($Src, $tmp, $true)
    if (Test-Path -LiteralPath $Dst) { [IO.File]::Replace($tmp, $Dst, [NullString]::Value) }
    else { [IO.File]::Move($tmp, $Dst) }
}

function Write-RecqLog([string]$msg) {
    $line = '[{0}] {1}' -f (Get-Date).ToString('yyyy-MM-dd HH:mm:ss'), $msg
    Write-Host $line
    if ($script:Cfg) {
        try { [IO.File]::AppendAllText((QPath 'state/runner.log'), $line + "`r`n", $script:Utf8) } catch { }
    }
}

function Write-Heartbeat([string]$activity, [switch]$Force) {
    if ($activity) { $script:Activity = $activity }
    $now = Get-Date
    if (-not $Force -and ($now - $script:LastHeartbeat).TotalSeconds -lt $script:Cfg.HeartbeatSec) { return }
    $script:LastHeartbeat = $now
    $hb = [ordered]@{
        pid       = $PID
        version   = $script:RunnerVersion
        heartbeat = $now.ToString('o')
        activity  = $script:Activity
        paused    = (Test-Path -LiteralPath (QPath 'PAUSE'))
        config    = [ordered]@{ HeartbeatSec = $script:Cfg.HeartbeatSec; PollSec = $script:Cfg.PollSec }
    }
    try { Write-JsonFile (QPath 'state/runner.json') $hb } catch { }
}

# Quotes one argument the way CommandLineToArgvW reads it back.
function ConvertTo-ArgString([string]$a) {
    if ($a -ne '' -and $a -notmatch '[\s"]') { return $a }
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append('"')
    $bs = 0
    foreach ($c in $a.ToCharArray()) {
        if ($c -eq '\') { $bs++; continue }
        if ($c -eq '"') { [void]$sb.Append('\' * (2 * $bs + 1)); [void]$sb.Append('"'); $bs = 0; continue }
        if ($bs) { [void]$sb.Append('\' * $bs); $bs = 0 }
        [void]$sb.Append($c)
    }
    if ($bs) { [void]$sb.Append('\' * (2 * $bs)) }
    [void]$sb.Append('"')
    $sb.ToString()
}

# Starts a console program without a window, its output captured (PowerShell 5.1 turns a native
# program's stderr into errors under ErrorActionPreference Stop, so not through `&`).
function Start-Native([string]$Exe, [string[]]$ArgList, [string]$WorkDir = '') {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Exe
    $psi.Arguments = (($ArgList | ForEach-Object { ConvertTo-ArgString $_ }) -join ' ')
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    if ($WorkDir) { $psi.WorkingDirectory = $WorkDir }
    $p = [System.Diagnostics.Process]::Start($psi)
    [pscustomobject]@{ Process = $p; Out = $p.StandardOutput.ReadToEndAsync(); Err = $p.StandardError.ReadToEndAsync(); CommandLine = "$Exe $($psi.Arguments)" }
}

function Invoke-Native([string]$Exe, [string[]]$ArgList, [int]$TimeoutSec = 600, [string]$WorkDir = '') {
    $n = Start-Native $Exe $ArgList $WorkDir
    $p = $n.Process
    $timedOut = $false
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
        $timedOut = $true
        Stop-ProcessTree $p.Id
        [void]$p.WaitForExit(15000)
    }
    $p.WaitForExit()
    [pscustomobject]@{ Code = $p.ExitCode; Out = (Get-NativeOutput $n.Out); Err = (Get-NativeOutput $n.Err); TimedOut = $timedOut }
}

# A captured stream once the program has exited. A process it left behind can hold the pipe open
# (a build server, a detached child): then don't wait for that one forever.
function Get-NativeOutput($Task, [int]$WaitMs = 30000) {
    try {
        if ($Task.Wait($WaitMs)) { return $Task.Result }
        return '(output not complete: a process the program started still holds its output pipe)'
    } catch { return "(output unreadable: $($_.Exception.Message))" }
}

function Stop-ProcessTree([int]$ProcessId) {
    try {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = 'taskkill.exe'
        $psi.Arguments = "/PID $ProcessId /T /F"
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $true
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $k = [System.Diagnostics.Process]::Start($psi)
        [void]$k.StandardOutput.ReadToEndAsync(); [void]$k.StandardError.ReadToEndAsync()
        [void]$k.WaitForExit(60000)
    } catch { }
    try { Stop-Process -Id $ProcessId -Force -ErrorAction SilentlyContinue } catch { }
}

# Processes of our game copy (by name, then by path when Windows tells us; no path counts as ours).
function Get-GameProcesses {
    $exe = [IO.Path]::GetFullPath($script:Cfg.GameExe)
    @(Get-Process -Name $script:Cfg.GameProcessName -ErrorAction SilentlyContinue | Where-Object {
            $path = $null
            try { $path = $_.Path } catch { }
            (-not $path) -or ([string]::Equals([IO.Path]::GetFullPath($path), $exe, 'OrdinalIgnoreCase'))
        })
}

# Any process with the game's name, whichever copy: only one may run at a time (Steam).
function Get-AnyGameProcesses { @(Get-Process -Name $script:Cfg.GameProcessName -ErrorAction SilentlyContinue) }

function Stop-OurGame([string]$why) {
    foreach ($g in Get-GameProcesses) { Write-RecqLog "killing game process $($g.Id) ($why)"; Stop-ProcessTree $g.Id }
}

# Whether process $ProcessId is alive and its command line mentions $Needle (a pid kept in
# state/current.json can belong to anybody after a reboot).
function Test-OurProcess([int]$ProcessId, [string]$Needle) {
    try {
        $p = Get-CimInstance Win32_Process -Filter "ProcessId = $ProcessId" -ErrorAction Stop
        return [bool]($p -and $p.CommandLine -and $p.CommandLine.IndexOf($Needle, [StringComparison]::OrdinalIgnoreCase) -ge 0)
    } catch { return $false }
}

# Kills what still builds in the queue's builds/ folder (a dotnet build of a runner that died).
function Stop-QueueBuilds {
    $builds = QPath 'builds'
    $alt = $builds.Replace('\', '/')
    foreach ($p in @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
                $_.ProcessId -ne $PID -and $_.CommandLine -and
                ($_.CommandLine.IndexOf($builds, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or $_.CommandLine.IndexOf($alt, [StringComparison]::OrdinalIgnoreCase) -ge 0) })) {
        Write-RecqLog "recovery: killing an interrupted build process $($p.ProcessId) ($($p.Name))"
        Stop-ProcessTree ([int]$p.ProcessId)
    }
}

# ---------------------------------------------------------------------------------------------
# Requests

$script:IdPattern = '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$'
$script:NamePattern = '^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$'

# The oracle's done-marker for the mode in `args` (OraclePlugin.cs and each mode's Run).
function Get-DoneMarker([string]$ArgLine) {
    $marks = New-Object System.Collections.ArrayList
    foreach ($t in ($ArgLine -split '\s+')) {
        if ($t -match '^-oracle-enemy="?([^:\s"]+)') { [void]$marks.Add("ENEMY_$($Matches[1])_DONE") }
        elseif ($t -match '^-oracle-slime(=|$)') { [void]$marks.Add('SLIME_DONE') }
        elseif ($t -match '^-oracle-tangle(=|$)') { [void]$marks.Add('TANGLE_DONE') }
        elseif ($t -match '^-oracle-walk(=|$)') { [void]$marks.Add('WALK_DONE') }
        elseif ($t -match '^-oracle-polynav(=|$)') { [void]$marks.Add('POLYNAV_DONE') }
        elseif ($t -match '^-oracle-harvest(=|$)') { [void]$marks.Add('HARVEST_DONE') }
        elseif ($t -eq '-oracle-run') { [void]$marks.Add('DONE') }
    }
    if ($marks.Count -eq 1) { return $marks[0] }
    $null
}

function Test-IsNumber($v) { $v -is [int] -or $v -is [long] -or $v -is [double] -or $v -is [decimal] }

# Checks a request and fills in the defaults. Returns @{ ok; errors; request }.
# (request.ps1 runs the same checks before it submits.)
function Test-RecqRequest($raw, [string]$id) {
    $cfg = $script:Cfg
    $errors = New-Object System.Collections.ArrayList
    $rid = [string](Get-Prop $raw 'id' '')
    if ($rid -ne $id) { [void]$errors.Add("id '$rid' does not match the file name '$id'") }
    if ($id -notmatch $script:IdPattern) { [void]$errors.Add("id '$id' must match $($script:IdPattern)") }

    $argLine = ([string](Get-Prop $raw 'args' '')).Trim()
    if ($argLine -eq '') { [void]$errors.Add('args is empty') }
    if ($argLine -match '[\r\n]') { [void]$errors.Add('args has a line break') }
    $tokens = @($argLine -split '\s+' | Where-Object { $_ })
    if ($tokens -contains '-oracle-visible') { [void]$errors.Add('-oracle-visible is not allowed in the queue (game windows stay hidden)') }
    if (-not ($tokens | Where-Object { $_ -like '-oracle-*' -and $_ -ne '-oracle-quit' -and $_ -ne '-oracle-visible' })) {
        [void]$errors.Add('args has no -oracle-<mode>')
    }
    foreach ($t in $tokens) {
        if ($t -match '^-screen-') { [void]$errors.Add("args must not set $t (the launcher sets the window)") }
        if ($t -match '^"?-oracle-out(=|$)') { [void]$errors.Add("args must not set -oracle-out (the runner gives each run its folder)") }
    }

    $expects = @(Get-Prop $raw 'expects' @())
    if ($expects.Count -eq 0) { [void]$errors.Add('expects is empty') }
    foreach ($e in $expects) {
        if (-not ($e -is [string]) -or $e -notmatch $script:NamePattern) { [void]$errors.Add("expects entry '$e' must be a plain file name in data/oracle") }
    }
    if (@($expects | Select-Object -Unique).Count -ne $expects.Count) { [void]$errors.Add('expects lists a file twice') }

    $done = [string](Get-Prop $raw 'done' '')
    if ($done -eq '') { $done = Get-DoneMarker $argLine }
    if (-not $done) { [void]$errors.Add('done is not given and the args do not name exactly one oracle mode to infer it from') }
    elseif ($done -notmatch $script:NamePattern) { [void]$errors.Add("done '$done' must be a plain file name") }
    elseif ($expects -contains $done) { [void]$errors.Add("done '$done' is listed in expects too") }

    $worktree = [string](Get-Prop $raw 'worktree' '')
    $oracle = [string](Get-Prop $raw 'oracle' '')
    if ($oracle -eq '') { $oracle = $(if ($worktree) { 'worktree' } else { 'main' }) }
    if ($oracle -ne 'worktree' -and $oracle -ne 'main') { [void]$errors.Add("oracle must be 'worktree' or 'main', not '$oracle'") }
    if ($oracle -eq 'worktree') {
        if (-not $worktree) { [void]$errors.Add("oracle 'worktree' needs a worktree") }
        else {
            $proj = Join-Path $worktree $cfg.OracleProjectRelPath
            if (-not (Test-Path -LiteralPath $proj -PathType Container)) { [void]$errors.Add("no oracle project at $proj") }
        }
    }

    $timeout = Get-Prop $raw 'timeout' $cfg.DefaultTimeoutSec
    if (-not (Test-IsNumber $timeout)) { [void]$errors.Add('timeout must be a number of seconds'); $timeout = $cfg.DefaultTimeoutSec }
    $timeout = [int][Math]::Ceiling([double]$timeout)
    if ($timeout -le 0 -or $timeout -gt $cfg.MaxTimeoutSec) { [void]$errors.Add("timeout must be in (0, $($cfg.MaxTimeoutSec)] seconds") }

    $retries = Get-Prop $raw 'retries' $cfg.MaxRetries
    if (-not ($retries -is [int] -or $retries -is [long])) { [void]$errors.Add('retries must be an integer'); $retries = $cfg.MaxRetries }
    $retries = [int][Math]::Max(0, [Math]::Min([int]$retries, [int]$cfg.MaxRetries))

    $promote = Get-Prop $raw 'promote' $false
    if (-not ($promote -is [bool])) { [void]$errors.Add('promote must be true or false'); $promote = $false }

    $req = [ordered]@{
        id          = $id
        branch      = [string](Get-Prop $raw 'branch' '')
        worktree    = $worktree
        oracle      = $oracle
        args        = $argLine
        expects     = [object[]]$expects
        done        = $done
        timeout     = $timeout
        retries     = $retries
        promote     = $promote
        submitted   = [string](Get-Prop $raw 'submitted' '')
        submittedBy = [string](Get-Prop $raw 'submittedBy' '')
        note        = [string](Get-Prop $raw 'note' '')
    }
    [pscustomobject]@{ ok = ($errors.Count -eq 0); errors = [object[]]$errors; request = $req }
}

# The request's args for the game, with -oracle-quit (the launcher adds the window settings) and,
# for an oracle that knows it, -oracle-out=<the run's folder>.
function Get-GameArgLine($req, [string]$OutDir = '') {
    $line = $req.args
    if (($req.args -split '\s+') -notcontains '-oracle-quit') { $line += ' -oracle-quit' }
    if ($OutDir) { $line += ' ' + (ConvertTo-ArgString "-oracle-out=$OutDir") }
    $line
}

# Whether an oracle build knows -oracle-out=<dir> (OraclePlugin.cs): its string literal, UTF-16
# in the assembly's #US heap (at an even or an odd offset).
function Test-OracleSupportsOut([string]$Dll) {
    if (-not $Dll -or -not (Test-Path -LiteralPath $Dll -PathType Leaf)) { return $false }
    $bytes = [IO.File]::ReadAllBytes($Dll)
    if ($bytes.Length -lt 2) { return $false }
    foreach ($off in 0, 1) {
        if ([Text.Encoding]::Unicode.GetString($bytes, $off, $bytes.Length - $off).Contains('-oracle-out=')) { return $true }
    }
    $false
}

# Oldest first: the request's own `submitted` time, else the file's. Files younger than
# MinRequestAgeSec wait a round (someone may still be writing one by hand).
function Get-PendingRequests {
    $dir = QPath 'requests'
    $cutoff = (Get-Date).ToUniversalTime().AddSeconds(-[double]$script:Cfg.MinRequestAgeSec)
    $items = foreach ($f in @(Get-ChildItem -LiteralPath $dir -Filter '*.json' -File -ErrorAction SilentlyContinue)) {
        if ($f.Name -notlike '*.json') { continue }   # (-Filter '*.json' also matches *.json.tmp's 8.3 names)
        if ($f.LastWriteTimeUtc -gt $cutoff) { continue }
        $t = $f.LastWriteTimeUtc
        try {
            $s = Get-Prop (Read-JsonFile $f.FullName) 'submitted' ''
            if ($s) { $t = (ConvertFrom-IsoTime $s).ToUniversalTime() }
        } catch { }
        [pscustomobject]@{ File = $f; Time = $t }
    }
    @($items | Sort-Object -Property @{ Expression = 'Time' }, @{ Expression = { $_.File.Name } } | ForEach-Object { $_.File })
}

# ---------------------------------------------------------------------------------------------
# Steam

# The last 128 KB of Steam's connection log (Steam keeps it open).
function Get-SteamLogTail([string]$Path, [int]$Bytes = 131072) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return @() }
    $fs = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]'ReadWrite, Delete')
    try {
        $start = [Math]::Max([long]0, $fs.Length - $Bytes)
        [void]$fs.Seek($start, [IO.SeekOrigin]::Begin)
        $text = (New-Object IO.StreamReader($fs)).ReadToEnd()
    } finally { $fs.Dispose() }
    $lines = @($text -split "`r?`n")
    if ($start -gt 0 -and $lines.Count -gt 1) { $lines = $lines[1..($lines.Count - 1)] }
    $lines
}

# Steam's connection state from its log: the last "[time] [State, n, n]" line (at or after
# `Since` when given, to the second).
function Get-SteamConnectionState([string[]]$Lines, $Since = $null, [string[]]$ConnectedStates = @('Logged On')) {
    $re = '^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)\] \[([A-Za-z][A-Za-z ]*?), -?\d+, -?\d+\]'
    $floor = $null
    if ($Since) { $floor = ([datetime]$Since).AddTicks(-(([datetime]$Since).Ticks % [TimeSpan]::TicksPerSecond)) }
    for ($i = $Lines.Count - 1; $i -ge 0; $i--) {
        $m = [regex]::Match($Lines[$i], $re)
        if (-not $m.Success) { continue }
        $t = [datetime]::ParseExact($m.Groups[1].Value, 'yyyy-MM-dd HH:mm:ss', [Globalization.CultureInfo]::InvariantCulture)
        if ($floor -and $t -lt $floor) { break }
        $state = $m.Groups[2].Value
        return [pscustomobject]@{ State = $state; Time = $t; Connected = ($ConnectedStates -contains $state) }
    }
    [pscustomobject]@{ State = $null; Time = $null; Connected = $false }
}

function Test-SteamRunning { [bool](Get-Process -Name $script:Cfg.SteamProcessName -ErrorAction SilentlyContinue) }

# Starts Steam at Normal priority (the runner is at Idle and a plain start would pass that on),
# detached from the runner: `cmd /c start "" /NORMAL steam.exe -silent`. Through ShellExecute, so
# cmd and Steam inherit none of the runner's handles (a Steam holding the runner's output pipe
# would keep whoever reads it waiting until Steam quits).
function Start-Steam {
    $cfg = $script:Cfg
    $line = 'start "" /NORMAL ' + (ConvertTo-ArgString ([IO.Path]::GetFullPath($cfg.SteamExe))) + ' ' + $cfg.SteamArgs
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = Join-Path $env:SystemRoot 'System32/cmd.exe'
    $psi.Arguments = '/d /c ' + $line
    $psi.UseShellExecute = $true
    $psi.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    $psi.WorkingDirectory = Split-Path -Parent ([IO.Path]::GetFullPath($cfg.SteamExe))
    $p = [System.Diagnostics.Process]::Start($psi)
    [void]$p.WaitForExit(30000)
    if ($p.ExitCode -ne 0) { throw "cmd /c $line exited $($p.ExitCode)" }
}

# Steam running and logged on before a run; started with `steam.exe -silent` when it isn't.
function Confirm-Steam {
    $cfg = $script:Cfg
    $r = [ordered]@{ ok = $false; checked = [bool]$cfg.SteamCheck; wasRunning = $null; started = $false; state = $null; waitedSec = 0; reason = $null }
    if (-not $cfg.SteamCheck) { $r.ok = $true; return $r }
    $t0 = Get-Date
    $since = $null
    $r.wasRunning = Test-SteamRunning
    if (-not $r.wasRunning) {
        Write-RecqLog "Steam is not running: starting $($cfg.SteamExe) $($cfg.SteamArgs)"
        $since = Get-Date
        try { Start-Steam; $r.started = $true }
        catch {
            $r.reason = "could not start Steam: $($_.Exception.Message)"
            Write-RecqLog "Steam: $($r.reason)"
            return $r
        }
    }
    $deadline = $t0.AddSeconds($cfg.SteamConnectTimeoutSec)
    while ($true) {
        $st = Get-SteamConnectionState (Get-SteamLogTail $cfg.SteamLog) $since @($cfg.SteamConnectedStates)
        if (-not $st.State -and -not $since) {
            # (A long session can push its last state line out of the tail: read the whole log;
            # Steam starts a new one at 4 MB.)
            $st = Get-SteamConnectionState (Get-SteamLogTail $cfg.SteamLog 16777216) $null @($cfg.SteamConnectedStates)
        }
        $r.state = $st.State
        if ($st.Connected -and (Test-SteamRunning)) { $r.ok = $true; break }
        if ((Get-Date) -gt $deadline) {
            $running = Test-SteamRunning
            $r.reason = "Steam not logged on after $($cfg.SteamConnectTimeoutSec) s (running: $running, last state: $(if ($st.State) { $st.State } else { 'none' }))"
            break
        }
        Write-Heartbeat "waiting for Steam to log on"
        Start-Sleep -Milliseconds 500
    }
    if ($r.ok -and $r.started -and $cfg.SteamSettleSec -gt 0) { Start-Sleep -Seconds $cfg.SteamSettleSec }
    $r.waitedSec = [Math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
    if ($r.ok) { Write-RecqLog "Steam: logged on ($($r.state))$(if ($r.started) { ', started by the runner' })" }
    else { Write-RecqLog "Steam: $($r.reason)" }
    $r
}

# ---------------------------------------------------------------------------------------------
# Main's plugin: guarded while a build runs and while a request's build is swapped in.
# state/swap.json marks it until main's plugin is back (a restarted runner restores it first).

function Get-PluginBackupPath { Join-Path (QPath 'main-plugin') ([IO.Path]::GetFileName($script:Cfg.PluginPath)) }

# Phase 'build' (nothing swapped in: the build must not touch the game) or 'run' ($Dll swapped in).
function Enter-PluginGuard([string]$Id, [string]$Phase, [string]$Dll = '') {
    $cfg = $script:Cfg
    $marker = QPath 'state/swap.json'
    if (Test-Path -LiteralPath $marker) {
        $x = Exit-PluginGuard
        if (-not $x.ok) { throw "main's plugin could not be restored from an earlier swap" }
    }
    $plugin = $cfg.PluginPath
    [void](New-Dir (QPath 'main-plugin'))
    $backup = Get-PluginBackupPath
    $mainSha = Get-Sha256 $plugin
    if ($mainSha) {
        Copy-FileAtomic $plugin $backup
        if ((Get-Sha256 $backup) -ne $mainSha) { throw "copying main's plugin to $backup did not verify" }
    }
    $swapSha = $(if ($Dll) { Get-Sha256 $Dll } else { $null })
    Write-JsonFile $marker ([ordered]@{ id = $Id; phase = $Phase; time = (Get-Now); plugin = $plugin; mainBackup = $backup; mainSha256 = $mainSha; swappedSha256 = $swapSha })
    if ($Dll) {
        [void](New-Dir (Split-Path -Parent $plugin))
        Copy-FileAtomic $Dll $plugin
        if ((Get-Sha256 $plugin) -ne $swapSha) { throw "copying the build into $plugin did not verify" }
        Write-RecqLog "plugin: $Id's build swapped in ($($swapSha.Substring(0, 12)); main's is $(if ($mainSha) { $mainSha.Substring(0, 12) } else { 'absent' }))"
    }
}

# Puts main's plugin back, always. A plugin that is neither main's nor the swapped build is kept
# aside in main-plugin/unexpected-<time>.dll (and reported), then main's goes back anyway.
# Returns @{ ok; changed (the plugin was not main's); unexpected (the kept file, if any) }.
function Exit-PluginGuard {
    $marker = QPath 'state/swap.json'
    $r = [ordered]@{ ok = $true; changed = $false; unexpected = $null; phase = $null }
    if (-not (Test-Path -LiteralPath $marker)) { return $r }
    $m = Read-JsonFile $marker
    $r.phase = $m.phase
    $plugin = $m.plugin
    $cur = Get-Sha256 $plugin
    $r.changed = ($cur -ne $m.mainSha256)
    if ($r.changed -and $cur -and $cur -ne $m.swappedSha256) {
        $keep = Join-Path (QPath 'main-plugin') ("unexpected-{0}.dll" -f (Get-Date).ToString('yyyyMMdd-HHmmss'))
        try { [IO.File]::Copy($plugin, $keep, $false); $r.unexpected = $keep } catch { }
        Write-RecqLog "plugin: WARNING: $plugin is neither main's nor the swapped build ($($cur.Substring(0, 12))); kept as $keep, main's goes back"
    }
    for ($try = 1; $try -le 20; $try++) {
        try {
            if ($m.mainSha256) {
                if ((Get-Sha256 $m.mainBackup) -ne $m.mainSha256) { throw "the backup of main's plugin ($($m.mainBackup)) is missing or does not verify" }
                if ((Get-Sha256 $plugin) -ne $m.mainSha256) { Copy-FileAtomic $m.mainBackup $plugin }
                if ((Get-Sha256 $plugin) -ne $m.mainSha256) { throw "restored plugin does not verify" }
            } elseif (Test-Path -LiteralPath $plugin) {
                Remove-Item -LiteralPath $plugin -Force
            }
            Remove-Item -LiteralPath "$plugin.recq-tmp" -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $marker -Force
            if ($r.changed) { Write-RecqLog "plugin: main's restored" }
            return $r
        } catch {
            Write-RecqLog "plugin: restore attempt $try failed: $($_.Exception.Message)"
            Stop-OurGame 'it may hold the plugin'
            Start-Sleep -Milliseconds 500
        }
    }
    Write-RecqLog "ERROR: could not restore main's plugin; state/swap.json stays and no run starts until it is back"
    $r.ok = $false
    $r
}

# ---------------------------------------------------------------------------------------------
# Per-request oracle builds

function Get-WorktreeInfo([string]$Worktree) {
    $git = $script:Cfg.Git
    $b = Invoke-Native $git @('-C', $Worktree, 'rev-parse', '--abbrev-ref', 'HEAD') 60
    $c = Invoke-Native $git @('-C', $Worktree, 'rev-parse', 'HEAD') 60
    $s = Invoke-Native $git @('-C', $Worktree, 'status', '--porcelain', '--', $script:Cfg.OracleProjectRelPath) 60
    [ordered]@{
        branch = $(if ($b.Code -eq 0) { $b.Out.Trim() } else { $null })
        commit = $(if ($c.Code -eq 0) { $c.Out.Trim() } else { $null })
        dirty  = $(if ($s.Code -eq 0) { [bool]$s.Out.Trim() } else { $null })
        error  = $(if ($b.Code -ne 0) { ($b.Err + $b.Out).Trim() } else { $null })
    }
}

# A copy of the game's reference assemblies for builds (the csproj's GameDir): a build then never
# sees the real game folder, whatever targets the project has.
function Update-RefMirror {
    $cfg = $script:Cfg
    $mirror = New-Dir (QPath 'refgame')
    foreach ($rel in @($cfg.RefDirs)) {
        $src = Join-Path $cfg.GameDir $rel
        $dst = New-Dir (Join-Path $mirror $rel)
        if (-not (Test-Path -LiteralPath $src -PathType Container)) { continue }
        foreach ($f in @(Get-ChildItem -LiteralPath $src -Filter '*.dll' -File)) {
            $d = Join-Path $dst $f.Name
            $di = Get-Item -LiteralPath $d -ErrorAction SilentlyContinue
            if (-not $di -or $di.Length -ne $f.Length -or $di.LastWriteTimeUtc -ne $f.LastWriteTimeUtc) {
                [IO.File]::Copy($f.FullName, $d, $true)
            }
        }
    }
    $mirror
}

# Removes the targets that copy the plugin into the game (DeployToGame, or any target that
# mentions BepInEx\plugins or GameDir) from a copied csproj. Returns their names.
function Remove-DeployTargets([string]$Csproj) {
    $xml = New-Object System.Xml.XmlDocument
    $xml.PreserveWhitespace = $true
    $xml.Load($Csproj)
    $removed = New-Object System.Collections.ArrayList
    foreach ($t in @($xml.GetElementsByTagName('Target'))) {
        $name = $t.GetAttribute('Name')
        if ($name -eq 'DeployToGame' -or $t.OuterXml -match '(?i)BepInEx[\\/]+plugins|GameDir') {
            [void]$t.ParentNode.RemoveChild($t)
            [void]$removed.Add($name)
        }
    }
    $xml.Save($Csproj)
    [object[]]$removed
}

# dotnet build of the request's worktree's oracle into builds/<id>/ (the project copied to src/
# without its deploy targets, GameDir pointed at the reference mirror, output in out/; -m:1, at
# the runner's Idle priority). Main's plugin is guarded during the build: if anything still
# writes it, it goes back and the build fails.
function Invoke-OracleBuild([string]$Id, [string]$Worktree) {
    $cfg = $script:Cfg
    $dir = QPath "builds/$Id"
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    $srcDir = New-Dir (Join-Path $dir 'src')
    $outDir = New-Dir (Join-Path $dir 'out')
    $log = Join-Path $dir 'build.log'
    $r = [ordered]@{ ok = $false; dll = $null; sha256 = $null; log = $log; removedTargets = [object[]]@(); reason = $null; seconds = 0 }
    $t0 = Get-Date

    $proj = Join-Path $Worktree $cfg.OracleProjectRelPath
    $rc = Invoke-Native 'robocopy.exe' @($proj, $srcDir, '/E', '/XD', 'bin', 'obj', '/R:1', '/W:1', '/NP', '/NFL', '/NDL', '/NJH', '/NJS') 300
    if ($rc.Code -ge 8) { $r.reason = "copying $proj failed (robocopy $($rc.Code)): $($rc.Out)"; return $r }
    $csproj = @(Get-ChildItem -LiteralPath $srcDir -Filter '*.csproj' -File)
    if ($csproj.Count -ne 1) { $r.reason = "expected one .csproj in $proj, found $($csproj.Count)"; return $r }
    $r.removedTargets = [object[]]@(Remove-DeployTargets $csproj[0].FullName)
    $mirror = Update-RefMirror

    $buildArgs = @('build', $csproj[0].FullName, '-c', 'Release', '-o', $outDir, "-p:GameDir=$mirror",
        '-m:1', '-nodeReuse:false', '-p:UseSharedCompilation=false', '-nologo', '-v:m')
    Write-RecqLog "build: $($cfg.Dotnet) $($buildArgs -join ' ')"
    Enter-PluginGuard $Id 'build'
    $b = $null
    try {
        $env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
        $env:DOTNET_NOLOGO = '1'
        $b = Invoke-Native $cfg.Dotnet $buildArgs $cfg.BuildTimeoutSec $srcDir
    } finally {
        $g = Exit-PluginGuard
    }
    [IO.File]::WriteAllText($log, "> $($cfg.Dotnet) $($buildArgs -join ' ')`r`n$($b.Out)`r`n$($b.Err)`r`nexit $($b.Code)$(if ($b.TimedOut) { ' (timed out)' })`r`n", $script:Utf8)
    $r.seconds = [Math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
    if (-not $g.ok) { throw "main's plugin could not be restored after the build of $Id" }
    if ($g.changed) {
        $r.reason = "the build wrote into the game's plugin folder (main's plugin was put back$(if ($g.unexpected) { "; the written file is kept as $($g.unexpected)" }))"
        return $r
    }
    Remove-Item -LiteralPath (Join-Path $srcDir 'obj') -Recurse -Force -ErrorAction SilentlyContinue
    $dll = Join-Path $outDir ([IO.Path]::GetFileName($cfg.PluginPath))
    if ($b.TimedOut) { $r.reason = "build timed out after $($cfg.BuildTimeoutSec) s"; return $r }
    if ($b.Code -ne 0) {
        $errs = @(("$($b.Out)`n$($b.Err)" -split "`r?`n") | Where-Object { $_ -match ': error ' } | Select-Object -Unique -First 5)
        $r.reason = "dotnet build exited $($b.Code)$(if ($errs) { ': ' + ($errs -join ' | ') })"
        return $r
    }
    if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) { $r.reason = "the build made no $([IO.Path]::GetFileName($dll))"; return $r }
    $r.ok = $true
    $r.dll = $dll
    $r.sha256 = Get-Sha256 $dll
    $r
}

# ---------------------------------------------------------------------------------------------
# Main's recordings: data/oracle is copied before each run and put back after it, and what the
# run wrote moves into the request's folder (state/stash.json marks it until it's back).

function Save-OracleSnapshot([string]$Id, [string[]]$TopNames) {
    $cfg = $script:Cfg
    $marker = QPath 'state/stash.json'
    if (Test-Path -LiteralPath $marker) { [void](Restore-OracleSnapshot (QPath "stash-recovered/$((Get-Date).ToString('yyyyMMdd-HHmmss'))")) }
    $stash = QPath 'stash/current'
    if (Test-Path -LiteralPath $stash) { Remove-Item -LiteralPath $stash -Recurse -Force }
    [void](New-Dir $stash)
    [void](New-Dir $cfg.OracleOutDir)
    $files = New-Object System.Collections.ArrayList
    foreach ($f in @(Get-ChildItem -LiteralPath $cfg.OracleOutDir -File)) {
        $copy = Join-Path $stash $f.Name
        [IO.File]::Copy($f.FullName, $copy, $true)
        $sha = Get-Sha256 $copy
        $now = Get-Item -LiteralPath $f.FullName
        if ($now.Length -ne $f.Length -or $now.LastWriteTimeUtc -ne $f.LastWriteTimeUtc -or (Get-Item -LiteralPath $copy).Length -ne $f.Length) {
            throw "data/oracle's $($f.Name) changed while it was being copied"
        }
        [void]$files.Add([ordered]@{ name = $f.Name; bytes = $f.Length; written = $f.LastWriteTimeUtc.Ticks; sha256 = $sha })
    }
    Write-JsonFile $marker ([ordered]@{ id = $Id; time = (Get-Now); dir = $stash; topNames = [object[]]@($TopNames); files = [object[]]$files })
}

# Moves every file the run created or changed into $CollectTo (the expected names and the
# markers at its top, anything else under other/) and puts main's files back as they were.
# Returns the names moved.
function Restore-OracleSnapshot([string]$CollectTo) {
    $cfg = $script:Cfg
    $marker = QPath 'state/stash.json'
    if (-not (Test-Path -LiteralPath $marker)) { return [object[]]@() }
    $m = Read-JsonFile $marker
    $TopNames = @(@(Get-Prop $m 'topNames' @()) | Where-Object { $_ })
    $known = @{}
    foreach ($f in @($m.files)) { if ($f) { $known[$f.name] = $f } }
    $moved = New-Object System.Collections.ArrayList
    $unchanged = @{}
    foreach ($fi in @(Get-ChildItem -LiteralPath $cfg.OracleOutDir -File -ErrorAction SilentlyContinue)) {
        $k = $known[$fi.Name]
        if ($k -and $fi.Length -eq $k.bytes -and $fi.LastWriteTimeUtc.Ticks -eq [long]$k.written) { $unchanged[$fi.Name] = $true; continue }
        $dstDir = $(if ($TopNames -contains $fi.Name) { $CollectTo } else { Join-Path $CollectTo 'other' })
        [void](New-Dir $dstDir)
        if ($k -and $TopNames -notcontains $fi.Name -and $fi.Length -gt [long]$k.bytes -and (Get-PrefixSha256 $fi.FullName ([long]$k.bytes)) -eq $k.sha256) {
            # An append-only log (trace.jsonl, cave_game.jsonl): keep only what the run added,
            # as other/<name>.appended (main's whole file goes back below).
            Copy-FileTail $fi.FullName ([long]$k.bytes) (Join-Path $dstDir "$($fi.Name).appended")
            [void]$moved.Add("$($fi.Name).appended")
            continue
        }
        $dst = Join-Path $dstDir $fi.Name
        if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Force }
        for ($i = 0; ; $i++) {
            try { [IO.File]::Move($fi.FullName, $dst); break }
            catch { if ($i -ge 20) { throw }; Stop-OurGame 'it may hold a recording'; Start-Sleep -Milliseconds 250 }
        }
        [void]$moved.Add($fi.Name)
    }
    foreach ($k in $known.Values) {
        if ($unchanged[$k.name]) { continue }
        $p = Join-Path $cfg.OracleOutDir $k.name
        Copy-FileAtomic (Join-Path $m.dir $k.name) $p
        if ((Get-Sha256 $p) -ne $k.sha256) { throw "main's $($k.name) did not verify after restoring it" }
        # (Its original write time too, so the next snapshot and freshness checks see main's file.)
        [IO.File]::SetLastWriteTimeUtc($p, [datetime]::new([long]$k.written, [DateTimeKind]::Utc))
    }
    Remove-Item -LiteralPath $marker -Force
    Remove-Item -LiteralPath $m.dir -Recurse -Force -ErrorAction SilentlyContinue
    [object[]]$moved
}

# SHA-256 of a file's first $Length bytes ($null when it is shorter).
function Get-PrefixSha256([string]$Path, [long]$Length) {
    $sha = [Security.Cryptography.SHA256]::Create()
    $fs = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $buf = New-Object byte[] 1048576
        $left = $Length
        while ($left -gt 0) {
            $n = $fs.Read($buf, 0, [int][Math]::Min([long]$buf.Length, $left))
            if ($n -le 0) { return $null }
            [void]$sha.TransformBlock($buf, 0, $n, $null, 0)
            $left -= $n
        }
        [void]$sha.TransformFinalBlock($buf, 0, 0)
        ([BitConverter]::ToString($sha.Hash) -replace '-', '').ToLowerInvariant()
    } finally { $fs.Dispose(); $sha.Dispose() }
}

# Copies a file from byte $From to its end.
function Copy-FileTail([string]$Src, [long]$From, [string]$Dst) {
    $in = [IO.File]::Open($Src, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        [void]$in.Seek($From, [IO.SeekOrigin]::Begin)
        $out = [IO.File]::Create($Dst)
        try { $in.CopyTo($out) } finally { $out.Dispose() }
    } finally { $in.Dispose() }
}

# A run with its own output folder (-oracle-out): everything in it moves into $CollectTo (the
# expected names and the markers at its top, anything else under other/), then the folder goes.
# Returns the names moved.
function Move-RunOutput([string]$From, [string]$CollectTo, [string[]]$TopNames) {
    $moved = New-Object System.Collections.ArrayList
    if (-not $From -or -not (Test-Path -LiteralPath $From -PathType Container)) { return [object[]]@() }
    foreach ($it in @(Get-ChildItem -LiteralPath $From)) {
        $dstDir = $(if (-not $it.PSIsContainer -and $TopNames -contains $it.Name) { $CollectTo } else { Join-Path $CollectTo 'other' })
        [void](New-Dir $dstDir)
        $dst = Join-Path $dstDir $it.Name
        if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Recurse -Force }
        for ($i = 0; ; $i++) {
            try { Move-Item -LiteralPath $it.FullName -Destination $dst -ErrorAction Stop; break }
            catch { if ($i -ge 20) { throw }; Stop-OurGame 'it may hold a recording'; Start-Sleep -Milliseconds 250 }
        }
        [void]$moved.Add($it.Name)
    }
    Remove-Item -LiteralPath $From -Recurse -Force -ErrorAction SilentlyContinue
    $parent = Split-Path -Parent $From   # (out/<id>/ once its last run folder is gone)
    if (-not @(Get-ChildItem -LiteralPath $parent -Force -ErrorAction SilentlyContinue).Count) { Remove-Item -LiteralPath $parent -Force -ErrorAction SilentlyContinue }
    [object[]]$moved
}

# ---------------------------------------------------------------------------------------------
# Backups: name_MMDDHHMM.ext by the recording's write time; never over an existing file

function Backup-OracleFile([string]$Path) {
    $cfg = $script:Cfg
    [void](New-Dir $cfg.BackupDir)
    $fi = Get-Item -LiteralPath $Path
    $stem = [IO.Path]::GetFileNameWithoutExtension($fi.Name)
    $ext = [IO.Path]::GetExtension($fi.Name)
    $stamp = $fi.LastWriteTime.ToString('MMddHHmm')
    $sha = Get-Sha256 $Path
    for ($k = 1; ; $k++) {
        $name = $(if ($k -eq 1) { "${stem}_$stamp$ext" } else { "${stem}_${stamp}_$k$ext" })
        $dst = Join-Path $cfg.BackupDir $name
        if (Test-Path -LiteralPath $dst) {
            if ((Get-Sha256 $dst) -eq $sha) { return $dst }   # (already backed up)
            continue
        }
        [IO.File]::Copy($Path, "$dst.recq-tmp", $true)
        try { [IO.File]::Move("$dst.recq-tmp", $dst) }
        catch { Remove-Item -LiteralPath "$dst.recq-tmp" -Force -ErrorAction SilentlyContinue; if (Test-Path -LiteralPath $dst) { continue }; throw }
        return $dst
    }
}

# ---------------------------------------------------------------------------------------------
# One game run

function Test-Fresh([string]$Path, [datetime]$StartUtc) {
    $fi = Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
    [bool]($fi -and $fi.LastWriteTimeUtc -ge $StartUtc)
}

function Copy-RunLog([string]$Src, [string]$Dst, [datetime]$StartUtc) {
    if ($Src -and (Test-Fresh $Src $StartUtc)) {
        try { [IO.File]::Copy($Src, $Dst, $true); return $true } catch { }
    }
    $false
}

function Save-CurrentState($state) { Write-JsonFile (QPath 'state/current.json') $state }

# Runs the game once through the launcher (hidden desktop, Normal priority, the request's
# timeout), watches for the done-marker, and judges the run. The oracle writes into $OutDir: the
# run's own folder (-oracle-out, $Redirect) or main's data/oracle. Fills in and returns $att.
function Invoke-GameRun($req, $att, [string]$AttemptDir, $state, [string]$OutDir, [bool]$Redirect) {
    $cfg = $script:Cfg
    $argLine = Get-GameArgLine $req $(if ($Redirect) { $OutDir } else { '' })
    $att.args = $argLine
    $markerPath = Join-Path $OutDir $req.done
    [void](New-Dir $AttemptDir)

    $launchArgs = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $cfg.Launcher,
        '-Exe', $cfg.GameExe, '-Arguments', $argLine, '-WorkingDirectory', $cfg.GameDir,
        '-Priority', $cfg.GamePriority, '-TimeoutSec', "$($req.timeout)", '-DesktopName', $cfg.DesktopName)
    $start = Get-Date
    $startUtc = $start.ToUniversalTime()
    $att.gameStarted = $start.ToString('o')
    Write-RecqLog "run $($req.id) attempt $($att.n): $($cfg.GameExe) $argLine (timeout $($req.timeout) s)"
    $n = Start-Native $cfg.PowerShellExe $launchArgs $cfg.GameDir
    $p = $n.Process
    $att.launcherPid = $p.Id
    $state.current = $att
    Save-CurrentState $state

    $doneAt = $null
    $timedOut = $false
    $killedAfterDone = $false
    $backstop = [double]$req.timeout + [double]$cfg.LauncherGraceSec
    while (-not $p.WaitForExit([int]$cfg.RunPollMs)) {
        $now = Get-Date
        if (-not $att.gamePid) {
            $g = @(Get-GameProcesses | Where-Object { try { $_.StartTime -ge $start.AddSeconds(-2) } catch { $true } })
            if ($g.Count) { $att.gamePid = $g[0].Id; Save-CurrentState $state }
        }
        if (-not $doneAt -and (Test-Fresh $markerPath $startUtc)) { $doneAt = $now; Write-RecqLog "run $($req.id): done-marker $($req.done) written" }
        if ($doneAt -and ($now - $doneAt).TotalSeconds -gt $cfg.DoneGraceSec) {
            Write-RecqLog "run $($req.id): the game did not quit $($cfg.DoneGraceSec) s after its done-marker: killing it"
            $killedAfterDone = $true
            Stop-ProcessTree $p.Id
            break
        }
        if (($now - $start).TotalSeconds -gt $backstop) {
            Write-RecqLog "run $($req.id): the launcher did not end $($cfg.LauncherGraceSec) s after the timeout: killing it"
            $timedOut = $true
            Stop-ProcessTree $p.Id
            break
        }
        Write-Heartbeat "running $($req.id) attempt $($att.n): $([int]($now - $start).TotalSeconds) s of $($req.timeout)"
    }
    [void]$p.WaitForExit(30000)
    $end = Get-Date
    $att.exitedAfterSec = [Math]::Round(($end - $start).TotalSeconds, 1)
    $code = $null
    if ($p.HasExited -and -not $timedOut -and -not $killedAfterDone) { $code = $p.ExitCode }
    if ($code -eq 124) { $timedOut = $true }   # (oracle_launch.ps1 killed the game at the timeout)
    $att.exitCode = $code
    $att.killed = ($timedOut -or $killedAfterDone)
    Stop-OurGame 'left over after the run'
    $launchOut = ''
    try { $launchOut = ((Get-NativeOutput $n.Out 10000) + (Get-NativeOutput $n.Err 10000)).Trim() } catch { }
    if ($launchOut) { [IO.File]::WriteAllText((Join-Path $AttemptDir 'launcher.log'), $launchOut + "`r`n", $script:Utf8) }
    # (The marker is written before Application.Quit; a moment for the file system anyway.)
    if (-not (Test-Fresh $markerPath $startUtc)) { Start-Sleep -Milliseconds 500 }

    $logCopied = Copy-RunLog $cfg.BepInExLog (Join-Path $AttemptDir 'LogOutput.log') $startUtc
    [void](Copy-RunLog $cfg.PlayerLog (Join-Path $AttemptDir 'Player.log') $startUtc)
    $att.log = $(if ($logCopied) { 'LogOutput.log' } else { $null })
    $att.oracleErrors = [object[]]@()
    if ($logCopied) {
        # (ReadAllLines, not Get-Content: its strings carry PSDrive/PSProvider notes, and
        # ConvertTo-Json -Depth 20 of those never finishes.)
        $att.oracleErrors = [object[]]@([IO.File]::ReadAllLines((Join-Path $AttemptDir 'LogOutput.log')) |
                Where-Object { $_ -match '^\[(Error|Fatal)\s*:' } | Select-Object -First 10)
    }

    # Judge: every expected file written by this run (and not empty), the done-marker written by
    # this run, no FAILED marker.
    $missing = New-Object System.Collections.ArrayList
    $stale = New-Object System.Collections.ArrayList
    $empty = New-Object System.Collections.ArrayList
    $fresh = 0
    foreach ($nm in $req.expects) {
        $fi = Get-Item -LiteralPath (Join-Path $OutDir $nm) -ErrorAction SilentlyContinue
        if (-not $fi) { [void]$missing.Add($nm) }
        elseif ($fi.LastWriteTimeUtc -lt $startUtc) { [void]$stale.Add($nm) }
        else { $fresh++; if ($fi.Length -eq 0) { [void]$empty.Add($nm) } }
    }
    $markerFresh = Test-Fresh $markerPath $startUtc
    $failedMarker = @(@($cfg.FailedMarkers) | Where-Object { Test-Fresh (Join-Path $OutDir $_) $startUtc })
    $att.missing = [object[]]$missing
    $att.stale = [object[]]$stale
    $att.empty = [object[]]$empty
    $att.doneMarker = $markerFresh

    $allGood = ($missing.Count -eq 0 -and $stale.Count -eq 0 -and $empty.Count -eq 0 -and $markerFresh -and $failedMarker.Count -eq 0)
    if ($allGood) {
        $att.reason = 'ok'
        $att.detail = $(if ($killedAfterDone) { "recorded; the game was killed $($cfg.DoneGraceSec) s after its done-marker" } elseif ($timedOut) { 'recorded; the game was killed at the timeout after its done-marker' } else { 'recorded' })
    } elseif ($timedOut) {
        $att.reason = 'timeout'; $att.detail = "no done-marker after $($req.timeout) s; the game was killed"
    } elseif ($att.exitedAfterSec -lt $cfg.StartupWindowSec -and $fresh -eq 0 -and -not $markerFresh -and $failedMarker.Count -eq 0) {
        $att.reason = 'startup'
        $steamNow = $(if ($cfg.SteamCheck) { "Steam running now: $(Test-SteamRunning)" } else { 'Steam not checked' })
        $att.detail = "the game exited after $($att.exitedAfterSec) s (code $code) without its files: a Steam or startup failure ($steamNow)"
        if ($launchOut) { $att.detail += "; launcher: $(($launchOut -split "`r?`n")[0])" }
    } elseif ($failedMarker.Count) {
        $att.reason = 'oracle-failed'; $att.detail = "the oracle wrote $($failedMarker -join ', ')"
    } elseif (-not $markerFresh -and $null -ne $code -and $code -ne 0) {
        $att.reason = 'crash'; $att.detail = "the game exited with code $code after $($att.exitedAfterSec) s without $($req.done)"
    } elseif (-not $markerFresh) {
        $att.reason = 'no-done-marker'; $att.detail = "the game exited after $($att.exitedAfterSec) s (code $code) without $($req.done)"
    } elseif ($missing.Count) {
        $att.reason = 'missing-files'; $att.detail = "not written: $($missing -join ', ')"
    } elseif ($stale.Count) {
        $att.reason = 'stale-files'; $att.detail = "not rewritten by this run: $($stale -join ', ')"
    } else {
        $att.reason = 'empty-files'; $att.detail = "empty: $($empty -join ', ')"
    }
    if ($att.reason -ne 'ok' -and $att.oracleErrors.Count) { $att.detail += "; oracle log: $($att.oracleErrors[0])" }
    Write-RecqLog "run $($req.id) attempt $($att.n): $($att.reason) ($($att.detail))"
    $att
}

# ---------------------------------------------------------------------------------------------
# Requests from start to result

# Waits while any copy of the game runs (the user playing, a run started by hand: one game at a
# time). $false when a STOP file comes first.
function Wait-NoForeignGame {
    $logged = $false
    while ($true) {
        $g = @(Get-AnyGameProcesses)
        if ($g.Count -eq 0) { return $true }
        if (Test-Path -LiteralPath (QPath 'STOP')) { return $false }
        if (-not $logged) { Write-RecqLog "waiting: the game is already running (pid $($g[0].Id)), not starting another"; $logged = $true }
        Write-Heartbeat "waiting: the game is already running (pid $($g[0].Id))" -Force
        Start-Sleep -Seconds ([Math]::Max(1, [int]$script:Cfg.PollSec))
    }
}

function Get-FileInfoRecord([string]$Path) {
    $fi = Get-Item -LiteralPath $Path
    [ordered]@{ name = $fi.Name; bytes = $fi.Length; sha256 = (Get-Sha256 $Path); written = $fi.LastWriteTime.ToString('o') }
}

# Runs the request in running/<id>.json to its result in done/<id>/ (result.json last).
# Picks up where a stopped or crashed runner left it (state/current.json). Returns the result,
# or $null when the runner was told to STOP before the request finished (it stays in running/).
function Invoke-RecqRequest([string]$RunningPath) {
    $cfg = $script:Cfg
    $id = [IO.Path]::GetFileNameWithoutExtension($RunningPath)
    $reqSha = Get-Sha256 $RunningPath
    $doneDir = QPath "done/$id"
    $resultPath = Join-Path $doneDir 'result.json'
    $curPath = QPath 'state/current.json'

    # Where are we? Our own state, a finished result (crash after result.json), or a fresh start.
    $state = $null
    if (Test-Path -LiteralPath $curPath) {
        try {
            $s = Read-JsonFile $curPath
            if ($s.id -eq $id -and $s.requestSha256 -eq $reqSha) { $state = ConvertTo-Ordered $s }
        } catch { }
    }
    if (Test-Path -LiteralPath $resultPath) {
        $old = $null
        try { $old = Read-JsonFile $resultPath } catch { }
        if ($old -and (Get-Prop $old 'requestSha256' '') -eq $reqSha -and ($state -or -not (Test-Path -LiteralPath $curPath))) {
            Write-RecqLog "$id`: already finished ($($old.status)); clearing it from running/"
            Remove-Item -LiteralPath $curPath -Force -ErrorAction SilentlyContinue
            Remove-Item -LiteralPath $RunningPath -Force
            return $old
        }
    }
    if (-not $state) {
        if (Test-Path -LiteralPath $doneDir) {
            $oldDir = New-Dir (QPath 'done/_old')
            $to = Join-Path $oldDir ("$id-" + (Get-Date).ToString('yyyyMMdd-HHmmss'))
            Move-Item -LiteralPath $doneDir -Destination $to
            Write-RecqLog "$id`: an earlier result moved to $to"
        }
        $state = [ordered]@{ id = $id; requestSha256 = $reqSha; started = (Get-Now); oracle = $null; attempts = [object[]]@(); current = $null }
        Save-CurrentState $state
    } else {
        Write-RecqLog "$id`: resuming after $(@($state.attempts).Count) attempt(s)"
    }
    [void](New-Dir $doneDir)
    $attempts = New-Object System.Collections.ArrayList
    foreach ($a in @($state.attempts)) { if ($a) { [void]$attempts.Add($a) } }

    $raw = $null
    $parseError = $null
    try { $raw = Read-JsonFile $RunningPath } catch { $parseError = $_.Exception.Message }
    $result = [ordered]@{
        id            = $id
        status        = 'failed'
        reason        = $null
        detail        = $null
        request       = $raw
        requestSha256 = $reqSha
        submitted     = (Get-Prop $raw 'submitted' $null)
        started       = $state.started
        finished      = $null
        seconds       = $null
        attempts      = $null
        files         = [object[]]@()
        doneMarker    = $null
        oracle        = $null
        backups       = [object[]]@()
        promoted      = [object[]]@()
        runner        = [ordered]@{ host = $env:COMPUTERNAME; pid = $PID; version = $script:RunnerVersion }
    }

    $v = $null
    $req = $null
    if ($parseError) { $result.reason = 'invalid-request'; $result.detail = "not JSON: $parseError" }
    else {
        $v = Test-RecqRequest $raw $id
        if (-not $v.ok) { $result.reason = 'invalid-request'; $result.detail = ($v.errors -join '; ') }
        else { $req = $v.request; $result.request = $req }
    }

    $last = $(if ($attempts.Count) { $attempts[$attempts.Count - 1] } else { $null })
    $alreadyOk = ($last -and (Get-Prop $last 'reason') -eq 'ok')

    # The oracle: main's deployed plugin, or the worktree's, built now (once per request).
    $oracle = $null
    if (-not $result.reason) {
        $oracle = $state.oracle
        if ($oracle) { $oracle = ConvertTo-Ordered $oracle }
        if (-not $alreadyOk -and $req.oracle -eq 'worktree' -and -not ($oracle -and $oracle.dll -and (Get-Sha256 $oracle.dll) -eq $oracle.sha256)) {
            Write-RecqLog "$id`: start ($($req.args); expects $($req.expects -join ', '); done-marker $($req.done); oracle from $($req.worktree))"
            $oracle = [ordered]@{ source = 'worktree' }
            $info = Get-WorktreeInfo $req.worktree
            $oracle.worktree = $req.worktree
            $oracle.branch = $info.branch
            $oracle.commit = $info.commit
            $oracle.dirty = $info.dirty
            if (-not $info.branch) { $result.reason = 'invalid-request'; $result.detail = "$($req.worktree) is not a git worktree: $($info.error)" }
            elseif ($req.branch -and $info.branch -ne $req.branch) {
                $result.reason = 'branch-mismatch'; $result.detail = "$($req.worktree) is on $($info.branch), the request says $($req.branch)"
            } else {
                Write-Heartbeat "building $id's oracle" -Force
                $build = Invoke-OracleBuild $id $req.worktree
                if (Test-Path -LiteralPath $build.log) { [IO.File]::Copy($build.log, (Join-Path $doneDir 'build.log'), $true) }
                $oracle.build = [ordered]@{ seconds = $build.seconds; removedTargets = $build.removedTargets; log = 'build.log' }
                if (-not $build.ok) { $result.reason = 'build-failed'; $result.detail = $build.reason }
                else { $oracle.dll = $build.dll; $oracle.sha256 = $build.sha256 }
            }
            $state.oracle = $oracle
            Save-CurrentState $state
        } elseif (-not $oracle) {
            Write-RecqLog "$id`: start ($($req.args); expects $($req.expects -join ', '); done-marker $($req.done); main's oracle)"
            $oracle = [ordered]@{ source = 'main'; sha256 = (Get-Sha256 $cfg.PluginPath) }
            $state.oracle = $oracle
            Save-CurrentState $state
        }
        $result.oracle = $oracle
    }

    if (-not $result.reason) {
        $topNames = @(@($req.expects) + @($req.done) + @($cfg.FailedMarkers) | Select-Object -Unique)
        $maxAttempts = 1 + $req.retries
        while (-not $alreadyOk -and $attempts.Count -lt $maxAttempts) {
            $n = $attempts.Count + 1
            if ($n -gt 1) {
                if (Test-Path -LiteralPath (QPath 'STOP')) { Write-RecqLog "$id`: STOP before attempt $n; the request stays in running/"; return $null }
                if ($cfg.RetryDelaySec -gt 0) { Write-Heartbeat "waiting $($cfg.RetryDelaySec) s before retrying $id" -Force; Start-Sleep -Seconds $cfg.RetryDelaySec }
            }
            if (-not (Wait-NoForeignGame)) { Write-RecqLog "$id`: STOP while waiting for another game to quit; the request stays in running/"; return $null }
            Write-Heartbeat "running $id attempt $n" -Force
            $att = [ordered]@{ n = $n; started = (Get-Now); finished = $null; reason = $null; detail = $null }
            $attemptDir = Join-Path $doneDir "attempts/$n"
            $att.steam = Confirm-Steam
            if (-not $att.steam.ok) {
                $att.reason = 'steam'; $att.detail = $att.steam.reason
            } else {
                # Where the oracle writes: the run's own folder when the plugin that runs knows
                # -oracle-out, else main's data/oracle (copied first and put back after).
                $runDll = $(if ($oracle.source -eq 'worktree') { $oracle.dll } else { $cfg.PluginPath })
                $redirect = ($cfg.RedirectOutput -eq 'auto') -and (Test-OracleSupportsOut $runDll)
                $outDir = $(if ($redirect) { QPath "out/$id/$n" } else { $cfg.OracleOutDir })
                $att.output = $(if ($redirect) { 'own-folder' } else { 'data-oracle' })
                $att.outDir = $outDir
                $state.current = $att
                Save-CurrentState $state
                $restored = $null
                try {
                    if ($redirect) {
                        if (Test-Path -LiteralPath $outDir) { Remove-Item -LiteralPath $outDir -Recurse -Force }
                        [void](New-Dir $outDir)
                    } else {
                        Save-OracleSnapshot $id $topNames
                    }
                    if ($oracle.source -eq 'worktree') {
                        if ((Get-Sha256 $oracle.dll) -ne $oracle.sha256) { throw "the build $($oracle.dll) changed or disappeared" }
                        Enter-PluginGuard $id 'run' $oracle.dll
                    }
                    $att = Invoke-GameRun $req $att $attemptDir $state $outDir $redirect
                } catch {
                    $att.reason = 'runner-error'; $att.detail = $_.Exception.Message
                    Write-RecqLog "$id attempt $n`: runner error: $($_.Exception.Message)"
                    Stop-OurGame 'runner error'
                } finally {
                    $restored = Exit-PluginGuard
                    $collect = $(if ($att.reason -eq 'ok') { $doneDir } else { $attemptDir })
                    if ($redirect) { $att.collected = [object[]]@(Move-RunOutput $outDir $collect $topNames) }
                    else { $att.collected = [object[]]@(Restore-OracleSnapshot $collect) }
                    if (-not $restored.ok) { $att.reason = 'plugin-restore-failed'; $att.detail = "main's plugin could not be put back" }
                    elseif ($restored.unexpected) { $att.pluginWarning = "the game's plugin was replaced during the run; kept as $($restored.unexpected)" }
                }
            }
            $att.finished = Get-Now
            [void]$attempts.Add($att)
            $state.attempts = [object[]]$attempts
            $state.current = $null
            Save-CurrentState $state
            if ($att.reason -eq 'ok' -or $att.reason -eq 'plugin-restore-failed') { break }
        }
        $last = $attempts[$attempts.Count - 1]
        $result.reason = $last.reason
        $result.detail = $last.detail
        if ($last.reason -eq 'ok') { $result.status = 'ok' }
        $logSrc = Join-Path $doneDir "attempts/$($last.n)/LogOutput.log"
        if (Test-Path -LiteralPath $logSrc) { [IO.File]::Copy($logSrc, (Join-Path $doneDir 'LogOutput.log'), $true) }
    }

    if ($result.status -eq 'ok') {
        $files = New-Object System.Collections.ArrayList
        $backups = New-Object System.Collections.ArrayList
        foreach ($nm in $req.expects) {
            $fp = Join-Path $doneDir $nm
            [void]$files.Add((Get-FileInfoRecord $fp))
            [void]$backups.Add((Backup-OracleFile $fp))
        }
        $result.files = [object[]]$files
        $mp = Join-Path $doneDir $req.done
        $result.doneMarker = [ordered]@{ name = $req.done; text = ([IO.File]::ReadAllText($mp)).Trim() }
        $result.otherFiles = [object[]]@(Get-ChildItem -LiteralPath (Join-Path $doneDir 'other') -File -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
        if ($req.promote) {
            $promoted = New-Object System.Collections.ArrayList
            [void](New-Dir $cfg.OracleOutDir)
            foreach ($nm in @($req.expects) + @($req.done)) {
                $src = Join-Path $doneDir $nm
                $dst = Join-Path $cfg.OracleOutDir $nm
                if ((Get-Sha256 $dst) -ne (Get-Sha256 $src)) {
                    if (Test-Path -LiteralPath $dst -PathType Leaf) { [void]$backups.Add((Backup-OracleFile $dst)) }
                    Copy-FileAtomic $src $dst
                    [IO.File]::SetLastWriteTimeUtc($dst, (Get-Item -LiteralPath $src).LastWriteTimeUtc)
                }
                [void]$promoted.Add($nm)
            }
            $result.promoted = [object[]]$promoted
            Write-RecqLog "$id`: promoted $($promoted -join ', ') into $($cfg.OracleOutDir)"
        }
        $result.backups = [object[]]$backups
    }

    $result.attempts = [object[]]$attempts
    $errs = Get-Prop $state 'runnerErrors' $null
    if ($errs) { $result.runnerErrors = [object[]]@($errs) }
    $result.finished = Get-Now
    try { $result.seconds = [Math]::Round(((Get-Date) - (ConvertFrom-IsoTime $result.started)).TotalSeconds, 1) } catch { }
    Write-JsonFile $resultPath $result
    Remove-Item -LiteralPath $curPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $RunningPath -Force -ErrorAction SilentlyContinue
    Write-RecqLog "$id`: $($result.status)$(if ($result.status -ne 'ok') { " ($($result.reason): $($result.detail))" }) after $($attempts.Count) attempt(s)"
    $result
}

# An exception escaped Invoke-RecqRequest (a bug, a full disk, a file held open...). Counted in
# state/current.json; the MaxRunnerErrors-th fails the request with runner-error instead of
# looping on it.
function Register-RunnerError([string]$RunningPath, [string]$Message) {
    $cfg = $script:Cfg
    if (-not (Test-Path -LiteralPath $RunningPath)) { return }
    $id = [IO.Path]::GetFileNameWithoutExtension($RunningPath)
    $reqSha = Get-Sha256 $RunningPath
    $curPath = QPath 'state/current.json'
    $state = $null
    if (Test-Path -LiteralPath $curPath) {
        try { $s = Read-JsonFile $curPath; if ($s.id -eq $id -and $s.requestSha256 -eq $reqSha) { $state = ConvertTo-Ordered $s } } catch { }
    }
    if (-not $state) { $state = [ordered]@{ id = $id; requestSha256 = $reqSha; started = (Get-Now); oracle = $null; attempts = [object[]]@(); current = $null } }
    $errs = [object[]](@(@(Get-Prop $state 'runnerErrors' @()) | Where-Object { $_ }) + @("$(Get-Now) $Message"))
    $state.runnerErrors = $errs
    Save-CurrentState $state
    if ($errs.Count -lt $cfg.MaxRunnerErrors) { return }
    $doneDir = New-Dir (QPath "done/$id")
    $raw = $null
    try { $raw = Read-JsonFile $RunningPath } catch { }
    $result = [ordered]@{
        id = $id; status = 'failed'; reason = 'runner-error'
        detail = "the runner failed on this request $($errs.Count) times; the last: $Message"
        request = $raw; requestSha256 = $reqSha; submitted = (Get-Prop $raw 'submitted' $null)
        started = $state.started; finished = (Get-Now); seconds = $null
        attempts = [object[]]@(@($state.attempts) | Where-Object { $_ }); runnerErrors = $errs
        files = [object[]]@(); doneMarker = $null; oracle = $state.oracle; backups = [object[]]@(); promoted = [object[]]@()
        runner = [ordered]@{ host = $env:COMPUTERNAME; pid = $PID; version = $script:RunnerVersion }
    }
    Write-JsonFile (Join-Path $doneDir 'result.json') $result
    Remove-Item -LiteralPath $curPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $RunningPath -Force -ErrorAction SilentlyContinue
    Write-RecqLog "$id`: failed (runner-error) after $($errs.Count) runner errors"
}

# ---------------------------------------------------------------------------------------------
# Startup and the loop

function Initialize-RecqQueue {
    foreach ($d in @('requests', 'running', 'done', 'builds', 'main-plugin', 'stash', 'state', 'cancelled', 'rejected')) { [void](New-Dir (QPath $d)) }
}

function Enter-RunnerLock {
    try {
        $script:LockStream = [IO.File]::Open((QPath 'state/runner.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
        $b = $script:Utf8.GetBytes("$PID")
        $script:LockStream.SetLength(0)
        $script:LockStream.Write($b, 0, $b.Length)
        $script:LockStream.Flush()
        return $true
    } catch { return $false }
}

function Exit-RunnerLock {
    if ($script:LockStream) { $script:LockStream.Dispose(); $script:LockStream = $null }
}

# Whether a runner holds the queue's lock (status.ps1, start.ps1).
function Test-RunnerAlive {
    $lock = QPath 'state/runner.lock'
    if (-not (Test-Path -LiteralPath $lock)) { return $false }
    try { $s = [IO.File]::Open($lock, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read); $s.Dispose(); return $false }
    catch { return $true }
}

# After a crash of the runner (or before anything else, every round): our game killed, main's
# plugin and recordings back, the interrupted attempt recorded in state/current.json.
function Invoke-RecqRecovery {
    $running = @(Get-ChildItem -LiteralPath (QPath 'running') -Filter '*.json' -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -like '*.json' })
    $swap = Test-Path -LiteralPath (QPath 'state/swap.json')
    $stash = Test-Path -LiteralPath (QPath 'state/stash.json')
    $cur = $null
    if (Test-Path -LiteralPath (QPath 'state/current.json')) { try { $cur = ConvertTo-Ordered (Read-JsonFile (QPath 'state/current.json')) } catch { } }
    $inAttempt = [bool]($cur -and $cur.current)
    if (-not $swap -and -not $stash -and -not $inAttempt) { return $true }

    Write-RecqLog "recovery: an attempt was interrupted (swap: $swap, data/oracle copy: $stash, attempt in progress: $inAttempt)"
    # The interrupted launcher, if it still runs (checked by its command line: after a reboot the
    # pid may be anybody's).
    if ($cur -and $cur.current) {
        $lp = Get-Prop $cur.current 'launcherPid' $null
        if ($lp -and (Test-OurProcess $lp $script:Cfg.Launcher)) { Write-RecqLog "recovery: killing the interrupted launcher (pid $lp)"; Stop-ProcessTree $lp }
    }
    # Nothing else starts our game copy while the runner owns it: any instance of it is ours.
    Stop-OurGame 'recovery'
    # A build the dead runner started (dotnet and its nodes, building in builds/<id>/).
    if ($swap) {
        $phase = $null
        try { $phase = (Read-JsonFile (QPath 'state/swap.json')).phase } catch { }
        if ($phase -eq 'build') { Stop-QueueBuilds }
    }
    $g = Exit-PluginGuard
    if ($swap -and $g.ok) { Write-RecqLog "recovery: main's plugin is back" }
    $collected = [object[]]@()
    if ($stash) {
        $sid = $null
        try { $sid = (Read-JsonFile (QPath 'state/stash.json')).id } catch { }
        $n = $(if ($cur -and $cur.current -and $cur.id -eq $sid) { Get-Prop $cur.current 'n' 1 } else { $null })
        $to = $(if ($n) { QPath "done/$sid/attempts/$n" } else { QPath "stash-recovered/$((Get-Date).ToString('yyyyMMdd-HHmmss'))" })
        $collected = [object[]]@(Restore-OracleSnapshot $to)
        Write-RecqLog "recovery: main's recordings are back (the run's files: $to)"
    }
    if ($inAttempt -and (Get-Prop $cur.current 'output' '') -eq 'own-folder') {
        $od = Get-Prop $cur.current 'outDir' ''
        $to = QPath "done/$($cur.id)/attempts/$(Get-Prop $cur.current 'n' 1)"
        $topNames = @($script:Cfg.FailedMarkers)
        try {
            $rq = Read-JsonFile (QPath "running/$($cur.id).json")
            $dm = [string](Get-Prop $rq 'done' '')
            if (-not $dm) { $dm = Get-DoneMarker ([string](Get-Prop $rq 'args' '')) }
            $topNames = @(@(Get-Prop $rq 'expects' @()) + @($dm) + $topNames | Where-Object { $_ })
        } catch { }
        $collected = [object[]]@(Move-RunOutput $od $to $topNames)
        if ($collected.Count) { Write-RecqLog "recovery: the interrupted run's files moved to $to" }
    }
    if ($inAttempt) {
        $c = ConvertTo-Ordered $cur.current
        $att = [ordered]@{
            n = (Get-Prop $c 'n' 1); started = (Get-Prop $c 'started' $null); finished = (Get-Now)
            reason = 'runner-interrupted'; detail = 'the runner stopped during this attempt; the next runner cleaned up'
            launcherPid = (Get-Prop $c 'launcherPid' $null); gamePid = (Get-Prop $c 'gamePid' $null); collected = [object[]]$collected
        }
        $cur.attempts = [object[]](@(@($cur.attempts) | Where-Object { $_ }) + @($att))
        $cur.current = $null
        Save-CurrentState $cur
        Write-RecqLog "recovery: $($cur.id) attempt $($att.n) recorded as runner-interrupted"
    }
    $g.ok
}

function Start-RecqRunner([string]$ConfigPath, [switch]$Once) {
    $cfg = Read-RecqConfig $ConfigPath
    Set-RecqConfig $cfg
    Initialize-RecqQueue
    if (-not (Enter-RunnerLock)) {
        Write-Host "recq: another runner holds $(QPath 'state/runner.lock'); exiting"
        return 3
    }
    try {
        try { [Diagnostics.Process]::GetCurrentProcess().PriorityClass = $cfg.RunnerPriority } catch { }
        Write-RecqLog "runner $PID started ($(if ($ConfigPath) { "config $ConfigPath" } else { 'built-in config' }); queue $($cfg.QueueDir))"
        Write-Heartbeat 'starting' -Force
        $idleLogged = $false
        while ($true) {
            if (-not (Invoke-RecqRecovery)) {
                Write-Heartbeat "ERROR: main's plugin could not be restored (state/swap.json); retrying" -Force
                if ($Once) { Write-RecqLog "main's plugin could not be restored: exiting (-Once)"; return 4 }
                Start-Sleep -Seconds ([Math]::Max(1, [int]$cfg.PollSec))
                continue
            }
            if (Test-Path -LiteralPath (QPath 'STOP')) { Write-RecqLog 'STOP file: exiting'; break }
            if (Test-Path -LiteralPath (QPath 'PAUSE')) {
                Write-Heartbeat 'paused (PAUSE file)' -Force
                if ($Once) { Write-RecqLog 'PAUSE file: exiting (-Once)'; break }
                Start-Sleep -Seconds ([Math]::Max(1, [int]$cfg.PollSec))
                continue
            }
            $next = @(Get-ChildItem -LiteralPath (QPath 'running') -Filter '*.json' -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -like '*.json' } | Sort-Object Name)
            if ($next.Count) { $path = $next[0].FullName }
            else {
                $pending = @(Get-PendingRequests)
                if ($pending.Count -eq 0) {
                    if ($Once -and @(Get-ChildItem -LiteralPath (QPath 'requests') -Filter '*.json' -File -ErrorAction SilentlyContinue).Count -eq 0) { Write-RecqLog 'queue empty: exiting (-Once)'; break }
                    if (-not $idleLogged) { Write-RecqLog 'queue empty: waiting'; $idleLogged = $true }
                    Write-Heartbeat 'idle' -Force
                    Start-Sleep -Seconds $(if ($Once) { 1 } else { [Math]::Max(1, [int]$cfg.PollSec) })
                    continue
                }
                $f = $pending[0]
                $id = [IO.Path]::GetFileNameWithoutExtension($f.Name)
                if ($id -notmatch $script:IdPattern) {
                    $to = Join-Path (QPath 'rejected') ("{0}-{1}" -f (Get-Date).ToString('yyyyMMdd-HHmmss'), $f.Name)
                    Move-Item -LiteralPath $f.FullName -Destination $to -Force
                    [IO.File]::WriteAllText("$to.reason.txt", "the file name is not a valid id ($($script:IdPattern))", $script:Utf8)
                    Write-RecqLog "rejected $($f.Name): not a valid id"
                    continue
                }
                $path = Join-Path (QPath 'running') $f.Name
                try { Move-Item -LiteralPath $f.FullName -Destination $path -ErrorAction Stop }
                catch { Write-RecqLog "could not take $($f.Name): $($_.Exception.Message)"; Start-Sleep -Seconds 1; continue }
            }
            $idleLogged = $false
            Write-Heartbeat "request $([IO.Path]::GetFileNameWithoutExtension($path))" -Force
            try {
                $r = Invoke-RecqRequest $path
                if ($null -eq $r) { continue }   # (STOP between attempts: the loop exits at the top)
            } catch {
                $msg = $_.Exception.Message
                Write-RecqLog "ERROR in $path`: $msg`r`n$($_.ScriptStackTrace)"
                # Clean up like after a crash; the request stays in running/ and is tried again,
                # up to MaxRunnerErrors times, then it fails with runner-error.
                try { [void](Invoke-RecqRecovery) } catch { Write-RecqLog "recovery failed too: $($_.Exception.Message)" }
                try { Register-RunnerError $path $msg } catch { Write-RecqLog "could not record the error: $($_.Exception.Message)" }
                # (If even that fails, don't spin: stop after a few rounds on the same request.)
                $script:ErrorCounts[$path] = 1 + [int]$script:ErrorCounts[$path]
                if ((Test-Path -LiteralPath $path) -and $script:ErrorCounts[$path] -gt [int]$cfg.MaxRunnerErrors + 1) {
                    Write-RecqLog "giving up: the runner keeps failing on $path and cannot record it; exiting"
                    return 6
                }
                Start-Sleep -Seconds $(if ($Once) { 1 } else { [Math]::Max(1, [int]$cfg.PollSec) })
            }
        }
    } finally {
        Write-Heartbeat 'stopped' -Force
        Exit-RunnerLock
    }
    0
}

Export-ModuleMember -Function *
