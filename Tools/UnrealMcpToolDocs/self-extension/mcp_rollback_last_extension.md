# unreal.mcp_rollback_last_extension

**Category**: self-extension
**Title**: Rollback Last Extension
**Risk level**: high

Rolls back the most recent applied scaffold using its backup manifest and reports restored files.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: manual

## Input schema

```json
{
  "type": "object",
  "properties": {
    "manifestPath": {
      "type": "string",
      "description": "Optional project-local manifest path. Defaults to Saved/UnrealMcp/LastExtensionApply.json."
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview rollback without restoring source.",
      "default": false
    },
    "force": {
      "type": "boolean",
      "description": "Restore even if current source hash differs from the apply manifest.",
      "default": false
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
  "dryRun": true,
  "skipLock": true
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: high-impact write/build/rollback/destructive editor operation.
