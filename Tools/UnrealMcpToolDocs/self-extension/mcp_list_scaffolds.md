# unreal.mcp_list_scaffolds

**Category**: self-extension
**Title**: List MCP Scaffolds
**Risk level**: read_only

Lists project-local and shared MCP scaffold drafts or starters so a user can choose one to inspect or apply.

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
    "outputRoot": {
      "type": "string",
      "description": "Project-relative scaffold root to scan.",
      "default": "Tools/UnrealMcpToolScaffolds"
    },
    "includeSavedTestScaffolds": {
      "type": "boolean",
      "description": "Whether to also scan Saved/UnrealMcp/TestScaffolds.",
      "default": true
    },
    "toolNameFilter": {
      "type": "string",
      "description": "Optional case-insensitive filter applied to tool names and scaffold paths."
    },
    "readyOnly": {
      "type": "boolean",
      "description": "Only return scaffolds with all required files and a valid TestRequest.json.",
      "default": false
    },
    "includeFileText": {
      "type": "boolean",
      "description": "Whether to include full file text for each scaffold file.",
      "default": false
    },
    "maxPreviewChars": {
      "type": "number",
      "description": "Maximum per-file preview characters.",
      "default": 1200
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
