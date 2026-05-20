# unreal.task_pin

**Category**: task-atlas
**Title**: Pin Task Atlas Task
**Risk level**: low

Persists a pinned state for an existing Task Atlas task and appends a task_pin_change ActivityLog event with a critical path snapshot.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "taskId": {
      "type": "string",
      "description": "Task Atlas taskId to pin or unpin."
    },
    "pinned": {
      "type": "boolean",
      "description": "Whether the task should be pinned.",
      "default": true
    }
  },
  "required": [
    "taskId",
    "pinned"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "taskId": "fixture-task",
  "pinned": "true"
}
```

## Provenance
- Source docs: Docs/TaskAtlas.md
- Reason: v0.17 Task Atlas pinning tool for workflow curation.
- Notes: Pinning is functional in v0.17 and remains a user lock that retrospective label backfill never overwrites.
