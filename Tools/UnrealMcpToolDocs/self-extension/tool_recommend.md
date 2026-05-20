# unreal.tool_recommend

**Category**: self-extension
**Title**: Recommend MCP Tools
**Risk level**: read_only

Given a task description, returns ranked tool suggestions from the local registry plus knowledge index.

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
      "description": "Natural-language task to map to existing tools and workflows."
    },
    "riskMax": {
      "type": "string",
      "description": "Maximum risk to recommend: read_only, low, medium, high, or critical.",
      "default": "critical"
    },
    "limit": {
      "type": "number",
      "description": "Maximum tool recommendations.",
      "default": 8
    },
    "includeKnowledge": {
      "type": "boolean",
      "description": "Include top matching KnowledgeCards when the index exists.",
      "default": true
    },
    "includeWorkflowDraft": {
      "type": "boolean",
      "description": "Include a lightweight workflow draft using preview/search/tool/verify gates.",
      "default": true
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
  "task": "Create a Widget HUD and verify it with existing tools",
  "riskMax": "medium",
  "limit": 6,
  "includeKnowledge": true,
  "includeWorkflowDraft": true
}
```

## Provenance
- Source docs: Docs/KnowledgeRag.md
- Reason: Explicit registry: read-only recommendation of existing MCP tools and workflow gates before self-extension.
- Notes: Uses ToolRegistry policy and optionally top KnowledgeCards when the local index exists.
