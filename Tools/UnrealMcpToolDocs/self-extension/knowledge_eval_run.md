# unreal.knowledge_eval_run

**Category**: self-extension
**Title**: Run Knowledge Evals
**Risk level**: read_only

Runs the offline RAG retrieval evaluation suite under Tools/UnrealMcpKnowledge/Evals/ and reports recall plus per-question diagnostics.

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
    "evalPath": {
      "type": "string",
      "description": "Project-local eval JSON file or directory. Defaults to Tools/UnrealMcpKnowledge/Evals."
    },
    "refreshIndex": {
      "type": "boolean",
      "description": "Refresh the local KnowledgeCard index before running evals.",
      "default": false
    },
    "includeDetails": {
      "type": "boolean",
      "description": "Include per-case structuredContent in the eval output.",
      "default": true
    },
    "limit": {
      "type": "number",
      "description": "Search/recommendation limit used by each eval case.",
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
  "evalPath": "Tools/UnrealMcpKnowledge/Evals",
  "refreshIndex": false,
  "includeDetails": false,
  "limit": 6
}
```

## Provenance
- Source docs: Docs/KnowledgeRag.md
- Reason: Explicit registry: read-only regression eval runner for RAG search, tool recommendation, gap analysis, and workflow recommendation.
- Notes: Reads versioned eval cases from Tools/UnrealMcpKnowledge/Evals by default.
