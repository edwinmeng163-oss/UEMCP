# unreal.mcp_rollback_to_manifest

**Category**: self-extension
**Title**: Rollback To Manifest
**Risk level**: high

Restores source and registry files from a selected extension backup manifest.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {
    "manifestPath": {
      "type": "string",
      "description": "Specific project-local/absolute manifest to restore. If empty, selects from ExtensionBackups."
    },
    "toolName": {
      "type": "string",
      "description": "Optional toolName filter when manifestPath is empty."
    },
    "selector": {
      "type": "string",
      "description": "Manifest selector when manifestPath is empty: latest or oldest.",
      "default": "latest"
    },
    "manifestIndex": {
      "type": "number",
      "description": "Optional zero-based candidate index after filtering/sorting; -1 uses selector.",
      "default": -1
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
    },
    "createPreRollbackBackup": {
      "type": "boolean",
      "description": "Snapshot current project state before a real rollback.",
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
- Reason: Explicit registry: high-impact write/build/rollback/destructive editor operation.
