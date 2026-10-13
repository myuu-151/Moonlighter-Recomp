# Unattended recording queue: plan

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

- Game windows stay minimized at Idle priority. Never `-oracle-visible` in the queue.
- Harvest safety blocks saving and Steam stats in every mode. Never press "Read More" on the news
  popup.
- Agents never launch the game, the oracle or Dolphin themselves: they only write requests.
- Data never goes into the repo: recordings live in `data/oracle` (gitignored), the queue folder
  and the backup folder.

## Order of work

1. The runner with failure handling and backups, on the current single plugin.
2. Per-request oracle builds and the plugin swap.
3. Tell the agents: the request format, and how to wait for `done/<id>/result.json`.
