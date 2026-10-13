# The recording queue's runner (tools/recq/README.md, docs/recording_queue_plan.md).
#
# Takes requests/<id>.json from the queue folder one at a time, oldest first: builds the request's
# worktree's oracle (no deploy step), swaps it into the game's BepInEx/plugins, makes sure Steam is
# running and logged on, runs the game through tools/oracle_launch.ps1 (hidden desktop, Normal
# priority, -oracle-quit, a timeout), checks the expected files and the done-marker, retries, backs
# the recordings up, puts main's plugin and main's data/oracle back, and writes done/<id>/result.json.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\recq\runner.ps1 [-Config <json>] [-Once]
#
# Normally started detached with tools\recq\start.ps1. -Once: exit when the queue is empty.
# Exit codes: 0 stopped (STOP file, or -Once with an empty queue), 3 another runner holds the
# queue, 4 main's plugin could not be restored (-Once).
param(
    [string]$Config = '',
    [switch]$Once
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'recq.psm1') -Force -DisableNameChecking
$out = @(Start-RecqRunner -ConfigPath $Config -Once:$Once)
exit [int]$out[$out.Count - 1]
