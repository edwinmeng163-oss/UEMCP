# unreal.mcp_clean_test_artifacts

**Category**: self-extension
**Title**: Clean Test Artifacts
**Risk level**: high

Removes disposable MCP test artifacts from approved sandbox paths and can preview cleanup with dryRun.

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
    "dryRun": {
      "type": "boolean",
      "description": "Preview cleanup candidates without deleting anything.",
      "default": true
    },
    "cleanTestScaffolds": {
      "type": "boolean",
      "description": "Include Saved/UnrealMcp/TestScaffolds child directories.",
      "default": true
    },
    "cleanTestRequests": {
      "type": "boolean",
      "description": "Include Saved/UnrealMcp/TestRequests child directories.",
      "default": false
    },
    "cleanBuildLogs": {
      "type": "boolean",
      "description": "Include Saved/UnrealMcp/BuildLogs/*.log files.",
      "default": false
    },
    "cleanExtensionBackups": {
      "type": "boolean",
      "description": "Include Saved/UnrealMcp/ExtensionBackups child directories.",
      "default": false
    },
    "cleanProjectMemory": {
      "type": "boolean",
      "description": "Include Saved/UnrealMcp/ProjectMemory.json. Use carefully.",
      "default": false
    },
    "maxAgeDays": {
      "type": "number",
      "description": "Only include artifacts at least this many days old. 0 disables age filtering.",
      "default": 0
    },
    "nameContains": {
      "type": "string",
      "description": "Optional case-insensitive path substring filter for targeted cleanup."
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
