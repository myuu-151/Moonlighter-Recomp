# Tests for the recording queue (tools/recq): every failure path of the runner, the client and the
# recovery after a crash, against a fake game, a fake Steam and throwaway oracle projects. Nothing
# here touches the real game, its plugin, Steam or main's data/oracle (the build test of the real
# oracle only reads the game's reference assemblies, and checks the deployed plugin is unchanged).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\recq\tests\run_tests.ps1
#       [-Only name,name] [-SkipDotnet] [-Scratch <dir>] [-Keep]
#
# The fake game (fakes/FakeGame.cs) and fake Steam (fakes/FakeSteam.cs) are compiled into the scratch
# folder; games run through the real tools/oracle_launch.ps1 on a hidden desktop named RecqTest.
# -SkipDotnet leaves out the tests that run `dotnet build` (about a minute of the total).
# Exit code: the number of failed tests.
param(
    [string]$Scratch = (Join-Path $env:TEMP 'recq-tests'),
    [string[]]$Only = @(),
    [switch]$SkipDotnet,
    [switch]$Keep
)
$ErrorActionPreference = 'Stop'
$Here = $PSScriptRoot
$Recq = Split-Path -Parent $Here
$Repo = Split-Path -Parent (Split-Path -Parent $Recq)
Import-Module (Join-Path $Recq 'recq.psm1') -Force -DisableNameChecking
$PsExe = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
$Git = 'C:/Program Files/Git/cmd/git.exe'
$RealGame = 'C:/Users/legen/Documents/moonlighter/Moonlighter'
try { [Diagnostics.Process]::GetCurrentProcess().PriorityClass = 'BelowNormal' } catch { }

if ($Only.Count -eq 1 -and $Only[0] -match ',') { $Only = $Only[0] -split ',' }
if (Test-Path -LiteralPath $Scratch) {
    Get-Process recqtest_game, recqtest_steam -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
    Remove-Item -LiteralPath $Scratch -Recurse -Force
}
$Bin = (New-Item -ItemType Directory -Force (Join-Path $Scratch 'bin')).FullName
Add-Type -TypeDefinition ([IO.File]::ReadAllText((Join-Path $Here 'fakes/FakeGame.cs'))) -OutputAssembly (Join-Path $Bin 'recqtest_game.exe') -OutputType WindowsApplication -ReferencedAssemblies System.Core
Add-Type -TypeDefinition ([IO.File]::ReadAllText((Join-Path $Here 'fakes/FakeSteam.cs'))) -OutputAssembly (Join-Path $Bin 'recqtest_steam.exe') -OutputType WindowsApplication
Add-Type -TypeDefinition 'public static class FakeRef { public static int Value = 7; }' -OutputAssembly (Join-Path $Bin 'FakeRef.dll') -OutputType Library
$Utf8 = New-Object System.Text.UTF8Encoding($false)

# ---------------------------------------------------------------------------------------------
# Helpers

$script:Failures = New-Object System.Collections.ArrayList
function Assert([bool]$cond, [string]$msg) {
    if (-not $cond) { [void]$script:Failures.Add($msg); Write-Host "    FAIL: $msg" -ForegroundColor Red }
}
function AssertEq($actual, $expected, [string]$msg) {
    Assert ("$actual" -eq "$expected") "$msg (expected '$expected', got '$actual')"
}

function Write-Text([string]$path, [string]$text) {
    $d = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $d)) { [void](New-Item -ItemType Directory -Force $d) }
    [IO.File]::WriteAllText($path, $text, $Utf8)
}

function Get-DirState([string]$dir) {
    $h = [ordered]@{}
    foreach ($f in @(Get-ChildItem -LiteralPath $dir -File -ErrorAction SilentlyContinue | Sort-Object Name)) {
        $h[$f.Name] = "$((Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash) $($f.LastWriteTimeUtc.Ticks)"
    }
    ($h.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join '; '
}

# A fresh fake world: game copy (exe, plugin, reference dlls), data/oracle with main's recordings,
# queue and backup folders, and a config pointing at all of it.
# -Redirect: main's "plugin" knows -oracle-out (the fake game then writes into the run's folder).
function New-Env([string]$name, [hashtable]$over = @{}, [switch]$Redirect) {
    $root = (New-Item -ItemType Directory -Force (Join-Path $Scratch "t/$name")).FullName
    $game = Join-Path $root 'game'
    [void](New-Item -ItemType Directory -Force (Join-Path $game 'BepInEx/plugins'), (Join-Path $game 'BepInEx/core'), (Join-Path $game 'Moonlighter_Data/Managed'))
    Copy-Item (Join-Path $Bin 'recqtest_game.exe') $game
    Copy-Item (Join-Path $Bin 'FakeRef.dll') (Join-Path $game 'BepInEx/core')
    Write-Text (Join-Path $game 'Moonlighter_Data/Managed/Fake.Managed.dll') 'not really a dll'
    Write-Text (Join-Path $game 'BepInEx/plugins/MoonlighterOracle.dll') "MAIN PLUGIN $name"
    if ($Redirect) { [IO.File]::WriteAllBytes((Join-Path $game 'BepInEx/plugins/MoonlighterOracle.dll'), [Text.Encoding]::Unicode.GetBytes("MAIN PLUGIN $name knows -oracle-out=")) }
    $oracle = Join-Path $root 'data/oracle'
    Write-Text (Join-Path $oracle 'keep.jsonl') "main's keep`n"
    Write-Text (Join-Path $oracle 'trace.jsonl') "main's trace`n"
    $old = (Get-Date).AddHours(-3)
    foreach ($f in Get-ChildItem $oracle) { $f.LastWriteTime = $old }
    $cfg = [ordered]@{
        QueueDir = (Join-Path $root 'queue'); GameDir = $game; GameExe = (Join-Path $game 'recqtest_game.exe')
        OracleOutDir = $oracle; BackupDir = (Join-Path $root 'backup'); PlayerLog = (Join-Path $root 'nolog/Player.log')
        Launcher = (Join-Path $Repo 'tools/oracle_launch.ps1'); DesktopName = 'RecqTest'
        SteamCheck = $false; SteamExe = (Join-Path $Bin 'recqtest_steam.exe'); SteamProcessName = 'recqtest_steam'
        SteamLog = (Join-Path $root 'steam/connection_log.txt')
        SteamArgs = "-silent -log `"$(Join-Path $root 'steam/connection_log.txt')`" -delay 1"
        SteamConnectTimeoutSec = 15; SteamSettleSec = 0
        StartupWindowSec = 3; DoneGraceSec = 3; LauncherGraceSec = 10; RetryDelaySec = 0
        PollSec = 1; RunPollMs = 200; HeartbeatSec = 1; RunnerPriority = 'BelowNormal'; MinRequestAgeSec = 0
        DefaultTimeoutSec = 30; BuildTimeoutSec = 300
    }
    foreach ($k in $over.Keys) { $cfg[$k] = $over[$k] }
    $cfgPath = Join-Path $root 'config.json'
    Write-Text $cfgPath ($cfg | ConvertTo-Json -Depth 5)
    [pscustomobject]@{
        Name = $name; Root = $root; Game = $game; Oracle = $oracle; Queue = $cfg.QueueDir; Backup = $cfg.BackupDir
        Config = $cfgPath; Plugin = (Join-Path $game 'BepInEx/plugins/MoonlighterOracle.dll'); SteamLog = $cfg.SteamLog
        MainPluginSha = (Get-FileHash (Join-Path $game 'BepInEx/plugins/MoonlighterOracle.dll') -Algorithm SHA256).Hash
        OracleState = (Get-DirState $oracle)
        OracleDirTime = (Get-Item -LiteralPath $oracle).LastWriteTimeUtc.Ticks
    }
}

function Set-Plan($e, [string[]]$lines) { Write-Text (Join-Path $e.Game 'fakegame.plan') (($lines -join "`r`n") + "`r`n") }
function Get-Runs($e) { @(Get-Content -LiteralPath (Join-Path $e.Game 'fakegame.runs.log') -ErrorAction SilentlyContinue | Where-Object { $_ }) }

$script:SubmitCount = 0
function Submit($e, [string]$id, [hashtable]$fields, [string]$fileName = '') {
    $script:SubmitCount++
    $r = [ordered]@{ id = $id; args = '-oracle-enemy=Fake'; expects = @('enemy_Fake.jsonl'); retries = 0 }
    foreach ($k in $fields.Keys) { $r[$k] = $fields[$k] }
    if (-not $r.Contains('submitted')) { $r.submitted = (Get-Date).AddSeconds($script:SubmitCount * 0.001).ToString('o') }
    if (-not $fileName) { $fileName = "$id.json" }
    Write-Text (Join-Path $e.Queue "requests/$fileName") ($r | ConvertTo-Json -Depth 5)
}

function Invoke-Runner($e, [switch]$Once, [int]$TimeoutSec = 240) {
    $a = @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $Recq 'runner.ps1'), '-Config', $e.Config)
    if ($Once) { $a += '-Once' }
    $r = Invoke-Native $PsExe $a $TimeoutSec
    if ($r.TimedOut) { Assert $false "runner did not finish in $TimeoutSec s" }
    if ($r.Code -ne 0) { Write-Host "    (runner exit code $($r.Code); its output:)`n$((($r.Out + $r.Err) -split "`r?`n" | Select-Object -Last 15 | ForEach-Object { "      $_" }) -join "`n")" }
    [pscustomobject]@{ Code = $r.Code; Out = ($r.Out + $r.Err) }
}

function Start-RunnerBg($e) {
    Start-Native $PsExe @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $Recq 'runner.ps1'), '-Config', $e.Config)
}

