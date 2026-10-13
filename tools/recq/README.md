# tools/recq: the unattended recording queue

Porting lanes need oracle recordings of the real game, and only one game can run at a time. The
queue lets any agent ask for a recording and wait for it, with nobody starting runs by hand: an
agent writes a request, one long-lived runner takes the requests one at a time (oldest first),
builds the request's worktree's oracle, runs the game on a hidden desktop, checks what it wrote,
retries failures, and leaves the files and a `result.json` in `done/<id>/`.

The plan this implements is [docs/recording_queue_plan.md](../../docs/recording_queue_plan.md).

| File | What it is |
|---|---|
| `start.ps1` | starts the runner in the background (no window, Idle priority, nobody's child) |
| `runner.ps1` | the runner itself (`start.ps1` runs it; `-Once` exits when the queue is empty) |
| `request.ps1` | submit a request and wait for its result (agents) |
| `status.ps1` | the queue at a glance; `-Pause`, `-Resume`, `-Stop` |
| `recq.psm1` | everything the scripts share |
| `tests/run_tests.ps1` | the tests: every failure path against a fake game and a fake Steam |

All of it is Windows PowerShell 5.1 (Git's shell mangles `start`'s `/min` and `/low`).

## For the integrator

Start the runner (once per logon; it keeps running when the shell that started it closes):

```
powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\legen\Documents\moonlighter\tools\recq\start.ps1
```

It does nothing if a runner already holds the queue. What it touches:

- the game copy `C:\Users\legen\Documents\moonlighter\Moonlighter` (runs it, and swaps a lane's
  oracle build into `BepInEx\plugins` for the length of a run),
- Steam (starts `steam.exe -silent` when it is not running),
- `C:\Users\legen\Documents\moonlighter-recq` (the queue folder),
- `C:\Users\legen\Documents\moonlighter-oracle-backup` (backups of every recording, never
  overwritten),
- main's `data\oracle` only for `"promote": true` requests, and, while main's deployed oracle
  does not know `-oracle-out` yet, for the length of a run (see *Where the game writes*).

Watch it, and control it:

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\recq\status.ps1           # or -Json
powershell -NoProfile -ExecutionPolicy Bypass -File tools\recq\status.ps1 -Pause    # finish the current request, take no new one
powershell -NoProfile -ExecutionPolicy Bypass -File tools\recq\status.ps1 -Resume
powershell -NoProfile -ExecutionPolicy Bypass -File tools\recq\status.ps1 -Stop     # exit after the current attempt
```

The runner's log is `moonlighter-recq\state\runner.log`; its heartbeat (pid, what it is doing)
is `state\runner.json`.

- **Deploying main's oracle** (building `oracle\MoonlighterOracle` in main copies it into the
  game): `-Pause` first and wait until `status.ps1` shows nothing running, then build, then
  `-Resume`. A plugin that changes while a lane's build is swapped in is taken for a stray write:
  it is put aside in `main-plugin\unexpected-<time>.dll`, main's goes back, and `status.ps1` warns.
- **Playing or testing the game by hand**: the runner never starts a game while any
  `Moonlighter.exe` runs; it waits until it quits. `-Pause` keeps it from starting one after.
- **After a reboot or a crash of the runner**: start it again. Before anything else it kills a
  game it left running, puts main's plugin back if a lane's build was swapped in
  (`state\swap.json`), puts main's `data\oracle` back if a run had it (`state\stash.json`), and
  records the cut-off attempt as `runner-interrupted`; the request goes on with its retries.
- A different setup (paths, timings) goes in a JSON config: `start.ps1 -Config <file>` (and the
  same `-Config` for `status.ps1` and `request.ps1`). See *Config*.

## For agents: ask for a recording

Agents never run the game, the oracle or Steam; they write requests. From a lane's worktree
(Git Bash), with the oracle built from that worktree (`-Worktree .`):

```
powershell -NoProfile -ExecutionPolicy Bypass -File C:/Users/legen/Documents/moonlighter/tools/recq/request.ps1 \
    butler-1 -Worktree . -OracleArgs -oracle-enemy=GolemButler -Expects enemy_GolemButler.jsonl -Timeout 900 \
    -WaitTimeoutSec 540
```

It checks the request, queues it, and waits for the result, printing the queue position and the
attempts as they go. A tool call can't wait for ever, so wait in pieces: `-WaitTimeoutSec 540`
returns with exit code 124 while the request is still queued or running, and

```
powershell -NoProfile -ExecutionPolicy Bypass -File C:/Users/legen/Documents/moonlighter/tools/recq/request.ps1 butler-1 -Wait -WaitTimeoutSec 540
```

waits again (as often as needed). `-NoWait` only submits. `-Cancel` withdraws a request that has
not started. `-Json` prints `result.json` (and nothing else on stdout) when it is done.

Exit codes: `0` recorded, `1` failed (the reason is printed and in `result.json`), `2` refused
(invalid request, id in use), `3` no such request, `124` not finished yet.

When it is recorded, the files are in `C:\Users\legen\Documents\moonlighter-recq\done\<id>\`
(the paths are printed). Copy them into your worktree's `data\oracle` (gitignored) for your tests.
They do not go into main's `data\oracle` unless the request says `-Promote`, which is for the
integrator's re-recordings.

Options of `request.ps1`: `<id>` (first argument: letters, digits, `.`, `_`, `-`; a new one per
request, e.g. `butler-1`, `butler-2`), `-OracleArgs`, `-Expects` (one or more, or comma-separated),
`-Worktree`, `-Branch` (default: the worktree's; the runner refuses the request if the worktree is
on another branch by then), `-Oracle main|worktree`, `-Timeout <s>`, `-Done <marker>`,
`-Retries <0..2>`, `-Promote`, `-Note`, `-Replace` (reuse an id: its old result moves to
`done\_old\`).

## The request: `requests/<id>.json`

`request.ps1` writes it; anything that writes the same JSON works too (write it under another
name and rename it, so the runner never reads half a file).

```json
{"id": "butler-1", "branch": "agent-butler", "worktree": "C:/Users/legen/Documents/ml-wt/butler",
 "args": "-oracle-enemy=GolemButler", "expects": ["enemy_GolemButler.jsonl"], "timeout": 900}
```

| Field | |
|---|---|
| `id` | must equal the file name without `.json` |
| `args` | the oracle mode and its options. Not `-oracle-visible`, `-screen-*` or `-oracle-out`; `-oracle-quit` is added |
| `expects` | the file names the run must write (plain names, as the oracle writes them into its output folder) |
| `done` | the oracle's done-marker. Default: from the mode in `args` (`-oracle-enemy=X` gives `ENEMY_X_DONE`, `-oracle-walk` `WALK_DONE`, `-oracle-slime` `SLIME_DONE`, `-oracle-tangle` `TANGLE_DONE`, `-oracle-polynav` `POLYNAV_DONE`, `-oracle-harvest` `HARVEST_DONE`, `-oracle-run` `DONE`) |
| `worktree` | a lane's worktree: its `oracle\MoonlighterOracle` is built and runs instead of main's |
| `branch` | checked against the worktree's branch |
| `oracle` | `worktree` (default when `worktree` is given) or `main` (the deployed plugin) |
| `timeout` | seconds for the game run, startup included (default 900, at most 7200) |
| `retries` | 0 to 2 (default 2) |
| `promote` | `true`: copy the recorded files into main's `data\oracle` too (main's old file is backed up first) |
| `submitted`, `submittedBy`, `note` | for people; `submitted` orders the queue (else the file's time) |

## The result: `done/<id>/`

```
done/<id>/result.json       written last: wait for this file
done/<id>/<expects...>      the recorded files and the done-marker (when ok)
done/<id>/LogOutput.log     BepInEx's log of the last attempt
done/<id>/build.log         the oracle build (worktree requests)
done/<id>/other/            anything else the run wrote (trace.jsonl, cave_game.jsonl, ...)
done/<id>/attempts/<n>/     each attempt's LogOutput.log (and Player.log, launcher.log), and
                            what a failed attempt wrote
```

`result.json`:

| Field | |
|---|---|
| `status` | `ok` or `failed` |
| `reason`, `detail` | why (the last attempt's reason; see below) |
| `files` | `{name, bytes, sha256, written}` of each expected file |
| `doneMarker` | `{name, text}` |
| `otherFiles` | names in `other/` |
| `attempts` | one object per attempt: `n`, `started`, `finished`, `reason`, `detail`, `steam` (the check before it), `output` (`own-folder` or `data-oracle`), `args` (the game's), `exitCode`, `exitedAfterSec`, `killed`, `missing`, `stale`, `empty`, `doneMarker`, `oracleErrors` (error lines of the log), `collected` |
| `oracle` | `source` (`main` or `worktree`), the plugin's `sha256`; for a worktree its `branch`, `commit`, `dirty` (uncommitted oracle changes are built too) and `build` |
| `backups` | the backup copies of the recorded files |
| `promoted` | what went into main's `data\oracle` |
| `request` | the request as run (defaults filled in); `requestSha256`, `submitted`, `started`, `finished`, `seconds`, `runner` |

Reasons:

| Reason | |
|---|---|
| `ok` | every expected file and the done-marker written by this run, none empty, no `FAILED` marker |
| `invalid-request` | the request is not valid (no attempt) |
| `branch-mismatch` | the worktree is on another branch than the request says (no attempt) |
| `build-failed` | the worktree's oracle did not build, or its build wrote into the game folder (no attempt) |
| `steam` | Steam was not running and logged on after 300 s, started or not |
| `timeout` | no done-marker by `timeout`; the game was killed |
| `startup` | the game quit within 30 s without writing anything: a Steam or startup failure |
| `oracle-failed` | the oracle wrote `FAILED` |
| `crash` | the game exited with a non-zero code without its done-marker |
| `no-done-marker` | the game exited without its done-marker |
| `missing-files`, `stale-files`, `empty-files` | the done-marker is there but an expected file is not, is older than the run, or is empty |
| `runner-interrupted` | the runner died during the attempt (the next runner records it) |
| `runner-error` | the runner itself failed (a file it could not read, a full disk...); after 3 such errors on one request it gives up on it |
| `plugin-restore-failed` | main's plugin could not be put back; the runner starts no game until it is |

## What a run does, and what happens when it fails

For each request: check it, build its oracle (worktree requests), then up to 1 + `retries`
attempts:

1. **Wait** while any `Moonlighter.exe` runs (one game at a time).
2. **Steam**: `steam.exe` must be running and logged on. If it is not running, the runner starts
   it (`cmd /c start "" /NORMAL steam.exe -silent`, so it neither inherits the runner's Idle
   priority nor any of its handles) and waits up to 300 s for it to log on, then 15 s more. Logged
   on means the last state line of `Steam\logs\connection_log.txt` (after the start, when the
   runner started it) is `[Logged On, ...]`; a bare `[Connected` also shows up in failed logons.
   Not logged on in time: the attempt fails with `steam`.
3. **Plugin swap** (worktree requests): main's `MoonlighterOracle.dll` is copied to
   `main-plugin\` and `state\swap.json` written, then the lane's build is copied in. Main's goes
   back after the run in a `finally`, and verified by SHA-256; a runner that died before that
   puts it back when it starts again.
4. **Run** through `tools\oracle_launch.ps1`: the game on a hidden desktop (it never shows on the
   screen or in a stream), windowed 640x360, Normal priority (Idle caused frame hitches that broke
   recordings), the request's args plus `-oracle-quit`, and the request's timeout (the launcher
   kills the game and exits 124). The runner kills the game when it is still up 60 s after its
   done-marker, and kills the launcher if it outlives the timeout by 60 s.
5. **Judge**: every expected file exists, was written after the run started and is not empty;
   the done-marker was written after the run started; no `FAILED`. Anything else fails the attempt
   with the first reason that fits, in the order of the table above. A run that quits within 30 s
   with nothing written is `startup` (the 2026-10-13 night: Steam closed and every run "finished"
   with nothing).
6. **Collect**: an ok run's expected files and marker go to `done\<id>\`, a failed attempt's to
   `attempts\<n>\`, anything else to `other\`.
7. **Retry** after 30 s, Steam checked (and started) again first.

When the request is ok, each recorded file is **backed up** to
`moonlighter-oracle-backup\<name>_MMDDHHMM.<ext>` (by the file's write time). A backup is never
overwritten: the same content is not copied twice, and another recording from the same minute gets
`_2`, `_3`... (a plain `cp -u` once overwrote the only copy of the first slime recording).

### Where the game writes

The oracle writes into `data\oracle` of the main checkout unless it is given `-oracle-out=<dir>`
(OraclePlugin.cs, on this branch). The runner looks for that option in the plugin that will run
(the string literal in the DLL):

- **It knows `-oracle-out`**: each attempt gets its own folder, `moonlighter-recq\out\<id>\<n>\`.
  Main's `data\oracle` is never touched.
- **It does not** (main's deployed plugin until this branch is merged and deployed, and lanes
  branched before that): the oracle writes into main's `data\oracle`. The runner copies the folder
  to `stash\current\` first (`state\stash.json`), and after the run moves everything the run
  created or changed into the request's folders and puts main's files back as they were (content
  and write time). For a log the oracle appends to (`trace.jsonl`, `cave_game.jsonl`) only the
  added lines are kept, as `other\<name>.appended`. During the run main's files are the run's: a
  test that reads main's `data\oracle` at that moment can see them.

### Per-request oracle builds

The worktree's `oracle\MoonlighterOracle` is copied (without `bin`, `obj`) to
`builds\<id>\src\`, its deploy targets are removed (`DeployToGame`, and any target that mentions
`BepInEx\plugins` or `GameDir`), and it is built with `dotnet build -c Release -o builds\<id>\out
-p:GameDir=<queue>\refgame -m:1` at the runner's Idle priority. `refgame\` is a copy of the game's
reference DLLs (`BepInEx\core`, `Moonlighter_Data\Managed`), so the build never sees the game
folder. Main's plugin is guarded during the build as during a run: if the build writes it anyway,
main's goes back and the request fails with `build-failed`. The build is made once per request
and reused by its retries (and after a crash of the runner).

## The queue folder

```
moonlighter-recq/
  requests/<id>.json   pending, oldest first
  running/<id>.json    the request being run
  done/<id>/           results; done/_old/ earlier results of reused ids
  builds/<id>/         oracle builds (src/, out/, build.log)
  out/<id>/<n>/        a run's own output folder while it runs
  refgame/             the game's reference DLLs for builds
  main-plugin/         main's plugin while it is swapped out; unexpected-*.dll put aside
  stash/               main's data/oracle during a run of an older oracle
  state/               runner.json (heartbeat), runner.log, runner.lock, current.json (the running
                       request's attempts), swap.json and stash.json (the guards)
  rejected/            request files whose name is no valid id; cancelled/ withdrawn requests
  PAUSE, STOP          written by status.ps1
```

## Config

`-Config <file.json>` changes settings; the built-in defaults are the real setup. A config must
give every path itself (`QueueDir`, `GameDir`, `GameExe`, `OracleOutDir`, `BackupDir`, `SteamExe`,
`SteamLog`), so a test config can never fall back onto the real game, its plugin or main's
recordings, unless it says `"UseDefaultPaths": true`. The other settings (defaults in
`Get-RecqDefaultConfig`, recq.psm1): `PlayerLog`, `Launcher`, `PowerShellExe`, `DesktopName`,
`GamePriority` (Normal), `SteamCheck` (true), `SteamArgs` (`-silent`), `SteamProcessName`,
`SteamConnectedStates` (`Logged On`), `SteamConnectTimeoutSec` (300), `SteamSettleSec` (15), `Git`,
`Dotnet`, `OracleProjectRelPath`, `BuildTimeoutSec` (900), `DefaultTimeoutSec` (900),
`MaxTimeoutSec` (7200), `MaxRetries` (2), `StartupWindowSec` (30), `DoneGraceSec` (60),
`LauncherGraceSec` (60), `RetryDelaySec` (30), `PollSec` (15), `RunnerPriority` (Idle),
`FailedMarkers` (`FAILED`), `RedirectOutput` (`auto`, or `never` to always use main's data/oracle),
`MinRequestAgeSec`, `MaxRunnerErrors` (3).

## Tests

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\recq\tests\run_tests.ps1
    [-Only name,name] [-SkipDotnet] [-Scratch <dir>] [-Keep]
```

About 5 minutes; the exit code is the number of failed tests. One test run at a time: each test
ends by killing every fake game, fake Steam and runner of its scratch folder. They never touch the real game, its plugin, Steam
or main's `data\oracle`: each test makes a throwaway world in the scratch folder (default
`%TEMP%\recq-tests`) with a fake game (`tests\fakes\FakeGame.cs`, compiled on the fly: it writes
files, crashes, hangs, quits early, lingers, appends, kills Steam, as a plan file tells it), a fake
Steam (`FakeSteam.cs`: writes `connection_log.txt` lines like Steam's, logs on after a delay or
never), throwaway git worktrees with fake oracle projects, and a config pointing at all of it. The
games run through the real `tools\oracle_launch.ps1` on a hidden desktop named `RecqTest`. Only
`build-real-oracle` reads the real game folder: it copies the reference DLLs to build this
branch's oracle, and checks the deployed plugin is unchanged. `-SkipDotnet` leaves out the five
tests that run `dotnet build`.

| Test | Covers |
|---|---|
| `unit-*` | Steam log parsing, request checks and done-marker inference, `-oracle-out` detection, deploy-target removal, backup names |
| `ok-main-oracle` | a whole request with main's oracle: files, marker, log, result, backup, window args, `-oracle-quit` |
| `redirect-output` | an oracle with `-oracle-out`: the run's own folder, `other/`, a failed run's files, main's `data\oracle` untouched |
| `appended-log` | an older oracle appending to `trace.jsonl`: only the added lines kept |
| `main-files-protected`, `promote` | main's recordings back after a run of an older oracle; promotion with a backup of main's old file |
| `startup-retry-then-ok`, `startup-exhausted` | quick exits without files: `startup`, retried, capped at 2 retries |
| `timeout`, `launcher-hangs` | the launcher's timeout (124); the runner's backstop when the launcher itself hangs |
| `crash`, `judging` | crash, no marker, stale, missing and empty files, `FAILED`, a non-zero exit after a full recording |
| `linger-after-marker` | a game that does not quit after its marker |
| `invalid-requests`, `order-oldest-first` | refused requests; queue order |
| `steam-start`, `steam-never-connects`, `steam-dies-during-run` | Steam started with `-silent` at Normal priority; never logging on; Steam closing mid-queue and restarted before the retry |
| `foreign-game-waits` | no run while another copy of the game is up |
| `pause-stop-lock`, `start-detached`, `client` | PAUSE, STOP, one runner per queue, status; the detached start; submit, wait, cancel, reuse |
| `restart-restores-plugin`, `restart-after-crash-midrun`, `restart-after-crash-redirect`, `crash-with-swap` | the runner killed mid-run (with and without a swapped plugin): main's plugin and recordings back, the attempt recorded, the request finished |
| `attempt-error`, `runner-errors-give-up` | the runner's own errors |
| `build-swap-restore`, `build-failures`, `build-writes-game`, `build-real-oracle` | per-request builds: the swap, compile errors, branch mismatch, a build that writes into the game anyway, the real oracle |
