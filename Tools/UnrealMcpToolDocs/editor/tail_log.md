# unreal.tail_log

**Category**: editor
**Title**: Tail Editor Log
**Risk level**: read_only

Reads the tail of the current Unreal Editor log for recent warnings, errors, and MCP diagnostics.

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
    "lines": {
      "type": "number",
      "description": "Maximum number of log lines to return.",
      "default": 120
    },
    "contains": {
      "type": "string",
      "description": "Optional case-insensitive substring filter."
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
