# unreal.bp_add_event_node

**Category**: blueprint
**Title**: Add Event Node
**Risk level**: medium

Adds a custom event or class override event node to a Blueprint graph and marks the asset dirty.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {
    "blueprintPath": {
      "type": "string",
      "description": "Blueprint asset path to edit."
    },
    "graphName": {
      "type": "string",
      "description": "Target graph name. Defaults to EventGraph.",
      "default": "EventGraph"
    },
    "eventName": {
      "type": "string",
      "description": "Event function name, for example ReceiveBeginPlay. If ownerClassPath is custom, creates a custom event with this name.",
      "default": "ReceiveBeginPlay"
    },
    "ownerClassPath": {
      "type": "string",
      "description": "Class that owns the override event, for example /Script/Engine.Actor. Use custom to create a custom event.",
      "default": "/Script/Engine.Actor"
    },
    "x": {
      "type": "number",
      "description": "Graph X position.",
      "default": 0
    },
    "y": {
      "type": "number",
      "description": "Graph Y position.",
      "default": 0
    }
  },
  "required": [],
  "additionalProperties": false
}
```

## Usage example

_Provenance: schema-minimal_

```json
{}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: blueprint tool with controlled write or workflow side effects.
