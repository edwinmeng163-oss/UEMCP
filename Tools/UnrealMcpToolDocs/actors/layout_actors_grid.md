# unreal.layout_actors_grid

**Category**: actors
**Title**: Layout Actors In Grid
**Risk level**: medium

Arranges selected actors into a grid with fixed spacing from a requested origin.

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
    "filter": {
      "type": "string",
      "description": "Optional substring filter applied to actor labels, names, classes, and paths."
    },
    "classPath": {
      "type": "string",
      "description": "Optional class path filter, for example /Script/Engine.StaticMeshActor."
    },
    "paths": {
      "type": "array",
      "description": "Optional exact actor paths to lay out.",
      "items": {
        "type": "string"
      }
    },
    "selectedOnly": {
      "type": "boolean",
      "description": "Whether to target only the currently selected actors. If no selectors are provided, selected actors are used automatically.",
      "default": true
    },
    "limit": {
      "type": "number",
      "description": "Maximum number of actors to reposition.",
      "default": 200
    },
    "columns": {
      "type": "number",
      "description": "Number of columns before wrapping to a new row.",
      "default": 5
    },
    "spacingX": {
      "type": "number",
      "description": "Horizontal spacing between actors in Unreal units.",
      "default": 300
    },
    "spacingY": {
      "type": "number",
      "description": "Vertical spacing between actors in Unreal units.",
      "default": 300
    },
    "spacingZ": {
      "type": "number",
      "description": "Z offset applied per row.",
      "default": 0
    },
    "startX": {
      "type": "number",
      "description": "Optional origin X. Omit to use the first target actor's location.",
      "default": 0
    },
    "startY": {
      "type": "number",
      "description": "Optional origin Y. Omit to use the first target actor's location.",
      "default": 0
    },
    "startZ": {
      "type": "number",
      "description": "Optional origin Z. Omit to use the first target actor's location.",
      "default": 0
    },
    "pitch": {
      "type": "number",
      "description": "Optional uniform pitch applied to every actor. Omit to keep current rotation.",
      "default": 0
    },
    "yaw": {
      "type": "number",
      "description": "Optional uniform yaw applied to every actor. Omit to keep current rotation.",
      "default": 0
    },
    "roll": {
      "type": "number",
      "description": "Optional uniform roll applied to every actor. Omit to keep current rotation.",
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
- Reason: Explicit registry: actors tool with controlled write or workflow side effects.
