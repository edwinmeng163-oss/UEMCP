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

## Functional In v0.18

- `To Skills` is row-specific and calls
  `unreal.skill_distill_from_activity` for the selected workflow with its
  `sessionId`, a deterministic slug derived from the task label, the task
  label as title, the user intent as goal when available, and draft-writing
  options enabled.
- `To RAG` is row-specific and writes one markdown source file under
  `Saved/UnrealMcp/KnowledgeSources/TaskAtlas/<taskId>.md`, including task
  metadata, user intent, AI summary, critical path tools, and compact event
  refs, then calls `unreal.knowledge_index_refresh` with default arguments.
  **v0.18 known limitation**: `knowledge_index_refresh` currently scans
  `documents.jsonl` manifest files, not arbitrary `*.md` under the source
  root. The Task Atlas `.md` is written reliably but is not yet picked up by
  the RAG index. The indexing extension lands in v0.19 alongside the
  Make Tool integration.
- `Make Tool` remains a visible v0.19 placeholder.

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
  "aiSummaryText": "string|null"
}
```

`schemaVersion` is exactly `1`. `taskId` is deterministic from the ActivityLog
`sessionId` plus the first task event timestamp compacted into a filename-safe
token. The default label is the first sentence of `user_intent`; without an
intent, it falls back to `Session YYYY-MM-DD HH:MM`.

Regeneration preserves existing `rating` and `pinned` fields unless an explicit
`task_rating` or `task_pin_change` event updates them.

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

v0.18 ships real `To Skills` and `To RAG` promotion behavior in the Task Atlas
window.

v0.19 is reserved for real `Make Tool` behavior and LLM retrospective labeling.

In v0.18, the `Make Tool` button is still a visible placeholder and shows
`Coming in v0.19`.
