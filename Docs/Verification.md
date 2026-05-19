# Verification Tools

v0.20 C1a adds the first verification category tools for wrapping Unreal
Engine's Automation Test framework from MCP, and v0.21 adds listener-backed
editor diagnostics from the Output Log:

- `unreal.automation_list` discovers currently runnable automation tests.
- `unreal.automation_run` queues one exact test name and returns a run ID
  immediately.
- `unreal.automation_report` polls the run state and reads persisted historical
  reports.
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
report path, and optional caller tags.

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
