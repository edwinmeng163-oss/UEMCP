# Verification Tools

v0.20 C1a adds the first verification category tools for wrapping Unreal
Engine's Automation Test framework from MCP, and v0.21 adds listener-backed
editor diagnostics from the Output Log. v0.22 adds a PIE runtime smoke tool:

- `unreal.automation_list` discovers currently runnable automation tests.
- `unreal.automation_run` queues one exact test name and returns a run ID
  immediately.
- `unreal.automation_report` polls the run state and reads persisted historical
  reports.
- `unreal.pie_smoke` queues a single-instance Play In Editor smoke run and
  reports through `unreal.automation_report`.
- `unreal.editor_diagnostics` reads recent warning, error, and fatal Output Log
  diagnostics from an in-memory ring buffer.

These tools live in the `verification` ToolRegistry category. They are separate
from `self-extension` because they are a general editor verification surface,
not a source-apply pipeline helper.

## Build Diagnostics

`unreal.editor_diagnostics` is a read-only tool for separating actionable build,
compile, map, content, automation, and log-severity diagnostics from general
editor noise.

The UnrealMcp module registers a `FOutputDevice` listener at the beginning of
startup before the MCP server and install-doctor scheduling run. Shutdown
unregisters the output device before automation stale marking or other module
teardown work, so late editor-shutdown logging cannot target a destroyed
listener object.

The listener writes to an in-memory ring buffer:

- Capacity is 5000 entries.
- Entries are reset on each editor launch.
- No JSONL or disk persistence is written in v0.21.
- `ringBufferStartedAt` is the listener registration time.
- `ringBufferOverflow=true` means the 5000-entry buffer has wrapped at least
  once.
- Commandlets attach the same listener and return whatever the buffer captured,
  but editor-UI-dependent events may be absent in commandlet mode.

The hot listener path is intentionally small: map verbosity to severity, map
log category to class, copy the source/message, timestamp, and push under a
critical section. It does not regex-match, substring-match, read files, or write
files. Readers snapshot the buffer under the same lock and then filter/process
outside the lock.

Class mapping is computed from log category first:

- `LogMapCheck` -> `map_check`
- `LogBlueprintEditor`, `LogKismet`, `LogKismetCompiler`, `LogCompile` ->
  `compile`
- `LogLinker`, `LogPackageName`, `LogStreaming` -> `content`
- `LogAutomation`, `LogAutomationController` -> `automation`

Unknown categories fall back by severity:

- `Warning` -> `log_warning`
- `Error` and `Fatal` -> `log_error`
- `Display`, `Log`, `Verbose`, and lower-severity output are not stored.

Tool input:

```json
{
  "since": "2026-05-19T12:00:00Z",
  "classes": ["compile", "log_error"],
  "limit": 200
}
```

`since` is optional but must parse as an ISO-8601 UTC timestamp when present.
Invalid values return `isError=true`. `classes` is optional and accepts only
`compile`, `map_check`, `content`, `automation`, `log_warning`, or `log_error`.
`limit` defaults to 200 and is clamped to 1..1000. Returned entries are
chronological oldest-to-newest within the returned slice; when more entries
match than `limit`, the newest matching entries are returned and
`truncated=true`.

`suggested` hints are computed at read time with fixed substring checks, not in
the listener:

- `Package Version: 0` or `Min Required Version:` -> open and save the asset to
  refresh CustomVersion stamps.
- `_ExternalActors_` and `was not available` -> save the parent umap to drop the
  stale external-actor reference.
- `Failed to load package` -> check for deleted/renamed packages or an
  EngineAssociation mismatch.
- `is not backwards compatible` -> open with the matching engine, resave, or
  revert the source change.
- `Live Coding` -> close Unreal Editor or disable Live Coding before running
  UBT because plugin binaries may be locked.

## Async Lifecycle

Use `unreal.automation_list` first to find a canonical `fullName`. The
`fullName` is the runnable `FAutomationTestFramework` test command name; display
fields are included for humans.

Call `unreal.automation_run` with that exact `fullName`. The tool accepts only
one queued or running automation run at a time, writes an initial state file, and
returns:

