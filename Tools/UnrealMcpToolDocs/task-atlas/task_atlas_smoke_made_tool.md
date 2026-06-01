# unreal.task_atlas_smoke_made_tool

**Category**: task-atlas
**Title**: Smoke Task Atlas Made Tool
**Risk level**: high

Run or preview a smoke test for a generated composite user tool and record failure diagnostics without deleting the tool.

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
    "toolName"
  ],
  "properties": {
    "toolName": {
      "type": "string",
      "minLength": 6,
      "pattern": "^user\\.[A-Za-z0-9_]+$"
    },
    "dryRun": {
      "type": "boolean",
      "default": true
    },
    "acceptChangedHashes": {
      "type": "boolean",
      "default": false
    }
  }
}
```

## Usage example

_Provenance: schema-minimal_

```json
{
  "toolName": "user.task_atlas_slug",
  "dryRun": true
}
```

## Provenance

- Source docs: Docs/TaskAtlas.md
- Reason: v0.31 Task Atlas generated composite smoke wrapper over TaskAtlasService::SmokeMadeTool.
- Notes: AssistantRun approval is required for dryRun=false because real smoke can execute user Python and can update generated failure markers.
