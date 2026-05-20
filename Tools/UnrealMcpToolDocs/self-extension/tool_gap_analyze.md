# unreal.tool_gap_analyze

**Category**: self-extension
**Title**: Analyze MCP Tool Gap
**Risk level**: read_only

Detects functional gaps in the current tool surface relative to a workflow goal as a read-only audit.

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
    "task": {
      "type": "string",
      "description": "Natural-language task to analyze for tool coverage gaps."
    },
    "riskMax": {
      "type": "string",
      "description": "Maximum existing-tool risk to consider: read_only, low, medium, high, or critical.",
      "default": "critical"
    },
    "limit": {
      "type": "number",
      "description": "Maximum existing tool recommendations to include.",
      "default": 6
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
  "task": "Build a Widget HUD with existing UMG tools and verify the result",
  "riskMax": "medium",
  "limit": 6
}
```

## Provenance
- Source docs: Docs/KnowledgeRag.md
- Reason: Explicit registry: read-only gap analysis that decides whether to use existing tools, compose a workflow, or scaffold a new MCP tool.
- Notes: Uses ToolRegistry policy and RAG-oriented scoring; returns scaffold hints and self-extension gate steps.
