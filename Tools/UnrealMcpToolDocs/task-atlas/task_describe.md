# unreal.task_describe

**Category**: task-atlas
**Title**: Describe Task Atlas Task
**Risk level**: read_only

Returns the full persisted Task Atlas JSON document plus task path/status fields for one taskId.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "taskId": {
      "type": "string",
      "description": "Task Atlas taskId to describe."
    }
  },
  "required": [
    "taskId"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{}
```

## Provenance
- Source docs: Docs/TaskAtlas.md
- Reason: v0.17 Task Atlas read-only task detail lookup.
- Notes: Rejects unsafe taskId strings before resolving the Saved/UnrealMcp/Tasks JSON path.
