# unreal.project_memory_read

**Category**: memory
**Title**: Read Project Memory Entry
**Risk level**: read_only

Reads one persistent project memory key from Saved/UnrealMcp without mutating state.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {
    "key": {
      "type": "string",
      "description": "Optional memory entry key. Empty returns all entries."
    },
    "includeContent": {
      "type": "boolean",
      "description": "Whether to include detailed content payloads.",
      "default": true
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
- Reason: Explicit registry: read-only inspection, audit, status, schema validation, memory read, or skill read tool.
