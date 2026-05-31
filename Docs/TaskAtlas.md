# Task Atlas

Task Atlas turns local ActivityLog evidence into reviewable workflow records
and promotion sources. It is local-first: runtime task files live under
`Saved/UnrealMcp/Tasks/` and are not committed.

## Functional In v0.17

- `unreal.activity_log_annotate` writes `user_intent` or `ai_summary`
  ActivityLog events.
- `unreal.task_list` rebuilds and lists local task JSON files.
- `unreal.task_describe` returns one full task JSON document.
- `unreal.task_rate` persists `success`, `failed`, or `unrated` and writes a
  `task_rating` ActivityLog event.
- `unreal.task_pin` persists pin state and writes a `task_pin_change`
  ActivityLog event with the task critical path snapshot.
- The Chat panel opens a Task Atlas window and can show `Success` / `Fail`
  rating buttons on final assistant responses.
- `STaskAtlasWindow` shows workflows, unused tools, live search, tool details,
  functional pinning, and visible promote buttons.

## Functional In v0.18 And v0.19

- `Distill Skill` is row-specific and calls
  `unreal.skill_distill_from_activity` for the selected workflow with its
  `sessionId`, a deterministic slug derived from the task label, the task
  label as title, the user intent as goal when available, and draft-writing
  options enabled.
- `To RAG` is row-specific and writes one markdown source file under
  `Saved/UnrealMcp/KnowledgeSources/TaskAtlas/<taskId>.md`, including task
  metadata, user intent, AI summary, critical path tools, and compact event
  refs, then calls `unreal.knowledge_index_refresh` with default arguments.
  v0.19 indexing ingests these Task Atlas markdown files as inline
  `task-atlas` KnowledgeCards so promoted workflows are searchable after the
  refresh completes.
- `Make Tool` is row-specific and derives an `atlas_*` user-tool id from the
  workflow label, then directly writes a composite Python user tool under
  `Tools/UnrealMcpPyTools/<atlas_*>/`. It does not call
  `unreal.scaffold_mcp_tool`.
- `unreal.task_label_backfill` scans `Saved/UnrealMcp/Tasks/*.json` for
  unpinned tasks whose label is exactly `Session YYYY-MM-DD HH:MM` and whose
  `userIntentText` is empty, then asks the configured Anthropic Messages
  provider for a 3-7 word retrospective label. It stores
  `labelSource: "llm_backfill"` and `labelConfidence` on updated task JSON.
  Missing Anthropic configuration is a structured success with
  `processedCount=0` and `no_provider_configured` skip reasons.
- v0.19 is complete: Part A made `Make Tool` create scaffold drafts, Part B
  added Task Atlas markdown ingestion/RAG indexing, and Part C added LLM
  retrospective label backfill.

## Functional In v0.30 R2 Wave C

- `Make Tool` now generates a project-local composite Python user tool instead
  of a self-extension scaffold. It filters the task `criticalPath` to visible
  core `unreal.*` tools, writes `main.py` with a `call_tool(...)` sequence,
  writes closed-schema `tool.json` with one `stepN_args` object per step,
  records `pythonHandlerSha256`, includes `smokeArgs`, then calls
  `unreal.mcp_user_registry_reload` and `unreal.mcp_user_tool_smoke`.
- Generated composites are skeletons. Task `eventRefs` store only `{ts, tool,
  isError}`, not per-step arguments, so real captured-argument replay is
  deferred to v0.31. Users must fill reviewed `stepN_args` before treating the
  composite as a real replay tool.
- Each step still runs through Wave A/Wave B `call_tool` policy. Non-visible,
  `user.*`, recursive, or dangerous-no-dry-run targets fail closed; dangerous
  dry-run-capable targets are forced to dry run by the core policy.
- The skills promotion button is labeled `Distill Skill` and still calls
  `unreal.skill_distill_from_activity`; it is separate from composite
  generation.

## Functional In v0.31 Stage 2 Wave C

- Task files now use schema v2.0. The ActivityLog reader carries top-level
  `eventId` plus payload `captureStatus` and `captureRef` into the task model.
- `stepRefs` are ordered tool-call steps. They are not deduped, so repeated
  calls to the same tool are preserved for generated composites. `criticalPath`
  remains the deduped human summary.
- Each `stepRefs` entry includes `ordinal`, `eventId`, `sessionId`, `tool`,
  `ts`, `isError`, `captureStatus`, optional `captureRef`, and
  `policyClassAtCapture` from the same call-tool policy used by Python user
  tools.
- `replayEligibility` is `preview_ready`, `partial`, `skeleton_pre_capture`, or
  `blocked`. Preview-ready means every emitted step has captured or redacted
  arguments and no step is denied by policy. Partial means at least one step is
  missing usable captured args. Skeleton-pre-capture means no step has captured
  args. Blocked means at least one step is denied by policy.
- The Task Atlas window and generated `tool.json` use honest labels:
  `compositeKind=preview|skeleton` and
  `replayStatus=preview_only|partial|skeleton_pre_capture|blocked`.
- Wave D `Make Tool` reads replayable `captureRef` entries from the private
  captured-args store with sha validation and emits preview composite code.
  Captured arguments are embedded only as JSON data constants parsed with
  `json.loads`; they are not spliced into Python logic, comments, or f-strings.
  Steps without readable captured args remain placeholders.
