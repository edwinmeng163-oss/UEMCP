# unreal.skill_activity_status

**Category**: skills
**Title**: Get Skill Activity Status
**Risk level**: read_only

Reports local skill activity recording status, current session files, and available drafts without mutating state.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "includeRecentEvents": {
      "type": "boolean",
      "description": "Include recent JSONL events from the active session.",
      "default": false
    },
    "maxEvents": {
      "type": "number",
      "description": "Maximum recent events to include.",
      "default": 20
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
  "includeRecentEvents": true,
  "maxEvents": 10
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: read-only inspection, audit, status, schema validation, memory read, or skill read tool.
