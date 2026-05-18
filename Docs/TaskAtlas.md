# Task Atlas

Task Atlas is the v0.17 foundation for turning local ActivityLog evidence into
reviewable workflow records. It is local-first: runtime task files live under
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
  functional pinning, and visible placeholders for future promote actions.

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

v0.18 is reserved for real `To Skills` and `To RAG` promotion behavior.

v0.19 is reserved for real `Make Tool` behavior and LLM retrospective labeling.

In v0.17, the `To Skills`, `To RAG`, and `Make Tool` buttons are visible
placeholders only and show `Coming in v0.18` or `Coming in v0.19`.
