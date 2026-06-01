# unreal.task_atlas_list_made_tools

**Category**: task-atlas
**Title**: List Task Atlas Made Tools
**Risk level**: read_only

List Task Atlas generated user composites for CLI diagnostics and Made Tools views.

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
  "additionalProperties": false,
  "properties": {
    "includeStale": {
      "type": "boolean",
      "default": true
    },
    "includeFailureMarkers": {
      "type": "boolean",
      "default": true
    },
    "sourceTaskId": {
      "type": "string",
      "default": ""
    }
  },
  "required": []
}
```

## Usage example

_Provenance: schema-minimal_

```json
{}
```

## Provenance

- Source docs: Docs/TaskAtlas.md
- Reason: v0.31 read-only Task Atlas generated composite listing wrapper.
- Notes: AssistantRun approval is not required. This tool only lists registry and generated-dir metadata.
