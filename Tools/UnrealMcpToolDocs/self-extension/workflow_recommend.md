# unreal.workflow_recommend

**Category**: self-extension
**Title**: Recommend MCP Workflow
**Risk level**: read_only

Given a goal, returns a step-by-step workflow composed of existing MCP tool calls.

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
      "description": "Natural-language task to convert into a recommended MCP workflow draft."
    },
    "riskMax": {
      "type": "string",
      "description": "Maximum tool risk to include in recommendations: read_only, low, medium, high, or critical.",
      "default": "critical"
    },
    "limit": {
      "type": "number",
      "description": "Maximum recommended task-specific tools to include as skipped placeholder steps.",
      "default": 5
    },
    "includeKnowledge": {
      "type": "boolean",
      "description": "Include knowledge_search as an early workflow step.",
      "default": true
    },
    "dryRun": {
      "type": "boolean",
      "description": "Set the generated workflow_run draft dryRun flag.",
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
  "task": "Create a Blueprint actor setup with verification gates",
  "riskMax": "medium",
  "limit": 5,
  "includeKnowledge": true,
  "dryRun": true
}
```

## Provenance
- Source docs: Docs/KnowledgeRag.md
- Reason: Explicit registry: read-only workflow_run draft generator that combines RAG, ToolRegistry policy, gap analysis, snapshot gates, and verification.
- Notes: Generated task-specific steps are skipped placeholders until exact arguments are filled from retrieved evidence.
