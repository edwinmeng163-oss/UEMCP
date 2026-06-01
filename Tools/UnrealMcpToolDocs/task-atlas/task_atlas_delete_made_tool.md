# unreal.task_atlas_delete_made_tool

**Category**: task-atlas
**Title**: Delete Task Atlas Made Tool
**Risk level**: high

Remove a Task Atlas generated user composite and reload the user registry.

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
    "toolName",
    "confirm"
  ],
  "properties": {
    "toolName": {
      "type": "string",
      "minLength": 6,
      "pattern": "^user\\.[A-Za-z0-9_]+$"
    },
    "confirm": {
      "type": "boolean",
      "const": true
    },
    "dryRun": {
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
  "confirm": true
}
```

## Provenance

- Source docs: Docs/TaskAtlas.md
- Reason: v0.31 Task Atlas generated composite deletion wrapper with confirm guard.
- Notes: AssistantRun approval is required for dryRun=false deletion. The wrapper refuses confirm=false and the service refuses non-Task-Atlas-generated tools.
