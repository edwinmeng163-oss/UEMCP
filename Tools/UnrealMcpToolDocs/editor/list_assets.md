# unreal.list_assets

**Category**: editor
**Title**: List Assets
**Risk level**: read_only

Lists assets under a Content Browser path with optional class filtering for read-only project inspection.

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
    "path": {
      "type": "string",
      "description": "Content Browser path such as /Game or /Game/Variant_TwinStick.",
      "default": "/Game"
    },
    "recursive": {
      "type": "boolean",
      "description": "Whether to include child paths.",
      "default": true
    },
    "classPath": {
      "type": "string",
      "description": "Optional asset class path filter such as /Script/Engine.Blueprint."
    },
    "limit": {
      "type": "number",
      "description": "Maximum number of assets to return.",
      "default": 200
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
