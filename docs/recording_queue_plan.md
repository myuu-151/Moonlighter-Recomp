# Unattended recording queue: plan

## Status: built (tools/recq)

All three pieces are built and tested in `tools/recq/` (how to use it:
[tools/recq/README.md](../tools/recq/README.md)):

- `start.ps1` starts the runner (`runner.ps1`) in the background: no window, Idle priority,
  created through WMI so it outlives the shell that started it. `status.ps1` shows the queue and
  writes the PAUSE / STOP control files. Agents use `request.ps1` to submit a request and wait for
  `done/<id>/result.json` in pieces that fit a tool call.
- Failure handling as planned: Steam checked (and started with `-silent`) before every attempt,
  the expected files and the done-marker checked for freshness, quick exits counted as `startup`
  failures, up to 2 retries, the timeout, timestamped backups that are never overwritten. Each
  failure has its own reason in `result.json`.
- One oracle build per request, swapped into the game for the run, main's plugin restored in a
  `finally` and, after a crash of the runner, by the next runner before anything else.
- Tests: `tools/recq/tests/run_tests.ps1` drives the real runner against a fake game, a fake
  Steam and throwaway oracle projects, one test per failure path, including a runner killed in
  the middle of a run.

Where it differs from the plan below:

- **Game runs** go through `tools/oracle_launch.ps1`: a hidden desktop (the window never shows),
  windowed 640x360, **Normal** priority (Idle priority caused frame hitches that broke
  recordings), not minimized at Idle.
- **Steam's connection**: the test is the last state line being `[Logged On`, not `[Connected`
  (a bare `Connected` also shows up in failed logons). The log is the long-lived
  `connection_log.txt`, so after starting Steam only lines written since count.
- **Output folder**: the oracle now takes `-oracle-out=<dir>` (OraclePlugin.cs). The runner gives
  each run of an oracle that knows it its own folder, and main's `data/oracle` is never touched.
  For an older oracle (main's deployed plugin until this is merged and deployed, and lanes
  branched before), the runner copies `data/oracle` before the run and puts main's files back
  after it, moving what the run wrote into the request's folder.
- **Builds** copy the worktree's oracle project into `builds/<id>/src`, remove its deploy
  targets and point `GameDir` at a copy of the game's reference DLLs, so a build can't write into
  the game; main's plugin is guarded during the build too.
- **The results**: `done/<id>/` holds the expected files and the marker, `other/` for anything
  else the run wrote, `attempts/<n>/` for each attempt's logs and a failed attempt's files.

The plan as it was written:

Every system in the port is verified byte for byte against a recording of the real game, made by
the BepInEx oracle (`oracle/MoonlighterOracle`). Recording already runs unattended: a shell queue
launches the game minimized at Idle priority, one `-oracle-*` mode after another, and each run
quits itself (`-oracle-quit`). The oracle keeps the window small, minimized and unfocused
(`Unobtrusive.cs`).

What's missing is what would let many porting agents (8-10 lanes) use it without a person in the
middle. Three pieces, roughly an afternoon's work in total.

## 1. Agents can ask for recordings

**Now:** an agent writes the recorder code; then someone merges it, builds and deploys the
oracle, and starts the run by hand.

**Plan:** a queue folder (outside every worktree, e.g. `Documents/moonlighter-recq/`):

- `requests/<id>.json`: written by an agent.
  ```json
  {"id": "butler-1", "branch": "agent-butler", "worktree": "C:/Users/NoSig/Documents/ml-wt/butler",
   "args": "-oracle-enemy=GolemButler", "expects": ["enemy_GolemButler.jsonl"], "timeout": 900}
  ```
- `running/<id>.json`: the runner moves a request here while the game is up.
- `done/<id>/`: the recorded files, the run's `LogOutput.log` and a `result.json`
  (`ok` / `failed`, the reason, the times, the attempts). The agent waits for this folder.

The runner is one long-lived low-priority script (PowerShell, since Git's shell mangles `start`'s
`/min` and `/low`). It takes one request at a time, oldest first, because only one game instance
can run.

## 2. Failure handling

**Now:** a run that fails is only noticed later, when the file is missing or stale. On
2026-10-13 Steam closed during a queue, the game quit within seconds, and every run "finished"
with nothing recorded.

**Plan:** for each run, the runner checks:

- **Before:** `steam.exe` is running; if not, start it with `steam.exe -silent` and wait until it
  has connected (`logs/connection_log.txt` shows `[Connected`).
- **After:** every file in `expects` exists and was written after the run started, and the
  oracle's done-marker is there (`data/oracle/*_DONE`).
- **Failure:** keep the log and retry up to 2 times. If Steam was gone, restart it first. A run
  that exits within 30 s without its files counts as a Steam or startup failure.
- **Timeout:** past `timeout`, kill the game and fail the run with `timeout`.
- **Backups:** copy each recorded file to `moonlighter-oracle-backup/` under a timestamped name
  (`name_MMDDHHMM.jsonl`). Never back up under a name a later run will write: a plain `cp -u`
  once overwrote the only copy of the first slime recording.

## 3. One oracle build per request

**Now:** there is one plugin in the game's `BepInEx/plugins`, built from main. A lane's oracle
changes can only record after they're merged.

**Plan:** for each request:

1. Build the request's worktree's oracle with `dotnet build -c Release -o <recq>/builds/<id>/`
   (no deploy step, at Idle, `-m:1`).
2. Swap it in: move `MoonlighterOracle.dll` from `BepInEx/plugins` to `<recq>/main-plugin/`, then
   copy the request's build in.
3. Run the request.
4. Restore main's plugin, always, even after a failure (a `finally`).

Only the runner touches the game folder, so swaps never overlap.

The game's output folder stays `data/oracle` in the main checkout. The runner moves the
`expects` files from there into `done/<id>/`, so lanes never overwrite main's recordings. A
request that is meant to replace a main recording says so (`"promote": true`), and the file is
copied back after the run.

## Rules that stay

- Game windows stay out of sight (now: a hidden desktop, at Normal priority, see above). Never
  `-oracle-visible` in the queue.
- Harvest safety blocks saving and Steam stats in every mode. Never press "Read More" on the news
  popup.
- Agents never launch the game, the oracle or Dolphin themselves: they only write requests.
- Data never goes into the repo: recordings live in `data/oracle` (gitignored), the queue folder
  and the backup folder.

## Order of work

1. The runner with failure handling and backups, on the current single plugin.
2. Per-request oracle builds and the plugin swap.
3. Tell the agents: the request format, and how to wait for `done/<id>/result.json`.
