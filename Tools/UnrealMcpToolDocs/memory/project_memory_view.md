# unreal.project_memory_view

**Category**: memory
**Title**: View Project Memory
**Risk level**: read_only

Lists project memory keys and compact values stored under Saved/UnrealMcp for local continuity.

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
    "keyFilter": {
      "type": "string",
      "description": "Optional case-insensitive substring filter for memory keys."
    },
    "status": {
      "type": "string",
      "description": "Optional exact status filter."
    },
    "tag": {
      "type": "string",
      "description": "Optional tag filter."
    },
    "includeContent": {
      "type": "boolean",
      "description": "Whether to include detailed content payloads.",
      "default": false
    },
    "maxEntries": {
      "type": "number",
      "description": "Maximum entries to return.",
      "default": 50
    },
    "sortDescending": {
      "type": "boolean",
      "description": "Sort newest updatedAtUtc entries first.",
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
