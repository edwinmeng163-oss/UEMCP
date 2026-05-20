# Verification Agent Guide

Read this when work touches UE Automation Framework wrappers, asynchronous run
state, PIE smoke, Output Log diagnostics, MCP test suites, or read-back loops.
The canonical deep reference is [Verification](../Verification.md).

## Tool Surface

- `unreal.automation_list`: discovers currently runnable UE Automation
  Framework tests.
- `unreal.automation_run`: queues one exact automation test and returns a run
  ID immediately.
- `unreal.automation_report`: polls current or historical persisted run state.
- `unreal.pie_smoke`: queues a single-instance Play In Editor smoke and reports
  through `unreal.automation_report`.
- `unreal.editor_diagnostics`: reads recent warning, error, and fatal Output
  Log diagnostics from an in-memory ring buffer.

These tools live in the `verification` ToolRegistry category. They are separate
from `self-extension` because they are a general editor verification surface,
not only source-apply helpers.

## Automation Lifecycle

Use `unreal.automation_list` first to find a canonical `fullName`. Then call
`unreal.automation_run` with that exact name. Only one automation or PIE smoke
run may be active at a time.

`unreal.automation_run` writes:

```text
Saved/UnrealMcp/AutomationRuns/<runId>.json
```

It returns immediately with `queued` state. Poll every 1-2 seconds:

```text
/tool unreal.automation_report {"runId":"20260519T120000Z-a1b2c3"}
```

Public status values are `queued`, `running`, `completed`, `failed`,
`timed_out`, and `stale`.

## PIE Smoke

`unreal.pie_smoke` verifies that Play In Editor can boot, remain alive for a
short window, and shut down to a sane editor state. It is high-risk and
write-capable because PIE mutates editor world state.

Input shape:

```json
{
  "mapPath": "/Game/Maps/L_Smoke",
  "timeoutSeconds": 60,
  "aliveWindowSeconds": 5
}
```

`mapPath` is optional. When supplied it must start with `/Game/`, exist in the
asset registry, and resolve to a `UWorld`. `timeoutSeconds` is clamped to
10..300. `aliveWindowSeconds` is clamped to 1..30 and must be less than
`timeoutSeconds`.

Common structured rejections:

- `RunAlreadyActive`: another automation or PIE smoke run holds the shared
  lock.
- `EditorMapDirty`: the requested map differs and the current map package is
  dirty.
- `MapNotFound`, `InvalidMapPath`, or `InvalidArguments`.

PIE reports include map path, `beganPlayObserved`, alive-window and clean-end
booleans, diagnostics excerpt, dirty-package delta, and a recoverability note.
PIE smoke never saves or cleans packages.

PIE process crashes are not recoverable in-process. Use
`Tools/unreal_mcp_supervisor.py` to restart the editor and re-run if needed.

## Editor Diagnostics

`unreal.editor_diagnostics` is a read-only tool for separating actionable
build, compile, map, content, automation, and log-severity diagnostics from
editor noise.

The in-memory ring buffer:

- Stores up to 5000 entries per editor launch.
- Is reset on launch and not persisted to disk.
- Stores warning, error, and fatal classes only.
- Returns chronological oldest-to-newest entries within the returned slice.

Input:

```json
{
  "since": "2026-05-19T12:00:00Z",
  "classes": ["compile", "log_error"],
  "limit": 200
}
```

Accepted classes are `compile`, `map_check`, `content`, `automation`,
`log_warning`, and `log_error`. `limit` is clamped to 1..1000.

Suggested hints are computed at read time for package version mismatches,
stale external-actor references, missing packages, backwards-compatible asset
issues, and Live Coding locks.

## Watchdog Semantics

The automation ticker heartbeats roughly every 0.1 seconds while a run is
queued or running. State-file heartbeat writes are debounced to at most once per
second.

Internal watchdog fields include `lastHeartbeatUtc`, `staleReason`, and
`bestEffortCancelAttempted`. `staleReason` values are:

- `hard_timeout`: `now > startedAt + timeoutSeconds + 60s`.
- `unresponsive`: `now > lastHeartbeatUtc + 30s`.
- `editor_shutdown`: the UnrealMcp module shut down while a run was active.

When a run becomes stale, the runner attempts best-effort cleanup, writes the
state file, and releases the shared lock so later runs can proceed.

## MCP Test Suites

Stable tests live under:

```text
Tools/UnrealMcpTests
```

Generated test scaffolds live under:

```text
Saved/UnrealMcp/TestScaffolds
```

Run the core suite from Chat:

```text
/tool unreal.mcp_run_test_suite {"testsDir":"Tools/UnrealMcpTests/Core","readProjectMemory":false,"writeProjectMemory":false}
```

Category suites:

```text
Tools/UnrealMcpTests/Actors
Tools/UnrealMcpTests/Blueprint
Tools/UnrealMcpTests/Scaffold
Tools/UnrealMcpTests/SelfExtension
Tools/UnrealMcpTests/Verification
Tools/UnrealMcpTests/Widget
```

Happy-path write tests should use disposable sandboxes:

```text
/Game/__UEvolveMcpTest*
UEvolveMcpTest_* actor labels
```

Avoid tests that mutate user content unless they first prepare and later clean a
bounded sandbox. Tests can assert structured content fields with
`expectToolCallStructuredFields`.

## Precision Loop

For non-trivial Chat work, prefer this loop:

1. `unreal.preview_change_plan`
2. `unreal.capture_project_snapshot`
3. Category mutation tools
4. Blueprint/Widget/Actor read-back tools
5. `unreal.capture_project_snapshot`
6. `unreal.diff_project_snapshot`
7. `unreal.verify_task_outcome`
8. `unreal.mcp_classify_error` for failures
