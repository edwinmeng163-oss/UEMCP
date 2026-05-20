# unreal.mcp_workbench_status

**Category**: self-extension
**Title**: MCP Workbench Status
**Risk level**: read_only

Read-only dashboard summary for self-extension health: tools, audit, memory, manifests, build/test artifacts, and supervisor status.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "memoryKey": {
      "type": "string",
      "description": "Project memory key to highlight in pipeline/workbench status.",
      "default": "mcp.extension.pipeline"
    },
    "includeBuildLogTail": {
      "type": "boolean",
      "description": "Whether to include the latest build log tail.",
      "default": false
    },
    "buildLogTailLines": {
      "type": "number",
      "description": "Maximum latest build log tail lines.",
      "default": 80
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
  "memoryKey": "mcp.extension.pipeline",
  "includeBuildLogTail": false
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: read-only inspection, audit, status, schema validation, memory read, or skill read tool.