```json
{
  "runId": "20260519T120000Z-a1b2c3",
  "acceptedAt": "2026-05-19T12:00:00Z",
  "initialStatus": "queued",
  "reportPath": "Saved/UnrealMcp/AutomationRuns/20260519T120000Z-a1b2c3.json"
}
```

The handler does not wait for completion. A ticker starts the UE automation test
on the game thread, advances latent/network automation commands, and writes the
final report once the framework reports completion.

Poll with:

```text
/tool unreal.automation_report {"runId":"20260519T120000Z-a1b2c3"}
```

Polling every 1-2 seconds is expected. Reports expose only the public fields in
the schema: status, test identity, result events, timing, a bounded log excerpt,
report path, `runType`, optional caller tags, and optional `pieReport`.

## C1b PIE Smoke

`unreal.pie_smoke` verifies that Play In Editor can boot, remain alive for a
short window, and shut down to a sane editor state. It is a high-risk
write-capable verification tool because PIE mutates editor world state. It uses
the same `Saved/UnrealMcp/AutomationRuns/<runId>.json` namespace and the same
single active-run lock as `unreal.automation_run`; callers poll with
`unreal.automation_report`.

Input:

```json
{
  "mapPath": "/Game/Maps/L_Smoke",
  "timeoutSeconds": 60,
  "aliveWindowSeconds": 5
}
```

`mapPath` is optional. When present it must start with `/Game/`, exist in the
asset registry, and resolve to a `UWorld` asset. `timeoutSeconds` defaults to
60 and is clamped to 10..300. `aliveWindowSeconds` defaults to 5 and is clamped
to 1..30; after clamping it must be less than `timeoutSeconds`.

The tool returns immediately:

```json
{
  "runId": "20260519T120000Z-a1b2c3",
  "acceptedAt": "2026-05-19T12:00:00Z",
  "matchedMap": "/Game/Maps/L_Smoke.L_Smoke",
  "initialStatus": "queued",
  "reportPath": "Saved/UnrealMcp/AutomationRuns/20260519T120000Z-a1b2c3.json",
  "pollingHint": "Poll automation_report every 1-2 seconds; PIE smoke typically completes in 10-30 seconds."
}
```

Structured rejection kinds are:

- `RunAlreadyActive`: another `automation` or `pie_smoke` run holds the shared
  active-run lock. The error includes `activeRunId` and `activeRunType`.
- `EditorMapDirty`: `mapPath` was provided, the current editor map differs, and
  the current map package is dirty. The tool does not auto-save or discard.
- `MapNotFound`: the requested `/Game/` map package or object path was not
  found in the asset registry.
- `InvalidMapPath`: the path does not start with `/Game/`, is malformed, or
  points at an existing non-`UWorld` asset.
- `InvalidArguments`: `aliveWindowSeconds >= timeoutSeconds` after clamping.

Lifecycle:

1. Validate and clamp arguments.
2. Validate `mapPath` when supplied.
3. Reject if the shared automation/PIE lock is active.
4. Reject dirty-current-map conflicts before opening a different map.
5. Acquire the shared active-run lock with `runType="pie_smoke"`.
6. Capture `startedAt` immediately after lock acquisition and before any map
   open so diagnostics include map-load warnings.
7. Snapshot `dirtyAtStart`.
8. Open `mapPath` if supplied and different from the current editor map.
9. Register `BeginPIE` and `EndPIE` delegate handles.
10. Request a single-instance PIE session on the game thread.
11. Wait for `BeginPIE`.
12. Poll for `aliveWindowSeconds`, requiring `GEditor->PlayWorld` to stay
    non-null and `HasBegunPlay()` to be observed at least once.
13. Request `EndPlayMap`.
14. Wait for `EndPIE` and `PlayWorld == nullptr`.
15. Unregister delegate handles.
16. Snapshot `dirtyAtEnd`.
17. Compute `newlyDirty` and `cleanedDuringPie`.
18. Attach diagnostics with `ts >= startedAt`, oldest-first excerpt capped at
    100 entries, and write `completed` only when BeginPIE, alive window, and
    clean EndPIE all succeeded. Other terminal lifecycle outcomes are `failed`.

`automation_report` includes `runType` for all new reports. If it reads a v0.20
state file with no `runType`, it defaults to `"automation"` for backward
compatibility. PIE smoke reports add `pieReport`:

