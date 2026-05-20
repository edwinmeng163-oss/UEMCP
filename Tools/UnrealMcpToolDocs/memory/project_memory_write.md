# unreal.project_memory_write

**Category**: memory
**Title**: Write Project Memory Entry
**Risk level**: medium

Writes a persistent project memory key under Saved/UnrealMcp so future sessions can resume context.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {
    "key": {
      "type": "string",
      "description": "Memory entry key.",
      "default": "current"
    },
    "summary": {
      "type": "string",
      "description": "Short human-readable memory summary."
    },
    "status": {
      "type": "string",
      "description": "Current status, for example pending, in_progress, blocked, or done."
    },
    "nextStep": {
      "type": "string",
      "description": "Next action to resume after restart."
    },
    "contentJson": {
      "type": "string",
      "description": "Optional JSON object payload with detailed state."
    },
    "tags": {
      "type": "array",
      "description": "Optional tags for this memory entry.",
      "items": {
        "type": "string"
      }
    }
  },
  "required": [],
  "additionalProperties": false
}
```

## Usage example

_Provenance: schema-minimal_

```json
{}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: memory tool with controlled write or workflow side effects.
