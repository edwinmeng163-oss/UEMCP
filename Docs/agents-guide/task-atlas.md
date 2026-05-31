# Task Atlas Agent Guide

Read this when work touches ActivityLog task extraction, workflow records,
ratings, pinning, promotion to skills/RAG, Make Tool composites, or
retrospective labels. The canonical deep reference is
[TaskAtlas](../TaskAtlas.md).

## Purpose

Task Atlas turns local ActivityLog evidence into reviewable workflow records
and promotion sources. Runtime task files live under:

```text
Saved/UnrealMcp/Tasks/<taskId>.json
```

These files are local runtime evidence only and are not committed.

## Tool Surface

- `unreal.activity_log_annotate`: writes `user_intent` or `ai_summary`
  ActivityLog events.
- `unreal.task_list`: rebuilds and lists local task JSON files.
- `unreal.task_describe`: returns one full task JSON document.
- `unreal.task_rate`: persists `success`, `failed`, or `unrated` and writes a
  `task_rating` ActivityLog event.
- `unreal.task_pin`: persists pin state and writes a `task_pin_change`
  ActivityLog event with the task critical path snapshot.
- `unreal.task_label_backfill`: labels placeholder tasks through the configured
  Anthropic provider while preserving pinned and user-edited tasks.
- `STaskAtlasWindow`: Chat-launched Slate view for workflows, unused tools,
  search, tool details, pinning, Distill Skill, To RAG, and Make Tool.

## Lifecycle

- v0.17 ships extraction, listing, details, rating, pinning, Chat hooks, and the
  Task Atlas window.
- v0.18 ships row-specific skills and RAG promotion behavior.
- v0.19 shipped the original `Make Tool` scaffold creation, Task Atlas
  markdown RAG ingestion, and `unreal.task_label_backfill`.
- v0.30 R2 Wave C changes `Make Tool` to direct composite Python user-tool
  generation from the task critical path.
- v0.31 Stage 2 adds ActivityLog `eventId`, redacted captured-argument
  metadata, schema v2 `stepRefs`, replay eligibility, and preview composite
  generation from the private captured-args store.

`Make Tool` now derives a `user.atlas_*` tool name from the workflow label,
uses ordered `stepRefs` when available, falls back to `criticalPath` for older
tasks, writes `Tools/UnrealMcpPyTools/<atlas_*>/main.py` plus `tool.json`,
records the `pythonHandlerSha256`, reloads the user registry, and smoke-tests
the user tool. It does not call `unreal.scaffold_mcp_tool`. Generated tools are
reviewable preview or skeleton composites, not real replay tools.

## Frozen ActivityLog Events

v0.17 adds these frozen `eventKind` values:

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
deterministic from the ActivityLog
`sessionId` plus the first task event timestamp compacted into a filename-safe
token. The default label is the first sentence of `user_intent`; without an
intent, it falls back to `Session YYYY-MM-DD HH:MM`. `labelSource` and
`labelConfidence` are optional and appear when retrospective labeling updates
the task.

Regeneration preserves existing `rating` and `pinned` fields unless an explicit
`task_rating` or `task_pin_change` event updates them. When no explicit
`userIntentText` exists, regeneration also preserves `labelSource`,
`labelConfidence`, and non-placeholder user labels.

## Clustering Heuristic

Task extraction clusters events with the same `sessionId`. A new task starts
only when the previous task has seen an `ai_summary` completion marker and the
inter-event gap is at least 10 minutes.

The 10-minute value is a v0.17 heuristic, not a schema field. Malformed, older,
or non-object JSONL records are skipped defensively.

## Promotion Actions

`Distill Skill` calls `unreal.skill_distill_from_activity` for the selected
workflow with its `sessionId`, deterministic slug, task label, user intent, and
draft-writing options enabled.

`To RAG` writes markdown under:

```text
Saved/UnrealMcp/KnowledgeSources/TaskAtlas/<taskId>.md
```

Then it calls `unreal.knowledge_index_refresh`. v0.19 indexing ingests these
markdown files as inline `task-atlas` KnowledgeCards so promoted workflows are
searchable after refresh.

`Make Tool` derives an `atlas_*` user-tool id from the workflow label and writes
a composite Python user tool directly to the user registry. For schema v2 tasks,
the generated `main.py` follows ordered, non-deduped `stepRefs` and reads
sanitized captured arguments from `Saved/UnrealMcp/CapturedToolArgs` through
`captureRef` plus SHA validation. Captured arguments are embedded as
`json.loads` data constants and used only as defaults; caller-supplied
`stepN_args` override them. Older tasks or steps without usable captures become
placeholder skeleton steps.

The generated `tool.json` has a closed top-level input schema with one object
property per step, `compositeKind=preview|skeleton`, and
`replayStatus=preview_only|partial|skeleton_pre_capture|blocked`. Each step
uses fail-closed `call_tool_raw` policy and reports `policyDecision`, `isError`,
and `effectiveArgsDiff` when the policy forces a dry run.

## Preview Composite Workflow

For AI or user review, treat `Make Tool` output as a draft:

1. Generate the composite from Task Atlas and inspect `replayEligibility` /
   `replayStatus`.
2. Run a preview with no arguments or with explicit `stepN_args` overrides.
   Read-only steps may execute; write-capable steps should be expected to run as
   dry runs through policy.
3. Replace captured defaults with reviewed real arguments where needed before
   using the tool as reusable automation.

Safety boundaries are fixed: user tools cannot call `user.*`, nested
composition is rejected at depth 1, hidden or denied tools fail closed, and
captured defaults are already redacted for secret-looking fields, home/project
paths, and size limits.

## Retrospective Label Backfill

`unreal.task_label_backfill` accepts:

```json
{
  "sessionId": "optional ActivityLog session id filter",
  "limit": 25,
  "force": false,
  "dryRun": false
}
```

The tool scans `Saved/UnrealMcp/Tasks/*.json` for unpinned tasks whose label is
exactly `Session YYYY-MM-DD HH:MM` and whose `userIntentText` is empty. It
never overwrites pinned tasks. Missing Anthropic configuration is a structured
success with `processedCount=0` and `no_provider_configured` skip reasons.

`force=true` broadens discovery, but still does not overwrite labels unless the
current label is the exact placeholder and `userIntentText` is empty.
`dryRun=true` reports eligible tasks without provider calls or writes.

## Activity Sources

ActivityLog launch records live under:

```text
Saved/UnrealMcp/ActivityLog/<sessionId>.jsonl
```

`UnrealMcp::GetLaunchSessionId()` mints one session ID per editor launch,
shaped `{YYYYMMDD-HHMMSS}-{guid8}`. The writer rotates to
`<sessionId>.<n>.jsonl` at 10 MB or 5000 entries and emits `tool_call`,
`chat_turn`, `manifest_apply`, `manifest_dryrun`, skill events, and Task Atlas
annotations.
