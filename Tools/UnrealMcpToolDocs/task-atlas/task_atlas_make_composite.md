# unreal.task_atlas_make_composite

**Category**: task-atlas
**Title**: Make Task Atlas Composite
**Risk level**: high

Turn a Task Atlas task into either a generated preview composite Python user tool or a document-only draft, depending on eligibility.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
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
    "preferDocumentOnly": {
      "type": "boolean",
      "default": false
    },
    "forceWriteEvenIfBlocked": {
      "type": "boolean",
      "default": false
    },
    "overrideStepArgs": {
      "type": "array",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "required": [
          "ordinal",
          "argumentsJson"
        ],
        "properties": {
          "ordinal": {
            "type": "integer",
            "minimum": 0
          },
          "toolName": {
            "type": "string"
          },
          "argumentsJson": {
            "type": "string"
          }
        }
      },
      "default": []
    }
  }
}
```

## Usage example

_Provenance: schema-minimal_

```json
{
  "taskId": "task-20260601"
}
```

## Provenance

- Source docs: Docs/TaskAtlas.md
- Reason: v0.31 Task Atlas Make Tool Set MCP wrapper over TaskAtlasService::MakeComposite.
- Notes: AssistantRun approval is required for generated PyTools writes and forceWriteEvenIfBlocked=true; preferDocumentOnly=true is the dry-run equivalent.
