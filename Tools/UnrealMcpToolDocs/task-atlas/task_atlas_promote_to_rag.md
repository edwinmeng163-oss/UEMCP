# unreal.task_atlas_promote_to_rag

**Category**: task-atlas
**Title**: Promote Task Atlas Task To RAG
**Risk level**: medium

Promote a Task Atlas task or draft into a RAG knowledge source and refresh the knowledge index.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "additionalProperties": false,
  "required": [
    "taskId"
  ],
  "properties": {
    "taskId": {
      "type": "string",
      "minLength": 1
    },
    "dryRun": {
      "type": "boolean",
      "default": false
    },
    "refreshIndex": {
      "type": "boolean",
      "default": true
    }
  }
}
```

## Usage example

_Provenance: schema-minimal_

```json
{
  "taskId": "task-20260601",
  "dryRun": true
}
```

## Provenance

- Source docs: Docs/TaskAtlas.md
- Reason: v0.31 Task Atlas RAG promotion wrapper over TaskAtlasService::PromoteToRag.
- Notes: AssistantRun approval is required for dryRun=false because this writes long-lived KnowledgeSources.
