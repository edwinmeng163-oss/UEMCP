# unreal.mcp_inspect_scaffold

**Category**: self-extension
**Title**: Inspect MCP Scaffold
**Risk level**: read_only

Reads a scaffold draft and reports metadata, patch fragments, dependencies, and integration readiness without writing source.

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
    "toolName": {
      "type": "string",
      "description": "MCP tool name whose scaffold should be inspected. Used when scaffoldDir is empty."
    },
    "scaffoldDir": {
      "type": "string",
      "description": "Project-local scaffold directory to inspect. To inspect shared repo recipes, pass toolName and leave scaffoldDir empty."
    },
    "outputRoot": {
      "type": "string",
      "description": "Project-relative scaffold root used with toolName.",
      "default": "Tools/UnrealMcpToolScaffolds"
    },
    "includeFileText": {
      "type": "boolean",
      "description": "Whether to include full file text for scaffold files.",
      "default": false
    },
    "maxPreviewChars": {
      "type": "number",
      "description": "Maximum per-file preview characters.",
      "default": 2000
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
  "toolName": "unreal.bad_patch_fixture",
  "scaffoldDir": "Tools/UnrealMcpTests/Fixtures/BadPatchScaffold"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: read-only inspection, audit, status, schema validation, memory read, or skill read tool.