```json
{
  "runType": "pie_smoke",
  "pieReport": {
    "mapPath": "/Game/Maps/L_Smoke.L_Smoke",
    "beganPlayObserved": true,
    "aliveWindowSatisfied": true,
    "pieEndedCleanly": true,
    "diagnostics": {
      "startedAt": "2026-05-19T12:00:00Z",
      "errorCount": 0,
      "warningCount": 0,
      "excerpt": [],
      "excerptTruncated": false
    },
    "packageDirtinessDelta": {
      "dirtyAtStart": [],
      "dirtyAtEnd": [],
      "newlyDirty": [],
      "cleanedDuringPie": []
    },
    "recoverabilityNote": "PIE crash is NOT recoverable in-process; use unreal_mcp_supervisor.py to restart the editor and re-run if needed."
  }
}
```

Dirty-package delta semantics are simple set differences over package paths:
`newlyDirty = dirtyAtEnd - dirtyAtStart`, and
`cleanedDuringPie = dirtyAtStart - dirtyAtEnd`. These fields are diagnostic
evidence only; PIE smoke never saves or cleans packages.

PIE lifecycle APIs, delegate registration/removal, `PlayWorld` access, and
`RequestEndPlayMap` cleanup are marshalled onto the editor/game thread. Stale
recovery follows the automation watchdog: `startedAt + timeoutSeconds + 60s`
marks the run `stale` with `staleReason="hard_timeout"`, unregisters delegate
handles, requests `EndPlayMap` best-effort, writes a partial `pieReport`, and
releases the shared lock.

PIE process crashes are not recoverable in-process. Use
`Tools/unreal_mcp_supervisor.py` to restart the editor and re-run the smoke if
the editor process exits or becomes unrecoverable.

## Watchdog Semantics

The ticker updates the active run heartbeat on every invocation, roughly every
0.1 seconds while an automation run is queued or running. The in-memory state
always carries the latest `lastHeartbeatUtc`, but the state file is debounced to
at most one heartbeat write per second to avoid hot disk writes while
`automation_report` may also be polling the same file.

The persisted state file has internal-only watchdog fields:

- `lastHeartbeatUtc`: latest ticker heartbeat, ISO8601.
- `staleReason`: present when `status` becomes `stale`.
- `bestEffortCancelAttempted`: true when the runner attempted to drain queued
  UE automation commands after a stale decision.

`staleReason` values are:

- `hard_timeout`: `now > startedAt + timeoutSeconds + 60s`.
- `unresponsive`: `now > lastHeartbeatUtc + 30s`.
- `editor_shutdown`: the UnrealMcp module shut down while a run was still
  queued or running.

When a run becomes stale because of `hard_timeout` or `unresponsive`, the
runner calls `FAutomationTestFramework::Get().DequeueAllCommands()` as a
best-effort cleanup and sets `bestEffortCancelAttempted=true`. This does not
claim that Unreal cancelled any latent work; the state file's `status` and
`staleReason` remain the source of truth. During module shutdown the runner
writes `editor_shutdown`, removes its ticker, clears the active-run lock, and
does not rely on the automation framework being available.

## State Files

State files are local runtime data under:

```text
Saved/UnrealMcp/AutomationRuns/<runId>.json
```

The JSON file is the canonical run record. It includes the public report data
plus internal-only fields such as `internalSchemaVersion`,
`timeoutSecondsConfigured`, `lastHeartbeatUtc`, `staleReason`, and
`bestEffortCancelAttempted`. Do not treat internal fields as part of the public
MCP contract.

`automation_run.tags` are caller metadata only. They are stored in the state file
and echoed by `automation_report`; they are not passed to UE Automation tag or
filter APIs.

## Status Values

Public report status values are:

- `queued`: accepted and waiting for the ticker to start the test.
- `running`: the UE automation framework has started the test.
- `completed`: the test stopped successfully.
- `failed`: the test stopped with assertion or log errors, or could not start.
- `timed_out`: the configured timeout has elapsed while the state file remains
  active.
- `stale`: the run exceeded the hard-timeout window, stopped heartbeating, or
  was active during editor shutdown, so a later `automation_run` can accept a
  new run.