function Invoke-Client([string]$script, [string[]]$a, [int]$TimeoutSec = 120) {
    $r = Invoke-Native $PsExe (@('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $Recq $script)) + $a) $TimeoutSec
    [pscustomobject]@{ Code = $r.Code; Out = ($r.Out + $r.Err); StdOut = $r.Out }
}

function Get-Result($e, [string]$id) {
    $p = Join-Path $e.Queue "done/$id/result.json"
    if (-not (Test-Path -LiteralPath $p)) { return $null }
    [IO.File]::ReadAllText($p) | ConvertFrom-Json
}

function Wait-Until([scriptblock]$cond, [int]$TimeoutSec = 60, [string]$what = 'condition') {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    while ((Get-Date) -lt $deadline) { if (& $cond) { return $true }; Start-Sleep -Milliseconds 200 }
    Assert $false "timed out waiting for $what"
    $false
}

function Assert-MainIntact($e, [string]$why = '') {
    AssertEq (Get-FileHash $e.Plugin -Algorithm SHA256).Hash $e.MainPluginSha "main's plugin is back $why"
    AssertEq (Get-DirState $e.Oracle) $e.OracleState "main's data/oracle is as it was $why"
    Assert (-not (Test-Path (Join-Path $e.Queue 'state/swap.json'))) "no plugin guard left $why"
    Assert (-not (Test-Path (Join-Path $e.Queue 'state/stash.json'))) "no data/oracle copy left $why"
    Assert (@(Get-Process recqtest_game -ErrorAction SilentlyContinue).Count -eq 0) "no game process left $why"
}

function Assert-Result($e, [string]$id, [string]$status, [string]$reason, [int]$attempts = -1) {
    $r = Get-Result $e $id
    Assert ($null -ne $r) "done/$id/result.json exists"
    if (-not $r) { return $null }
    AssertEq $r.status $status "$id status"
    AssertEq $r.reason $reason "$id reason ($($r.detail))"
    if ($attempts -ge 0) { AssertEq @($r.attempts).Count $attempts "$id attempts" }
    Assert (-not (Test-Path (Join-Path $e.Queue "running/$id.json"))) "$id left running/"
    Assert (-not (Test-Path (Join-Path $e.Queue 'state/current.json'))) "state/current.json cleared after $id"
    $r
}

# A git worktree with a fake oracle project: MoonlighterOracle.csproj like the real one (GameDir,
# DeployToGame), plus a second deploy target with the game's plugins folder written out, and
# optionally a sneaky target that writes the plugin through a property (not caught by name).
function New-FakeWorktree($e, [string]$tag, [switch]$Broken, [switch]$Sneaky, [switch]$Redirect) {
    $wt = Join-Path $e.Root 'wt'
    $proj = Join-Path $wt 'oracle/MoonlighterOracle'
    [void](New-Item -ItemType Directory -Force $proj)
    $pluginsDir = Join-Path $e.Game 'BepInEx\plugins'
    $sneakyProps = $(if ($Sneaky) { "<SneakyDest>$pluginsDir</SneakyDest>" } else { '' })
    $sneakyTarget = $(if ($Sneaky) { '<Target Name="Sneaky" AfterTargets="Build"><Copy SourceFiles="$(TargetPath)" DestinationFolder="$(SneakyDest)" /></Target>' } else { '' })
    Write-Text (Join-Path $proj 'MoonlighterOracle.csproj') @"
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net472</TargetFramework>
    <AssemblyName>MoonlighterOracle</AssemblyName>
    <GameDir>$($e.Game)</GameDir>
    $sneakyProps
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include="Microsoft.NETFramework.ReferenceAssemblies" Version="1.0.3" PrivateAssets="all" />
  </ItemGroup>
  <ItemGroup>
    <Reference Include="FakeRef"><HintPath>`$(GameDir)\BepInEx\core\FakeRef.dll</HintPath><Private>false</Private></Reference>
  </ItemGroup>
  <Target Name="DeployToGame" AfterTargets="Build">
    <Copy SourceFiles="`$(TargetPath)" DestinationFolder="`$(GameDir)\BepInEx\plugins" />
  </Target>
  <Target Name="AlsoDeploy" AfterTargets="Build">
    <Copy SourceFiles="`$(TargetPath)" DestinationFolder="$pluginsDir" />
  </Target>
  $sneakyTarget
</Project>
"@
    $code = "namespace MoonlighterOracle { public static class Plugin { public const string Tag = `"$tag`"; public static int F() { return FakeRef.Value; } } }"
    if ($Redirect) { $code = $code.Replace('public static int F()', 'public static string Out(string a) { return a.StartsWith("-oracle-out=") ? a.Substring(12) : null; } public static int F()') }
    if ($Broken) { $code = $code.Replace('return FakeRef.Value;', 'return FakeRef.Value') }
    Write-Text (Join-Path $proj 'Plugin.cs') $code
    [void](Invoke-Native $Git @('-C', $wt, 'init', '-q', '-b', 'agent-test') 60)
    [void](Invoke-Native $Git @('-C', $wt, 'add', '-A') 60)
    $c = Invoke-Native $Git @('-C', $wt, '-c', 'user.name=recq-test', '-c', 'user.email=recq-test@example.invalid', 'commit', '-q', '-m', 'fake oracle') 60
    Assert ($c.Code -eq 0) "git commit in the fake worktree ($($c.Err))"
    $wt
}

# ---------------------------------------------------------------------------------------------
# The tests

$Tests = [ordered]@{}

$Tests['unit-steam-log'] = {
    $lines = @(
        '[2026-10-13 12:56:42] [Logged Off, 4, 0] [U:1:342162341] LogOn() called; not connected yet, scheduling connection. Schedule init returned 1',
        '[2026-10-13 12:56:43] GetCMListForConnect -- DC ''lhr1'' count: 9',
        '[2026-10-13 12:56:44] [Connecting, 4, 7] [U:1:342162341] ConnectionCompleted() (162.254.196.79:27020, WebSocket) local address (192.168.0.44:58887)',
        '[2026-10-13 12:56:44] [Connected, 4, 7] [U:1:342162341] Logging on [U:1:342162341]',
        '[2026-10-13 12:56:44] [Logging On, 4, 7] [U:1:342162341] RecvMsgClientLogOnResponse() : [U:1:342162341] ''OK''',
        '[2026-10-13 12:56:44] [Logged On, 4, 7] [U:1:342162341] RecvMsgClientLogOnResponse() : processing complete',
        '[2026-10-13 12:56:44] CClientJobGetClientUpdateHosts: cached version not expired')
    $s = Get-SteamConnectionState $lines
    AssertEq $s.State 'Logged On' 'last state of a logged-on log'
    Assert $s.Connected 'Logged On counts as connected'
    $s = Get-SteamConnectionState $lines[0..3]
    AssertEq $s.State 'Connected' 'state before the logon'
    Assert (-not $s.Connected) 'a bare Connected does not count (it also shows in failed logons)'
    $s = Get-SteamConnectionState $lines ([datetime]'2026-10-13 12:57:00')
    Assert (-not $s.Connected) 'lines before -Since are ignored (a freshly started Steam)'
    $s = Get-SteamConnectionState ($lines + '[2026-10-13 13:10:00] [Logged Off, 0, 0] [U:1:342162341] Sending SteamServersDisconnected_t because we were logged on')
    Assert (-not $s.Connected) 'a later Logged Off wins'
    $s = Get-SteamConnectionState $lines[0..3] $null @('Connected', 'Logged On')
    Assert $s.Connected 'the accepted states are configurable'
}

$Tests['unit-request-checks'] = {
    Set-RecqConfig (Read-RecqConfig '')
    AssertEq (Get-DoneMarker '-oracle-enemy=GolemButler') 'ENEMY_GolemButler_DONE' 'enemy marker'
    AssertEq (Get-DoneMarker '-oracle-enemy=GolemSoldier:Room_3 -oracle-quit') 'ENEMY_GolemSoldier_DONE' 'enemy marker with a room'
    AssertEq (Get-DoneMarker '-oracle-enemy="GolemSoldier:Room A"') 'ENEMY_GolemSoldier_DONE' 'enemy marker, quoted room'
    AssertEq (Get-DoneMarker '-oracle-walk=roll') 'WALK_DONE' 'walk marker'
    AssertEq (Get-DoneMarker '-oracle-slime') 'SLIME_DONE' 'slime marker'
    AssertEq (Get-DoneMarker '-oracle-run') 'DONE' 'run marker'
    AssertEq (Get-DoneMarker '-oracle-walk -oracle-polynav') '' 'two modes: no marker inferred'
    $ok = '{"id":"a-1","args":"-oracle-enemy=GolemButler","expects":["enemy_GolemButler.jsonl"],"timeout":900}' | ConvertFrom-Json
    $v = Test-RecqRequest $ok 'a-1'
    Assert $v.ok "the plan's example request is valid ($($v.errors -join '; '))"
    AssertEq $v.request.done 'ENEMY_GolemButler_DONE' 'inferred done-marker'
    AssertEq $v.request.oracle 'main' 'no worktree: main oracle'
    foreach ($bad in @(
            @('{"id":"b","args":"-oracle-enemy=X -oracle-visible","expects":["x.jsonl"]}', 'oracle-visible'),
            @('{"id":"b","args":"-oracle-enemy=X -screen-width 1920","expects":["x.jsonl"]}', 'screen-'),
            @('{"id":"b","args":"-oracle-quit","expects":["x.jsonl"]}', 'no -oracle-<mode>'),
            @('{"id":"b","args":"-oracle-enemy=X","expects":[]}', 'expects is empty'),
            @('{"id":"b","args":"-oracle-enemy=X","expects":["../x.jsonl"]}', 'plain file name'),
            @('{"id":"b","args":"-oracle-enemy=X","expects":["x.jsonl"],"timeout":99999}', 'timeout must be'),
            @('{"id":"c","args":"-oracle-enemy=X","expects":["x.jsonl"]}', 'does not match the file name'),
            @('{"id":"b","args":"-oracle-enemy=X","expects":["x.jsonl"],"oracle":"worktree"}', 'needs a worktree'),
            @('{"id":"b","args":"-oracle-enemy=X","expects":["x.jsonl"],"worktree":"C:/nowhere/at/all"}', 'no oracle project'))) {
        $v = Test-RecqRequest ($bad[0] | ConvertFrom-Json) 'b'
        Assert (-not $v.ok -and ($v.errors -join ' ') -match [regex]::Escape($bad[1])) "rejects $($bad[0]) with '$($bad[1])' (got: $($v.errors -join '; '))"
    }
    $v = Test-RecqRequest ('{"id":"b","args":"-oracle-enemy=X -oracle-out=C:/x","expects":["x.jsonl"]}' | ConvertFrom-Json) 'b'
    Assert (-not $v.ok -and ($v.errors -join ' ') -match 'oracle-out') "rejects a request that sets -oracle-out ($($v.errors -join '; '))"
    AssertEq (Get-GameArgLine ([ordered]@{ args = '-oracle-walk=roll' }) 'C:\q\out\w-1\1') '-oracle-walk=roll -oracle-quit -oracle-out=C:\q\out\w-1\1' 'game args with -oracle-out'
    AssertEq (Get-GameArgLine ([ordered]@{ args = '-oracle-walk=roll -oracle-quit' })) '-oracle-walk=roll -oracle-quit' 'game args keep a given -oracle-quit'
    AssertEq (ConvertTo-ArgString '-oracle-enemy=X -oracle-quit') '"-oracle-enemy=X -oracle-quit"' 'quoting'
    AssertEq (ConvertTo-ArgString 'a"b') '"a\"b"' 'quoting a quote'
    AssertEq (ConvertTo-ArgString 'C:\dir with space\') '"C:\dir with space\\"' 'quoting a trailing backslash'
}

$Tests['unit-oracle-out-detection'] = {
    $d = (New-Item -ItemType Directory -Force (Join-Path $Scratch 't/unit-detect')).FullName
    $even = Join-Path $d 'even.dll'; $odd = Join-Path $d 'odd.dll'; $ascii = Join-Path $d 'ascii.dll'
    [IO.File]::WriteAllBytes($even, [Text.Encoding]::Unicode.GetBytes('MZ..-oracle-out=..'))
    [IO.File]::WriteAllBytes($odd, [byte[]](@([byte]0x4D) + [Text.Encoding]::Unicode.GetBytes('-oracle-out=')))
    [IO.File]::WriteAllBytes($ascii, [Text.Encoding]::ASCII.GetBytes('MZ -oracle-out= in ASCII only'))
    Assert (Test-OracleSupportsOut $even) 'the UTF-16 literal at an even offset'
    Assert (Test-OracleSupportsOut $odd) 'the UTF-16 literal at an odd offset'
    Assert (-not (Test-OracleSupportsOut $ascii)) 'an ASCII string is no string literal'
    Assert (-not (Test-OracleSupportsOut (Join-Path $d 'missing.dll'))) 'no file: no'
}

$Tests['unit-deploy-targets'] = {
    $d = (New-Item -ItemType Directory -Force (Join-Path $Scratch 't/unit-deploy')).FullName
    $p = Join-Path $d 'MoonlighterOracle.csproj'
    Copy-Item (Join-Path $Repo 'oracle/MoonlighterOracle/MoonlighterOracle.csproj') $p
    $removed = @(Remove-DeployTargets $p)
    AssertEq ($removed -join ',') 'DeployToGame' "the real csproj's deploy target is removed"
    $x = [xml](Get-Content -Raw $p)
    AssertEq @($x.GetElementsByTagName('Target')).Count 0 'no target left'
    AssertEq @($x.GetElementsByTagName('Reference')).Count 10 'the references stay'
}

$Tests['unit-backups'] = {
    $e = New-Env 'unit-backups'
    Set-RecqConfig (Read-RecqConfig $e.Config)
    $f = Join-Path $e.Root 'x/enemy_A.jsonl'
    Write-Text $f 'first'
    $t = [datetime]'2026-10-13 12:31:10'
    (Get-Item $f).LastWriteTime = $t
    $b1 = Backup-OracleFile $f
    AssertEq (Split-Path -Leaf $b1) 'enemy_A_10131231.jsonl' 'name_MMDDHHMM by the write time'
    $b1again = Backup-OracleFile $f
    AssertEq $b1again $b1 'the same content is not backed up twice'
    Write-Text $f 'second'
    (Get-Item $f).LastWriteTime = $t.AddSeconds(20)
    $b2 = Backup-OracleFile $f
    AssertEq (Split-Path -Leaf $b2) 'enemy_A_10131231_2.jsonl' 'another recording in the same minute gets _2'
    AssertEq ([IO.File]::ReadAllText($b1)) 'first' 'the first backup is never overwritten'
    AssertEq ([IO.File]::ReadAllText($b2)) 'second' 'the second backup'
}

$Tests['ok-main-oracle'] = {
    $e = New-Env 'ok-main'
    Set-Plan $e 'ok'
    Submit $e 'ok-1' @{ args = '-oracle-enemy=Fake'; expects = @('enemy_Fake.jsonl') }
    $run = Invoke-Runner $e -Once
    AssertEq $run.Code 0 'runner exit code'
    $r = Assert-Result $e 'ok-1' 'ok' 'ok' 1
    Assert-MainIntact $e
    $d = Join-Path $e.Queue 'done/ok-1'
    Assert (Test-Path (Join-Path $d 'enemy_Fake.jsonl')) 'the recording is in done/<id>/'
    Assert (Test-Path (Join-Path $d 'ENEMY_Fake_DONE')) 'the done-marker is in done/<id>/'
    Assert (Test-Path (Join-Path $d 'LogOutput.log')) 'LogOutput.log is in done/<id>/'
    Assert (-not (Test-Path (Join-Path $e.Oracle 'enemy_Fake.jsonl'))) "the recording is not left in main's data/oracle"
    $runs = @(Get-Runs $e)
    AssertEq $runs.Count 1 'one game run'
    Assert ($runs[0] -match '-screen-fullscreen 0 -screen-width 640 -screen-height 360 -oracle-enemy=Fake -oracle-quit') "the launcher's window args and -oracle-quit ($($runs[0]))"
    $content = [IO.File]::ReadAllText((Join-Path $d 'enemy_Fake.jsonl')) | ConvertFrom-Json
    AssertEq $content.plugin.ToUpperInvariant() $e.MainPluginSha "main's plugin was in place for a main-oracle request"
    AssertEq @($r.backups).Count 1 'one backup'
    Assert ((Split-Path -Leaf $r.backups[0]) -match '^enemy_Fake_\d{8}\.jsonl$') "backup name ($($r.backups[0]))"
    Assert (Test-Path $r.backups[0]) 'the backup exists'
    AssertEq $r.files[0].sha256 ((Get-FileHash (Join-Path $d 'enemy_Fake.jsonl') -Algorithm SHA256).Hash.ToLowerInvariant()) 'result.json records the sha256'
    AssertEq $r.oracle.source 'main' 'oracle source'
    AssertEq $r.attempts[0].exitCode 0 'exit code recorded'
    AssertEq $r.attempts[0].output 'data-oracle' "an oracle without -oracle-out writes into main's data/oracle (copied and put back)"
    Assert ($runs[0] -notmatch 'oracle-out') 'no -oracle-out for it'
}

$Tests['redirect-output'] = {
    # Main's plugin knows -oracle-out: each run writes into its own folder, main's data/oracle is
    # never touched (not even for the length of the run).
    $e = New-Env 'redirect' @{} -Redirect
    Set-Plan $e 'extra', 'crash sleep=4000'
    Submit $e 'rd-1' @{}
    Submit $e 'rd-2' @{}
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'rd-1' 'ok' 'ok' 1
    AssertEq $r.attempts[0].output 'own-folder' 'the run had its own folder'
    $runs = @(Get-Runs $e)
    $out1 = Join-Path $e.Queue 'out\rd-1\1'
    Assert ($runs[0] -match [regex]::Escape("-oracle-out=$out1")) "the game got -oracle-out ($($runs[0]))"
    $d = Join-Path $e.Queue 'done/rd-1'
    Assert ([IO.File]::ReadAllText((Join-Path $d 'enemy_Fake.jsonl')) -match '"line":"extra"') 'the recording is in done/'
    Assert (Test-Path (Join-Path $d 'ENEMY_Fake_DONE')) 'the marker is in done/'
    AssertEq ((@($r.otherFiles) | Sort-Object) -join ',') 'keep.jsonl,other.jsonl' 'the other files are in other/'
    AssertEq ([IO.File]::ReadAllText((Join-Path $d 'other/keep.jsonl'))) "rewritten by the run`n" 'other/keep.jsonl is the run''s'
    [void](Assert-Result $e 'rd-2' 'failed' 'crash' 1)
    Assert (Test-Path (Join-Path $e.Queue 'done/rd-2/attempts/1/enemy_Fake.jsonl')) "a failed run's files are in its attempt's folder"
    Assert (-not (Test-Path (Join-Path $e.Queue 'out/rd-1')) -and -not (Test-Path (Join-Path $e.Queue 'out/rd-2/1'))) 'the run folders are gone'
    Assert-MainIntact $e
    AssertEq (Get-Item -LiteralPath $e.Oracle).LastWriteTimeUtc.Ticks $e.OracleDirTime "nothing was created or removed in main's data/oracle"
}

$Tests['appended-log'] = {
    # An oracle without -oracle-out appends to main's trace.jsonl: the run keeps only the lines it
    # added (other/trace.jsonl.appended), main's file goes back whole.
    $e = New-Env 'appended'
    Set-Plan $e 'append'
    Submit $e 'ap-1' @{}
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'ap-1' 'ok' 'ok' 1
    $d = Join-Path $e.Queue 'done/ap-1/other'
    Assert (-not (Test-Path (Join-Path $d 'trace.jsonl'))) 'not the whole trace.jsonl'
    $app = Join-Path $d 'trace.jsonl.appended'
    Assert (Test-Path $app) 'the appended lines are kept'
    if (Test-Path $app) { Assert ([IO.File]::ReadAllText($app) -match '^\{"appended by":\d+\}\n$') "only what the run appended ($([IO.File]::ReadAllText($app)))" }
    AssertEq (@($r.otherFiles) -join ',') 'trace.jsonl.appended' 'result.json lists it'
    Assert-MainIntact $e
}

$Tests['main-files-protected'] = {
    $e = New-Env 'protected'
    # Main already has an older enemy_Fake.jsonl and marker; the run rewrites them and others.
    Write-Text (Join-Path $e.Oracle 'enemy_Fake.jsonl') "main's old recording`n"
    Write-Text (Join-Path $e.Oracle 'ENEMY_Fake_DONE') 'old marker'
    foreach ($f in Get-ChildItem $e.Oracle) { $f.LastWriteTime = (Get-Date).AddHours(-2) }
    $e.OracleState = Get-DirState $e.Oracle
    Set-Plan $e 'extra'
    Submit $e 'prot-1' @{}
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'prot-1' 'ok' 'ok' 1
    Assert-MainIntact $e
    $d = Join-Path $e.Queue 'done/prot-1'
    Assert ([IO.File]::ReadAllText((Join-Path $d 'enemy_Fake.jsonl')) -match '"line":"extra"') 'the new recording is in done/'
    AssertEq ([IO.File]::ReadAllText((Join-Path $d 'other/other.jsonl')).Length -gt 0) $true 'a file the request did not expect goes to done/<id>/other/'
    AssertEq ([IO.File]::ReadAllText((Join-Path $d 'other/keep.jsonl'))) "rewritten by the run`n" "a main file the run rewrote: the run's copy in other/"
    AssertEq ((@($r.otherFiles) | Sort-Object) -join ',') 'keep.jsonl,other.jsonl' 'result.json lists the other files'
}

$Tests['promote'] = {
    $e = New-Env 'promote'
    Write-Text (Join-Path $e.Oracle 'enemy_Fake.jsonl') "main's old recording`n"
    (Get-Item (Join-Path $e.Oracle 'enemy_Fake.jsonl')).LastWriteTime = [datetime]'2026-10-01 08:15:00'
    Set-Plan $e 'ok'
    Submit $e 'promo-1' @{ promote = $true }
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'promo-1' 'ok' 'ok' 1
    $d = Join-Path $e.Queue 'done/promo-1'
    AssertEq (Get-FileHash (Join-Path $e.Oracle 'enemy_Fake.jsonl')).Hash (Get-FileHash (Join-Path $d 'enemy_Fake.jsonl')).Hash 'the new recording replaced main''s'
    Assert (Test-Path (Join-Path $e.Oracle 'ENEMY_Fake_DONE')) "the marker is promoted too"
    $old = Join-Path $e.Backup 'enemy_Fake_10010815.jsonl'
    Assert (Test-Path $old) "main's old recording was backed up first"
    if (Test-Path $old) { AssertEq ([IO.File]::ReadAllText($old)) "main's old recording`n" 'with its content' }
    AssertEq ((@($r.promoted)) -join ',') 'enemy_Fake.jsonl,ENEMY_Fake_DONE' 'result.json lists the promoted files'
}

$Tests['startup-retry-then-ok'] = {
    $e = New-Env 'startup-retry'
    Set-Plan $e 'early', 'early', 'ok'
    Submit $e 'st-1' @{ retries = 2 }
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'st-1' 'ok' 'ok' 3
    AssertEq (@($r.attempts | ForEach-Object { $_.reason }) -join ',') 'startup,startup,ok' 'attempt reasons'
    Assert-MainIntact $e
    Assert (Test-Path (Join-Path $e.Queue 'done/st-1/attempts/1/LogOutput.log')) "a failed attempt's log is kept"
}

$Tests['startup-exhausted'] = {
    $e = New-Env 'startup-fail'
    Set-Plan $e 'early', 'early', 'early', 'ok'
    Submit $e 'st-2' @{ retries = 5 }   # (capped at MaxRetries = 2)
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'st-2' 'failed' 'startup' 3
    AssertEq (Get-Runs $e).Count 3 'three runs (1 + 2 retries)'
    Assert ($r.detail -match 'Steam or startup failure') "detail ($($r.detail))"
    Assert-MainIntact $e
}

$Tests['timeout'] = {
    $e = New-Env 'timeout'
    Set-Plan $e 'hang'
    Submit $e 'to-1' @{ timeout = 4 }
    $t0 = Get-Date
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'to-1' 'failed' 'timeout' 1
    AssertEq $r.attempts[0].exitCode 124 "the launcher's timeout exit code"
    Assert (((Get-Date) - $t0).TotalSeconds -lt 40) 'the timeout ended the run'
    Assert-MainIntact $e
}

$Tests['launcher-hangs'] = {
    # The launcher itself never returns: the runner's backstop (timeout + LauncherGraceSec) kills it.
    $fake = Join-Path $Scratch 't/hang_launcher.ps1'
    Write-Text $fake "param([string]`$Exe, [string]`$Arguments, [string]`$WorkingDirectory, [string]`$Priority, [int]`$TimeoutSec, [string]`$DesktopName)`r`nStart-Sleep -Seconds 600`r`n"
    $e = New-Env 'launcher-hangs' @{ Launcher = $fake; LauncherGraceSec = 2 }
    Submit $e 'lh-1' @{ timeout = 2 }
    $t0 = Get-Date
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'lh-1' 'failed' 'timeout' 1
    Assert (((Get-Date) - $t0).TotalSeconds -lt 30) 'the backstop ended the run'
    Assert $r.attempts[0].killed 'killed'
}

$Tests['crash'] = {
    $e = New-Env 'crash'
    Set-Plan $e 'crash sleep=4000'
    Submit $e 'cr-1' @{}
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'cr-1' 'failed' 'crash' 1
    AssertEq $r.attempts[0].exitCode -1073740791 'the crash exit code'
    Assert ($r.detail -match 'fake crash') "the oracle log's error is in the detail ($($r.detail))"
    Assert (Test-Path (Join-Path $e.Queue 'done/cr-1/attempts/1/enemy_Fake.jsonl')) "the half-written file is kept in the attempt's folder"
    Assert-MainIntact $e
}

$Tests['judging'] = {
    $e = New-Env 'judging'
    Write-Text (Join-Path $e.Oracle 'enemy_Stale.jsonl') "main's old`n"
    (Get-Item (Join-Path $e.Oracle 'enemy_Stale.jsonl')).LastWriteTime = (Get-Date).AddDays(-1)
    $e.OracleState = Get-DirState $e.Oracle
    Set-Plan $e 'nothing sleep=4000', 'nomarker sleep=4000', 'markeronly sleep=4000 marker=ENEMY_Stale_DONE', 'markeronly sleep=4000', 'empty sleep=4000', 'failed sleep=4000', 'ok code=3'
    Submit $e 'j-nothing' @{}
    Submit $e 'j-nomarker' @{}
    Submit $e 'j-stale' @{ args = '-oracle-enemy=Stale'; expects = @('enemy_Stale.jsonl') }
    Submit $e 'j-missing' @{}
    Submit $e 'j-empty' @{}
    Submit $e 'j-failed' @{}
    Submit $e 'j-okcode' @{}
    [void](Invoke-Runner $e -Once)
    [void](Assert-Result $e 'j-nothing' 'failed' 'no-done-marker' 1)
    [void](Assert-Result $e 'j-nomarker' 'failed' 'no-done-marker' 1)
    [void](Assert-Result $e 'j-stale' 'failed' 'stale-files' 1)
    [void](Assert-Result $e 'j-missing' 'failed' 'missing-files' 1)
    [void](Assert-Result $e 'j-empty' 'failed' 'empty-files' 1)
    [void](Assert-Result $e 'j-failed' 'failed' 'oracle-failed' 1)
    # (A non-zero exit after a complete recording is still a recording.)
    $r = Assert-Result $e 'j-okcode' 'ok' 'ok' 1
    AssertEq $r.attempts[0].exitCode 3 'exit code kept'
    Assert-MainIntact $e
}

$Tests['linger-after-marker'] = {
    $e = New-Env 'linger'
    Set-Plan $e 'linger'
    Submit $e 'lg-1' @{ timeout = 60 }
    $t0 = Get-Date
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'lg-1' 'ok' 'ok' 1
    Assert $r.attempts[0].killed 'the lingering game was killed'
    Assert ($r.detail -match 'killed') "detail says so ($($r.detail))"
    Assert (((Get-Date) - $t0).TotalSeconds -lt 40) 'killed after DoneGraceSec, not at the timeout'
    Assert-MainIntact $e
}

$Tests['invalid-requests'] = {
    $e = New-Env 'invalid'
    Write-Text (Join-Path $e.Queue 'requests/inv-json.json') '{ this is not json'
    Submit $e 'inv-visible' @{ args = '-oracle-enemy=Fake -oracle-visible' }
    Submit $e 'inv-expects' @{ expects = @() }
    Submit $e 'inv-other-id' @{} 'inv-id.json'
    Submit $e 'inv name' @{} 'inv name.json'
    Submit $e 'inv-branch' @{ worktree = $e.Root }   # (no oracle project there)
    [void](Invoke-Runner $e -Once)
    foreach ($id in 'inv-json', 'inv-visible', 'inv-expects', 'inv-id', 'inv-branch') { [void](Assert-Result $e $id 'failed' 'invalid-request' 0) }
    Assert (@(Get-ChildItem (Join-Path $e.Queue 'rejected') -Filter '*inv name.json').Count -eq 1) 'a file name that is no id goes to rejected/'
    AssertEq (Get-Runs $e).Count 0 'no game run for invalid requests'
}

$Tests['order-oldest-first'] = {
    $e = New-Env 'order'
    Set-Plan $e 'ok', 'ok', 'ok'
    $now = Get-Date
    Submit $e 'a-last' @{ submitted = $now.AddMinutes(-1).ToString('o') }
    Submit $e 'b-first' @{ submitted = $now.AddMinutes(-3).ToString('o') }
    Submit $e 'c-middle' @{ submitted = $now.AddMinutes(-2).ToString('o') }
    [void](Invoke-Runner $e -Once)
    $order = @('a-last', 'b-first', 'c-middle' | ForEach-Object { [pscustomobject]@{ id = $_; t = [datetime](Get-Result $e $_).started } } | Sort-Object t | ForEach-Object { $_.id })
    AssertEq ($order -join ',') 'b-first,c-middle,a-last' 'run order follows `submitted`'
}

$Tests['steam-start'] = {
    $e = New-Env 'steam-start' @{ SteamCheck = $true }
    Set-Plan $e 'ok'
    Submit $e 'sw-1' @{}
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'sw-1' 'ok' 'ok' 1
    Assert ($r.attempts[0].steam.started) 'the runner started Steam'
    AssertEq $r.attempts[0].steam.state 'Logged On' 'and waited for Logged On'
    $sp = @(Get-CimInstance Win32_Process -Filter "Name = 'recqtest_steam.exe'")
    AssertEq $sp.Count 1 'Steam is still running after the run'
    if ($sp.Count) {
        Assert ($sp[0].CommandLine -match ' -silent ') "started with -silent ($($sp[0].CommandLine))"
        AssertEq (Get-Process -Id $sp[0].ProcessId).PriorityClass 'Normal' 'Steam runs at Normal priority, not the runner''s'
    }
    Assert-MainIntact $e
}

$Tests['steam-never-connects'] = {
    $e = New-Env 'steam-never' @{ SteamCheck = $true; SteamConnectTimeoutSec = 3; SteamArgs = "-silent -never -log `"$(Join-Path $Scratch 't/steam-never/steam/connection_log.txt')`"" }
    Set-Plan $e 'ok'
    Submit $e 'sn-1' @{ retries = 1 }
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'sn-1' 'failed' 'steam' 2
    AssertEq (Get-Runs $e).Count 0 'the game never ran without Steam'
    Assert ($r.detail -match 'not logged on') "detail ($($r.detail))"
    Assert-MainIntact $e
}

$Tests['steam-dies-during-run'] = {
    $e = New-Env 'steam-dies' @{ SteamCheck = $true }
    # Steam is up and logged on before the queue starts.
    $sp = Start-Process (Join-Path $Bin 'recqtest_steam.exe') -ArgumentList "-silent -log `"$($e.SteamLog)`" -delay 0" -PassThru
    [void](Wait-Until { (Test-Path $e.SteamLog) -and ((Get-Content $e.SteamLog -Raw) -match 'Logged On') } 20 'fake Steam to log on')
    Set-Plan $e 'killsteam', 'ok'
    Submit $e 'sd-1' @{ retries = 2 }
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'sd-1' 'ok' 'ok' 2
    AssertEq $r.attempts[0].reason 'startup' 'the run that lost Steam counts as a startup failure'
    Assert (-not $r.attempts[0].steam.started) 'Steam was running for the first attempt'
    Assert ($r.attempts[1].steam.started) 'the runner restarted Steam before the retry'
    Assert-MainIntact $e
}

$Tests['foreign-game-waits'] = {
    $e = New-Env 'foreign'
    $other = (New-Item -ItemType Directory -Force (Join-Path $e.Root 'othercopy')).FullName
    Copy-Item (Join-Path $Bin 'recqtest_game.exe') $other
    Write-Text (Join-Path $other 'fakegame.plan') "hang`r`n"
    $fp = Start-Process (Join-Path $other 'recqtest_game.exe') -PassThru
    Set-Plan $e 'ok'
    Submit $e 'fg-1' @{}
    $bg = Start-RunnerBg $e
    Start-Sleep -Seconds 4
    AssertEq (Get-Runs $e).Count 0 'no run while another copy of the game is up'
    Assert ((Get-Content (Join-Path $e.Queue 'state/runner.log') -Raw) -match 'the game is already running') 'the runner says why it waits'
    Stop-Process -Id $fp.Id -Force
    [void](Wait-Until { Get-Result $e 'fg-1' } 60 'the request to run after the other game quit')
    Write-Text (Join-Path $e.Queue 'STOP') 'test'
    [void]$bg.Process.WaitForExit(30000)
    [void](Assert-Result $e 'fg-1' 'ok' 'ok' 1)
    Assert ($fp.HasExited) 'the foreign game was left alone until it quit'
}

$Tests['pause-stop-lock'] = {
    $e = New-Env 'pause'
    Set-Plan $e 'ok'
    Submit $e 'ps-1' @{}
    Write-Text (Join-Path $e.Queue 'PAUSE') 'test'
    $run = Invoke-Runner $e -Once
    AssertEq $run.Code 0 'paused -Once exits 0'
    Assert (Test-Path (Join-Path $e.Queue 'requests/ps-1.json')) 'a paused queue takes nothing'
    Remove-Item (Join-Path $e.Queue 'PAUSE')
    $bg = Start-RunnerBg $e
    [void](Wait-Until { Get-Result $e 'ps-1' } 60 'the request after resume')
    $second = Invoke-Runner $e -Once
    AssertEq $second.Code 3 'a second runner exits 3 while one holds the lock'
    $st = Invoke-Client 'status.ps1' @('-Config', $e.Config, '-Json')
    $sj = $st.Out | ConvertFrom-Json
    Assert $sj.runner.alive 'status: the runner is alive'
    AssertEq $sj.recent[0].id 'ps-1' 'status: recent result'
    $stop = Invoke-Client 'status.ps1' @('-Config', $e.Config, '-Stop')
    Assert ($bg.Process.WaitForExit(30000)) 'STOP ends the runner'
    AssertEq $bg.Process.ExitCode 0 'with exit code 0'
    $st = Invoke-Client 'status.ps1' @('-Config', $e.Config)
    Assert ($st.Out -match 'NOT RUNNING') "status shows it stopped ($($st.Out))"
}

$Tests['restart-restores-plugin'] = {
    # A runner died with a build swapped in: the next one puts main's plugin back before anything.
    $e = New-Env 'restore-plugin'
    $q = $e.Queue
    [void](New-Item -ItemType Directory -Force (Join-Path $q 'state'), (Join-Path $q 'main-plugin'))
    Copy-Item $e.Plugin (Join-Path $q 'main-plugin/MoonlighterOracle.dll')
    Write-Text $e.Plugin 'A LANE BUILD'
    $swap = [ordered]@{ id = 'x'; phase = 'run'; time = (Get-Date).ToString('o'); plugin = $e.Plugin; mainBackup = (Join-Path $q 'main-plugin/MoonlighterOracle.dll'); mainSha256 = $e.MainPluginSha.ToLowerInvariant(); swappedSha256 = (Get-FileHash $e.Plugin -Algorithm SHA256).Hash.ToLowerInvariant() }
    Write-Text (Join-Path $q 'state/swap.json') ($swap | ConvertTo-Json)
    $run = Invoke-Runner $e -Once
    AssertEq $run.Code 0 'runner exit code'
    Assert-MainIntact $e
    # A plugin that is neither (someone else wrote it during the swap): kept aside, main's back.
    Copy-Item $e.Plugin (Join-Path $q 'main-plugin/MoonlighterOracle.dll')
    Write-Text $e.Plugin 'SOMETHING ELSE'
    Write-Text (Join-Path $q 'state/swap.json') ($swap | ConvertTo-Json)
    [void](Invoke-Runner $e -Once)
    Assert-MainIntact $e
    Assert (@(Get-ChildItem (Join-Path $q 'main-plugin') -Filter 'unexpected-*.dll').Count -eq 1) 'the unexpected plugin is kept aside'
}

$Tests['restart-after-crash-midrun'] = {
    # The runner is killed while the game runs: the next one kills the game, restores main's
    # recordings, records the attempt as runner-interrupted and goes on with the retries.
    $e = New-Env 'crash-midrun' @{ DoneGraceSec = 120 }
    Set-Plan $e 'linger', 'ok'
    Submit $e 'cm-1' @{ retries = 1; timeout = 120 }
    $bg = Start-RunnerBg $e
    [void](Wait-Until { Test-Path (Join-Path $e.Oracle 'ENEMY_Fake_DONE') } 60 'the game to write its marker')
    Assert (Test-Path (Join-Path $e.Queue 'state/stash.json')) 'main data copy in place during the run'
    Stop-Process -Id $bg.Process.Id -Force   # (only the runner: the launcher and the game live on)
    Start-Sleep -Milliseconds 500
    Assert (@(Get-Process recqtest_game -ErrorAction SilentlyContinue).Count -eq 1) 'the game outlived the runner'
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'cm-1' 'ok' 'ok' 2
    AssertEq $r.attempts[0].reason 'runner-interrupted' 'the interrupted attempt'
    Assert (Test-Path (Join-Path $e.Queue 'done/cm-1/attempts/1/enemy_Fake.jsonl')) "the interrupted attempt's files are kept"
    Assert-MainIntact $e
}

$Tests['restart-after-crash-redirect'] = {
    # The same with a run in its own folder: the next runner moves what the run wrote into the
    # interrupted attempt's folder.
    $e = New-Env 'crash-redirect' @{ DoneGraceSec = 120 } -Redirect
    Set-Plan $e 'linger', 'ok'
    Submit $e 'cr-r' @{ retries = 1; timeout = 120 }
    $bg = Start-RunnerBg $e
    [void](Wait-Until { Test-Path (Join-Path $e.Queue 'out/cr-r/1/ENEMY_Fake_DONE') } 60 'the game to write its marker')
    Stop-Process -Id $bg.Process.Id -Force
    Start-Sleep -Milliseconds 500
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'cr-r' 'ok' 'ok' 2
    AssertEq $r.attempts[0].reason 'runner-interrupted' 'the interrupted attempt'
    Assert (Test-Path (Join-Path $e.Queue 'done/cr-r/attempts/1/enemy_Fake.jsonl')) "the interrupted attempt's files are kept"
    Assert (-not (Test-Path (Join-Path $e.Queue 'out/cr-r/1'))) 'its run folder is gone'
    AssertEq $r.attempts[1].output 'own-folder' 'the retry ran in its own folder'
    Assert-MainIntact $e
    AssertEq (Get-Item -LiteralPath $e.Oracle).LastWriteTimeUtc.Ticks $e.OracleDirTime "nothing was created or removed in main's data/oracle"
}

$Tests['attempt-error'] = {
    # The runner itself fails inside an attempt (here: a data/oracle file it can't read for the
    # copy): the attempt fails with runner-error, nothing is left half-done.
    $e = New-Env 'attempt-error'
    Set-Plan $e 'ok'
    Submit $e 'ae-1' @{}
    $lockFile = Join-Path $e.Oracle 'locked.jsonl'
    Write-Text $lockFile 'x'
    $fs = [IO.File]::Open($lockFile, 'Open', 'ReadWrite', 'None')
    try { [void](Invoke-Runner $e -Once) } finally { $fs.Dispose() }
    $r = Assert-Result $e 'ae-1' 'failed' 'runner-error' 1
    AssertEq (Get-Runs $e).Count 0 'no game run'
    Assert (-not (Test-Path (Join-Path $e.Queue 'state/stash.json'))) 'no data/oracle copy left'
    Remove-Item $lockFile
    $e.OracleState = Get-DirState $e.Oracle
    Assert-MainIntact $e
}

$Tests['runner-errors-give-up'] = {
    # An exception escapes the request after a good run (here: the backup folder is a file):
    # the request stays in running/, is resumed without running the game again, and after
    # MaxRunnerErrors it fails with runner-error.
    $e = New-Env 'runner-errors' @{ MaxRunnerErrors = 2 }
    Write-Text $e.Backup 'a file where the backup folder should be'
    Set-Plan $e 'ok', 'ok'
    Submit $e 're-1' @{ retries = 2 }
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 're-1' 'failed' 'runner-error' 1
    AssertEq $r.attempts[0].reason 'ok' 'the recording itself was fine'
    AssertEq @($r.runnerErrors).Count 2 'two runner errors recorded'
    AssertEq (Get-Runs $e).Count 1 'the game ran once (the resume did not run it again)'
    Assert (Test-Path (Join-Path $e.Queue 'done/re-1/enemy_Fake.jsonl')) 'the recording is kept'
    Assert-MainIntact $e
}

$Tests['client'] = {
    $e = New-Env 'client'
    $c = @('-Config', $e.Config)
    $sub = Invoke-Client 'request.ps1' (@('cl-1', '-OracleArgs', '-oracle-enemy=Fake', '-Expects', 'enemy_Fake.jsonl', '-NoWait', '-Timeout', '60') + $c)
    AssertEq $sub.Code 0 "submit -NoWait ($($sub.Out))"
    Assert ($sub.Out -match 'no runner is running') 'warns that no runner is running'
    $q = Get-Content -Raw (Join-Path $e.Queue 'requests/cl-1.json') | ConvertFrom-Json
    AssertEq $q.args '-oracle-enemy=Fake' 'request args'
    AssertEq $q.timeout 60 'request timeout'
    Assert ([bool]$q.submitted) 'request has a submitted time'
    $dup = Invoke-Client 'request.ps1' (@('cl-1', '-OracleArgs', '-oracle-enemy=Fake', '-Expects', 'enemy_Fake.jsonl', '-NoWait') + $c)
    AssertEq $dup.Code 2 'a queued id is refused'
    $bad = Invoke-Client 'request.ps1' (@('cl-bad', '-OracleArgs', '-oracle-enemy=Fake -oracle-visible', '-Expects', 'enemy_Fake.jsonl', '-NoWait') + $c)
    AssertEq $bad.Code 2 "an invalid request is refused ($($bad.Out))"
    Assert (-not (Test-Path (Join-Path $e.Queue 'requests/cl-bad.json'))) 'and not written'
    $w = Invoke-Client 'request.ps1' (@('cl-1', '-Wait', '-WaitTimeoutSec', '2', '-PollSec', '1') + $c)
    AssertEq $w.Code 124 'waiting without a runner times out with 124'
    $cancel = Invoke-Client 'request.ps1' (@('cl-1', '-Cancel') + $c)
    AssertEq $cancel.Code 0 'cancel a queued request'
    Assert (-not (Test-Path (Join-Path $e.Queue 'requests/cl-1.json'))) 'cancelled request is gone from requests/'
    $none = Invoke-Client 'request.ps1' (@('cl-1', '-Wait') + $c)
    AssertEq $none.Code 3 'waiting on an unknown id: 3'
    # A worktree given as Git Bash writes it (/c/Users/...): stored as a Windows path, branch filled in.
    $wt = New-FakeWorktree $e 'client'
    $posix = '/' + $wt.Substring(0, 1).ToLowerInvariant() + ($wt.Substring(2) -replace '\\', '/')
    $sw = Invoke-Client 'request.ps1' (@('cl-wt', '-Worktree', $posix, '-OracleArgs', '-oracle-enemy=Fake', '-Expects', 'enemy_Fake.jsonl', '-NoWait') + $c)
    AssertEq $sw.Code 0 "submit with a Git Bash path $posix ($($sw.Out))"
    if (Test-Path (Join-Path $e.Queue 'requests/cl-wt.json')) {
        $q2 = Read-JsonFile (Join-Path $e.Queue 'requests/cl-wt.json')
        AssertEq $q2.worktree ([IO.Path]::GetFullPath($wt)) 'the worktree as a Windows path'
        AssertEq $q2.branch 'agent-test' 'its branch'
    }
    [void](Invoke-Client 'request.ps1' (@('cl-wt', '-Cancel') + $c))
    # Submit and wait with a runner up.
    Set-Plan $e 'ok', 'early', 'early', 'early', 'ok'
    $bg = Start-RunnerBg $e
    $ok = Invoke-Client 'request.ps1' (@('cl-2', '-OracleArgs', '-oracle-enemy=Fake', '-Expects', 'enemy_Fake.jsonl', '-PollSec', '1') + $c) 120
    AssertEq $ok.Code 0 "submit and wait: ok ($($ok.Out))"
    Assert ($ok.Out -match [regex]::Escape((Join-Path $e.Queue 'done\cl-2\enemy_Fake.jsonl'))) 'prints where the recording is'
    $fail = Invoke-Client 'request.ps1' (@('cl-3', '-OracleArgs', '-oracle-enemy=Fake', '-Expects', 'enemy_Fake.jsonl', '-PollSec', '1') + $c) 120
    AssertEq $fail.Code 1 "submit and wait: failed ($($fail.Out))"
    Assert ($fail.Out -match 'startup') 'prints the reason'
    $reuse = Invoke-Client 'request.ps1' (@('cl-2', '-OracleArgs', '-oracle-enemy=Fake', '-Expects', 'enemy_Fake.jsonl', '-NoWait') + $c)
    AssertEq $reuse.Code 2 'a used id is refused without -Replace'
    $replace = Invoke-Client 'request.ps1' (@('cl-2', '-OracleArgs', '-oracle-enemy=Fake', '-Expects', 'enemy_Fake.jsonl', '-Replace', '-PollSec', '1', '-Json') + $c) 120
    AssertEq $replace.Code 0 "-Replace runs it again ($($replace.Out))"
    Assert (@(Get-ChildItem (Join-Path $e.Queue 'done/_old') -Directory -Filter 'cl-2-*').Count -eq 1) 'the old result moved to done/_old'
    $j = $replace.StdOut | ConvertFrom-Json
    AssertEq $j.status 'ok' '-Json prints result.json'
    Write-Text (Join-Path $e.Queue 'STOP') 'test'
    [void]$bg.Process.WaitForExit(30000)
    Assert-MainIntact $e
}

$Tests['start-detached'] = {
    # start.ps1 creates the runner through WMI (nobody's child) and it runs games on the hidden desktop.
    $e = New-Env 'start'
    Set-Plan $e 'ok'
    $s = Invoke-Client 'start.ps1' @('-Config', $e.Config)
    AssertEq $s.Code 0 "start.ps1 ($($s.Out))"
    $again = Invoke-Client 'start.ps1' @('-Config', $e.Config)
    Assert ($again.Out -match 'already running') 'a second start does nothing'
    $hb = Read-JsonFile (Join-Path $e.Queue 'state/runner.json')   # (retries while the runner replaces it)
    $proc = Get-CimInstance Win32_Process -Filter "ProcessId = $($hb.pid)"
    Assert ($proc -and $proc.CommandLine -match 'runner\.ps1') 'the runner process exists'
    $ok = Invoke-Client 'request.ps1' @('st-1', '-OracleArgs', '-oracle-enemy=Fake', '-Expects', 'enemy_Fake.jsonl', '-PollSec', '1', '-Config', $e.Config) 120
    AssertEq $ok.Code 0 "a request through the detached runner ($($ok.Out))"
    [void](Invoke-Client 'status.ps1' @('-Config', $e.Config, '-Stop'))
    [void](Wait-Until { -not (Get-Process -Id $hb.pid -ErrorAction SilentlyContinue) } 30 'the detached runner to stop')
    Assert-MainIntact $e
}

# --- dotnet build tests ---

$Tests['build-swap-restore'] = {
    $e = New-Env 'build-ok'
    $wt = New-FakeWorktree $e 'lane-build' -Redirect
    Set-Plan $e 'ok'
    Submit $e 'b-1' @{ worktree = $wt; branch = 'agent-test' }
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'b-1' 'ok' 'ok' 1
    Assert-MainIntact $e 'after a worktree build'
    $dll = Join-Path $e.Queue 'builds/b-1/out/MoonlighterOracle.dll'
    Assert (Test-Path $dll) 'the build is in builds/<id>/out/'
    $content = [IO.File]::ReadAllText((Join-Path $e.Queue 'done/b-1/enemy_Fake.jsonl')) | ConvertFrom-Json
    AssertEq $content.plugin.ToUpperInvariant() (Get-FileHash $dll -Algorithm SHA256).Hash 'the game ran with the lane''s build'
    AssertEq (@($r.oracle.build.removedTargets) -join ',') 'DeployToGame,AlsoDeploy' 'both deploy targets removed'
    AssertEq $r.oracle.branch 'agent-test' 'branch recorded'
    Assert ($r.oracle.commit -match '^[0-9a-f]{40}$') 'commit recorded'
    Assert (Test-Path (Join-Path $e.Queue 'done/b-1/build.log')) 'build.log in done/'
    Assert (Test-Path (Join-Path $e.Queue 'refgame/BepInEx/core/FakeRef.dll')) 'the build referenced the mirror of the game''s dlls'
    Assert (-not (Test-Path (Join-Path $e.Queue 'refgame/BepInEx/plugins/MoonlighterOracle.dll'))) 'nothing deployed even into the mirror'
    AssertEq $r.attempts[0].output 'own-folder' "the lane's build knows -oracle-out (main's does not): the run had its own folder"
}

$Tests['build-failures'] = {
    $e = New-Env 'build-fail'
    $wt = New-FakeWorktree $e 'broken' -Broken
    Set-Plan $e 'ok'
    Submit $e 'bf-1' @{ worktree = $wt }
    Submit $e 'bf-branch' @{ worktree = $wt; branch = 'agent-other' }
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'bf-1' 'failed' 'build-failed' 0
    Assert ($r.detail -match 'error CS') "the compiler error is in the detail ($($r.detail))"
    [void](Assert-Result $e 'bf-branch' 'failed' 'branch-mismatch' 0)
    AssertEq (Get-Runs $e).Count 0 'no game run'
    Assert-MainIntact $e
}

$Tests['build-writes-game'] = {
    # A target the deploy filter can't see writes the plugin anyway: caught, main's goes back.
    $e = New-Env 'build-sneaky'
    $wt = New-FakeWorktree $e 'sneaky' -Sneaky
    Set-Plan $e 'ok'
    Submit $e 'bs-1' @{ worktree = $wt }
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'bs-1' 'failed' 'build-failed' 0
    Assert ($r.detail -match 'wrote into the game') "detail ($($r.detail))"
    AssertEq (Get-Runs $e).Count 0 'no game run'
    Assert-MainIntact $e
}

$Tests['crash-with-swap'] = {
    # The runner is killed while a lane's build is swapped in and the game runs.
    $e = New-Env 'crash-swap' @{ DoneGraceSec = 120 }
    $wt = New-FakeWorktree $e 'crash-swap'
    Set-Plan $e 'linger', 'ok'
    Submit $e 'cs-1' @{ worktree = $wt; retries = 1; timeout = 120 }
    $bg = Start-RunnerBg $e
    [void](Wait-Until { Test-Path (Join-Path $e.Oracle 'ENEMY_Fake_DONE') } 120 'the game to write its marker')
    $swapPath = Join-Path $e.Queue 'state/swap.json'
    Assert (Test-Path $swapPath) 'the swap marker is there during the run'
    Assert ((Get-FileHash $e.Plugin -Algorithm SHA256).Hash -ne $e.MainPluginSha) "the lane's build is in the game during the run"
    Stop-Process -Id $bg.Process.Id -Force
    Start-Sleep -Milliseconds 500
    [void](Invoke-Runner $e -Once)
    $r = Assert-Result $e 'cs-1' 'ok' 'ok' 2
    AssertEq $r.attempts[0].reason 'runner-interrupted' 'the interrupted attempt'
    Assert-MainIntact $e 'after the crash'
    AssertEq (@(Get-ChildItem (Join-Path $e.Queue 'builds') -Directory).Count) 1 'the build was reused, not rebuilt'
}

$Tests['build-real-oracle'] = {
    # The real oracle project (this worktree's) builds through the runner's build step against a
    # copy of the real game's reference dlls; the deployed plugin is untouched.
    $realPlugin = Join-Path $RealGame 'BepInEx/plugins/MoonlighterOracle.dll'
    if (-not (Test-Path $realPlugin)) { Write-Host '    (no real game copy here; skipped)'; return }
    $before = (Get-FileHash $realPlugin -Algorithm SHA256).Hash
    $e = New-Env 'build-real'
    foreach ($rel in 'BepInEx/core', 'Moonlighter_Data/Managed') {
        $rc = Invoke-Native 'robocopy.exe' @((Join-Path $RealGame $rel), (Join-Path $e.Game $rel), '*.dll', '/R:1', '/W:1', '/NP', '/NFL', '/NDL', '/NJH', '/NJS') 300
        Assert ($rc.Code -lt 8) "copying the game's $rel"
    }
    Set-RecqConfig (Read-RecqConfig $e.Config)
    Initialize-RecqQueue
    $b = Invoke-OracleBuild 'real' $Repo
    Assert $b.ok "the real oracle builds ($($b.reason))"
    AssertEq (@($b.removedTargets) -join ',') 'DeployToGame' 'its deploy target removed'
    AssertEq (Get-FileHash $realPlugin -Algorithm SHA256).Hash $before 'the real deployed plugin is unchanged'
    Assert (Test-OracleSupportsOut $b.dll) 'the oracle built from this branch knows -oracle-out'
    Write-Host "    (the deployed plugin knows -oracle-out: $(Test-OracleSupportsOut $realPlugin))"
    Assert-MainIntact $e
}

# ---------------------------------------------------------------------------------------------

$dotnetTests = @('build-swap-restore', 'build-failures', 'build-writes-game', 'crash-with-swap', 'build-real-oracle')
$failed = New-Object System.Collections.ArrayList
$t00 = Get-Date
foreach ($name in $Tests.Keys) {
    if ($Only.Count -and $Only -notcontains $name) { continue }
    if ($SkipDotnet -and $dotnetTests -contains $name) { Write-Host "SKIP $name (-SkipDotnet)"; continue }
    $script:Failures.Clear()
    $t0 = Get-Date
    Write-Host "RUN  $name"
    try { & $Tests[$name] }
    catch { [void]$script:Failures.Add("exception: $($_.Exception.Message) at $($_.InvocationInfo.PositionMessage)"); Write-Host "    EXCEPTION: $($_.Exception.Message)`n$($_.ScriptStackTrace)" -ForegroundColor Red }
    finally {
        Get-Process recqtest_game, recqtest_steam -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        Get-CimInstance Win32_Process -Filter "Name = 'powershell.exe'" | Where-Object { $_.CommandLine -match 'runner\.ps1' -and $_.CommandLine -match [regex]::Escape($Scratch) } |
            ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    }
    $sec = [int]((Get-Date) - $t0).TotalSeconds
    if ($script:Failures.Count) { [void]$failed.Add($name); Write-Host "FAIL $name ($sec s): $($script:Failures.Count) failure(s)" -ForegroundColor Red }
    else { Write-Host "PASS $name ($sec s)" -ForegroundColor Green }
}
Write-Host ''
Write-Host ("{0} failed of {1} run, {2} s" -f $failed.Count, @($Tests.Keys | Where-Object { -not $Only.Count -or $Only -contains $_ }).Count, [int]((Get-Date) - $t00).TotalSeconds)
if ($failed.Count) { Write-Host ('failed: ' + ($failed -join ', ')) -ForegroundColor Red }
if (-not $Keep -and -not $failed.Count) { Remove-Item -LiteralPath $Scratch -Recurse -Force -ErrorAction SilentlyContinue }
exit $failed.Count
