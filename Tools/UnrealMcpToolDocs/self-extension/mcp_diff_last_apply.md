# unreal.mcp_diff_last_apply

**Category**: self-extension
**Title**: Diff Last Apply
**Risk level**: read_only

Reads the latest scaffold apply manifest and reports the source or registry file differences it recorded.

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
    "manifestPath": {
      "type": "string",
      "description": "Optional project-local manifest path. Defaults to Saved/UnrealMcp/LastExtensionApply.json."
    },
    "maxPreviewLines": {
      "type": "number",
      "description": "Maximum changed lines to include in the diff preview.",
      "default": 120
    },
    "includeFullText": {
      "type": "boolean",
      "description": "Whether to include full before/after source snapshots in structured output.",
      "default": false
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
