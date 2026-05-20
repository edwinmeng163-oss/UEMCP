# unreal.batch_configure_static_mesh_actors

**Category**: actors
**Title**: Configure Static Mesh Actors
**Risk level**: low

Applies static mesh component settings to a batch of actors, such as mesh, material, mobility, collision, and visibility fields when supplied.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
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
      "description": "Optional class path filter, for example /Script/Engine.PointLight."
    },
    "paths": {
      "type": "array",
      "description": "Optional exact actor paths to target.",
      "items": {
        "type": "string"
      }
    },
    "selectedOnly": {
      "type": "boolean",
      "description": "Whether to target only the currently selected actors. If no selectors are provided, selected actors are used automatically.",
      "default": false
    },
    "limit": {
      "type": "number",
      "description": "Maximum number of actors to affect.",
      "default": 200
    },
    "staticMeshPath": {
      "type": "string",
      "description": "Static mesh asset path to assign, for example /Game/LevelPrototyping/Meshes/SM_Cube."
    },
    "materialPath": {
      "type": "string",
      "description": "Optional material or material instance asset path to assign."
    },
    "materialSlot": {
      "type": "number",
      "description": "Material slot index to update when materialPath is provided.",
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