- Generated preview composites call through the same user-tool `call_tool_raw`
  policy seam, record each step's runtime `policyDecision` and `isError`, and
  include `effectiveArgsDiff` when force-dry-run changes the effective arguments
  such as adding `dryRun:true`.
- A preview composite is still reviewable workflow draft, not real replay.
  Write-capable steps are dry-run-only through call-tool policy, and denied
  steps stop the composite with structured step evidence.

`unreal.task_label_backfill` accepts:

```json
{
  "sessionId": "optional ActivityLog session id filter",
  "limit": 25,
  "force": false,
  "dryRun": false
}
```

The tool is intentionally conservative. It never overwrites pinned tasks.
`force=true` broadens discovery, but it still does not overwrite labels unless
the current label is the exact `Session YYYY-MM-DD HH:MM` placeholder and
`userIntentText` is empty. `dryRun=true` reports eligible tasks without calling
the provider or writing task JSON.

## Frozen ActivityLog Events

v0.17 adds these `eventKind` values:

```text
user_intent
  payload: { content: string, sessionRefId?: string }

ai_summary
  payload: { content: string, completionMarker: bool }

task_rating
  payload: { taskId: string, rating: "success"|"failed"|"unrated" }

task_pin_change
  payload: { taskId: string, pinned: bool, criticalPathSnapshot?: [string] }
```

These names and payload shapes are frozen for v0.17 through v0.19.

## Current Task JSON

Each task is persisted as `Saved/UnrealMcp/Tasks/<taskId>.json`:

```json
{
  "schemaVersion": 2.0,
  "taskId": "<sessionId>-<startTsCompact>",
  "label": "string",
  "sessionId": "string",
  "criticalPath": ["unreal.tool_name"],
  "rating": "success|failed|unrated",
  "pinned": false,
  "tStartUtc": "ISO8601",
  "tEndUtc": "ISO8601",
  "eventCount": 0,
  "eventRefs": [{"ts": "...", "tool": "...", "isError": false}],
  "stepRefs": [
    {
      "ordinal": 0,
      "eventId": "ActivityLog eventId",
      "sessionId": "string",
      "tool": "unreal.tool_name",
      "ts": "ISO8601",
      "isError": false,
      "captureStatus": "captured|redacted|missing",
      "captureRef": "CapturedToolArgs/<session>/<eventId>.json",
      "policyClassAtCapture": "allow|force_dry_run|deny"
    }
  ],
  "stepRefTotal": 0,
  "stepRefsTruncated": false,
  "replayEligibility": "preview_ready|partial|skeleton_pre_capture|blocked",
  "replayUnavailableReason": "string",
  "userIntentText": "string|null",
  "aiSummaryText": "string|null",
  "labelSource": "llm_backfill",
  "labelConfidence": 0.0
}
```

`schemaVersion` is exactly `2.0` for regenerated tasks. `taskId` is
deterministic from the ActivityLog `sessionId` plus the first task event
timestamp compacted into a filename-safe token. The default label is the first
sentence of `user_intent`; without an intent, it falls back to
`Session YYYY-MM-DD HH:MM`. `labelSource` and `labelConfidence` are optional
and are present when retrospective labeling has updated the task.

Regeneration preserves existing `rating` and `pinned` fields unless an explicit
`task_rating` or `task_pin_change` event updates them. When no explicit
`userIntentText` exists, regeneration also preserves `labelSource`,
`labelConfidence`, and non-placeholder user labels so retrospective labeling and
manual curation are not lost on the next `unreal.task_list` refresh.

## Clustering Heuristic

Task extraction clusters events with the same `sessionId`. A new task starts
only when the previous task has seen an `ai_summary` completion marker and the
inter-event gap is at least 10 minutes.

The 10-minute value is a v0.17 heuristic, not a schema field. It was chosen
because local ActivityLog sampling found 99.7 percent of same-session event gaps
were 10 minutes or less, with one outlier above 10 minutes.

Malformed, older, or non-object JSONL records are skipped defensively.

## Lifecycle Boundaries

v0.17 ships extraction, listing, details, rating, pinning, Chat hooks, and the
Task Atlas window.

v0.18 ships real skills and RAG promotion behavior in the Task Atlas
window.

v0.19 shipped the original Task Atlas `Make Tool` scaffold creation, Task Atlas
markdown RAG ingestion, and `unreal.task_label_backfill`.

v0.30 R2 Wave C changes `Make Tool` to direct composite Python user-tool
generation. The button derives a `user.atlas_*` tool name from the workflow
label, writes `main.py` plus `tool.json` with a matching SHA-256, reloads the
user registry, and smoke-tests the tool. It is intentionally Python-only and
skeleton-only; C++ composite output and captured-argument replay are deferred.

v0.31 Stage 2 Wave C upgrades the task projection to schema v2.0 with ordered
`stepRefs`, capture references, policy classification, and replay eligibility
metadata. `Make Tool` surfaces preview-only, partial, blocked, or
pre-capture-skeleton status, but still does not generate real captured-argument
replay code.

v0.31 Stage 2 Wave D upgrades `Make Tool` to preview composite generation. It
uses ordered `stepRefs` instead of deduped `criticalPath` when available, reads
captured args through `UnrealMcp::CapturedArgsStore::ReadCapturedArgs`, embeds
sanitized defaults as `json.loads` data constants, and reports per-step
`policyDecision`, `isError`, and force-dry-run `effectiveArgsDiff`. Real replay
remains deferred.
