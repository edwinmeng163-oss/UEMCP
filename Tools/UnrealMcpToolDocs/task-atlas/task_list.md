# unreal.task_list

**Category**: task-atlas
**Title**: List Task Atlas Tasks
**Risk level**: read_only

Refreshes Task Atlas JSON files from ActivityLog and lists extracted tasks with rating, pin state, and critical path.

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
    "filter": {
      "type": "string",
      "description": "Optional task filter.",
      "default": "all",
      "enum": [
        "all",
        "success",
        "failed",
        "unrated",
        "pinned"
      ]
    },
    "limit": {
      "type": "number",
      "description": "Maximum tasks to return.",
      "default": 50
    }
  },
  "required": [],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "filter": "all",
  "limit": 10
}
```

## Provenance
- Source docs: Docs/TaskAtlas.md
- Reason: v0.17 Task Atlas read-only task listing with optional rating/pin filters.
- Notes: Read-only from MCP policy perspective, but it may refresh derived task JSON from ActivityLog while preserving rating and pin choices.
