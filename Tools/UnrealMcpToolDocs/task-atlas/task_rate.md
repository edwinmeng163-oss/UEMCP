# unreal.task_rate

**Category**: task-atlas
**Title**: Rate Task Atlas Task
**Risk level**: low

Persists a success, failed, or unrated rating for an existing Task Atlas task and appends a task_rating ActivityLog event.

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
      "description": "Task Atlas taskId to rate."
    },
    "rating": {
      "type": "string",
      "description": "New task rating.",
      "default": "unrated",
      "enum": [
        "success",
        "failed",
        "unrated"
      ]
    }
  },
  "required": [
    "taskId",
    "rating"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "taskId": "fixture-task",
  "rating": "maybe"
}
```

## Provenance
- Source docs: Docs/TaskAtlas.md
- Reason: v0.17 Task Atlas user rating tool with local JSON persistence.
- Notes: The rating event is a local audit annotation. It does not promote the task to skills or RAG.
