# unreal.mcp_backup_project_state

**Category**: self-extension
**Title**: Backup Project State
**Risk level**: high

Writes a backup snapshot of relevant project and plugin files under Saved/UnrealMcp for rollback-oriented workflows.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "label": {
      "type": "string",
      "description": "Short label for the snapshot directory.",
      "default": "manual"
    },
    "reason": {
      "type": "string",
      "description": "Why this project state backup is being created."
    },
    "includeSource": {
      "type": "boolean",
      "description": "Include Unreal MCP source/header files.",
      "default": true
    },
    "includeReadmes": {
      "type": "boolean",
      "description": "Include root and plugin README files.",
      "default": true
    },
    "includeProjectMemory": {
      "type": "boolean",
      "description": "Include Saved/UnrealMcp/ProjectMemory.json.",
      "default": true
    },
    "includeManifests": {
      "type": "boolean",
      "description": "Include extension apply manifests.",
      "default": true
    },
    "includeBuildLogs": {
      "type": "boolean",
      "description": "Include the latest few build logs.",
      "default": false
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview snapshot contents without writing backup files.",
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
- Reason: Explicit registry: high-impact write/build/rollback/destructive editor operation.
