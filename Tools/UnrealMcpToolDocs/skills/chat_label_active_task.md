# unreal.chat_label_active_task

**Category**: skills
**Title**: Label Active Chat Task
**Risk level**: low

Sets or clears a user-visible label for subsequent launch-session ActivityLog events.

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
    "label": {
      "type": "string",
      "description": "User-set label for the current launch session; empty clears.",
      "maxLength": 2000
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
  "label": "FPS layout pass"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: low-risk launch-session label setter for ActivityLog task labels.