## v0.20 Scope

B1 implements the minimal foundation: discovery, one active async run, polling,
state-file persistence, caller tags, bounded log excerpts, and stale recovery.

B2 adds watchdog hardening plus Task Atlas dogfood gates. It intentionally does
not add a public cancellation tool, multi-run scheduling, history queries, or
public `automation_report` schema fields.

## Release Gate A (Manual)

Gate A checks LLM-inferred Task Atlas labels. It is manual because label quality
is subjective.

Pre-condition: have at least 5 historical sessions under:

```text
Examples/UEvolveExample57/Saved/UnrealMcp/ActivityLog/*.jsonl
```

These sessions should not already contain `user_intent` events; they are the
sessions that `unreal.task_label_backfill` will label.

Procedure:

```text
/tool unreal.task_label_backfill {"limit":5}
```

Use the default Anthropic provider, or whichever provider the project is
configured to use for Task Atlas label backfill. Then inspect the resulting
task JSON files under:

```text
Examples/UEvolveExample57/Saved/UnrealMcp/Tasks/*.json
```

Acceptance: at least 4 of 5 inferred labels are reasonable. "Reasonable" means
that an Unreal-experienced developer reading only the label could roughly guess
what the session did.

Failure mode: if fewer than 4 of 5 labels are reasonable, file a follow-up patch
to refine the prompt template in `UnrealMcpTaskLabelBackfillTool.cpp`, then
rerun Gate A before tagging the release.

## Release Gate B (Automated)

Gate B verifies skill replay for a read-only deterministic triad:

1. `unreal.editor.engine_version({})`
2. `unreal.project_settings_get({"category":"game","key":"DefaultGameMode"})`
3. `unreal.list_assets({"contentRoot":"/Game","limit":5})`

The release fixture is:

```text
Tools/UnrealMcpTests/Verification/05_gate_b_skill_replay.json
```

The C++ Automation Test is:

```text
UnrealMcp.Verification.GateB.SkillReplay
```

The test stages a synthetic ActivityLog window with the triad, calls the
in-process skill distillation helper to write a draft, then calls
`SkillApply` in-process with `writeMemory=false`. It passes only if the returned
skill text lists the three tool names in the observed order.

## Release Gate D (Automated)

Gate D verifies that a Task Atlas markdown source can be found through
`unreal.tool_recommend` after a knowledge refresh.

The release fixture is:

```text
Tools/UnrealMcpTests/Verification/06_gate_d_rag_hit.json
```

The C++ Automation Test is:

```text
UnrealMcp.Verification.GateD.RagHit
```

The test writes:

```text
Saved/UnrealMcp/KnowledgeSources/TaskAtlas/gate_d_smoke.md
```

with the sentinel `UEVOLVE_GATE_D_SENTINEL_TASK_ATLAS_SMOKE`, calls
`KnowledgeIndexRefresh` in-process, then calls `ToolRecommend` in-process with
the sentinel query. It passes only if `knowledgeCards[]` contains the synthetic
source by title, source path, or excerpt. A scoped cleanup guard deletes the
markdown file and refreshes the index again even when the assertion fails.

## Release Gate C (Stretch / Deferred)

Gate C would validate Make Tool full apply from a Task Atlas workflow. It is
deferred to v0.20.1 hardening and is not release-blocking for v0.20.

## Manual editor smoke checklist (v0.26.2+)

These checks cover runtime/editor behaviors that the current automation suite
does not fully protect.

- v0.26.2-blocking: AI provider settings survive an editor restart. The
  selected provider/model must remain what the user chose and must not reset to
  `gpt-test` or `openai-default`.
- v0.26.2-blocking: A user Python tool `reload -> smoke` round-trip returns a
  real pass/fail result from `unreal.mcp_user_registry_reload` followed by
  `unreal.mcp_user_tool_smoke`.
- v0.26.2-blocking: `unreal.mcp_tool_audit` reports
  `schemaIncompatibleCount=0`.
- v0.26.2-blocking: `unreal.tools.export_package` produces a real `.zip` and
  sidecar manifest.
- v0.26.3-pending: model-edit UX polish remains deferred.
- v0.26.3-pending: export Save-As UX remains deferred.
