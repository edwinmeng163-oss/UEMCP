# unreal.layout_actors_circle

**Category**: actors
**Title**: Layout Actors In Circle
**Risk level**: medium

Arranges selected actors evenly around a circle using the requested origin, radius, and orientation.

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
    "radius": {
      "type": "number",
      "description": "Circle radius in Unreal units.",
      "default": 1000
    },
    "startAngleDegrees": {
      "type": "number",
      "description": "Start angle in degrees.",
      "default": 0
    },
    "arcDegrees": {
      "type": "number",
      "description": "Arc coverage in degrees. Use 360 for a full circle.",
      "default": 360
    },
    "centerX": {
      "type": "number",
      "description": "Optional center X. Omit to use the first target actor's location.",
      "default": 0
    },
    "centerY": {
      "type": "number",
      "description": "Optional center Y. Omit to use the first target actor's location.",
      "default": 0
    },
    "centerZ": {
      "type": "number",
      "description": "Optional center Z. Omit to keep each actor's current Z.",
      "default": 0
    },
    "alignYawToCenter": {
      "type": "boolean",
      "description": "Whether each actor should face the center point.",
      "default": false
    },
    "yawOffset": {
      "type": "number",
      "description": "Additional yaw offset in degrees when alignYawToCenter is enabled.",
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
