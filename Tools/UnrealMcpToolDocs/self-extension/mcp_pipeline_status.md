# unreal.mcp_pipeline_status

**Category**: self-extension
**Title**: Get Pipeline Status
**Risk level**: read_only

Reads project memory, locks, manifests, build logs, and test state for the active MCP extension pipeline.

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
    "memoryKey": {
      "type": "string",
      "description": "Project memory key to inspect.",
      "default": "mcp.extension.pipeline"
    },
    "includeAllMemory": {
      "type": "boolean",
      "description": "Whether memory summaries should include full content payloads.",
      "default": false
    },
    "includeBuildLogTail": {
      "type": "boolean",
      "description": "Whether to include the latest build log tail.",
      "default": true
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

_Provenance: schema-minimal_

```json
{}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: read-only inspection, audit, status, schema validation, memory read, or skill read tool.
