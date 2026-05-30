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

`Make Tool` now derives a `user.atlas_*` tool name from the workflow label,
filters `criticalPath` to visible core `unreal.*` tools, writes
`Tools/UnrealMcpPyTools/<atlas_*>/main.py` plus `tool.json`, records the
`pythonHandlerSha256`, reloads the user registry, and smoke-tests the user tool.
It does not call `unreal.scaffold_mcp_tool`. Generated composites are skeletons:
Task Atlas currently has tool names but not per-step argument captures, so real
argument replay is deferred to v0.31.

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

## Frozen Task JSON

Each task is persisted as `Saved/UnrealMcp/Tasks/<taskId>.json`:

```json
{
  "schemaVersion": 1,
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
  "userIntentText": "string|null",
  "aiSummaryText": "string|null",
  "labelSource": "llm_backfill",
  "labelConfidence": 0.0
}
```

`schemaVersion` is exactly `1`. `taskId` is deterministic from the ActivityLog
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
a composite Python user tool directly to the user registry. The generated
`main.py` calls each visible core step with `call_tool("<tool>",
args.get("stepN_args", {}))`; the generated `tool.json` has a closed top-level
input schema with one object property per step and a `smokeArgs` object for the
reload/smoke path. Each step remains governed by fail-closed `call_tool` policy.

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
