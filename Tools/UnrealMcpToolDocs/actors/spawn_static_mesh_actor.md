# unreal.spawn_static_mesh_actor

**Category**: actors
**Title**: Spawn Static Mesh Actor
**Risk level**: medium

Spawns a StaticMeshActor with a mesh asset, transform, label, and optional material assignment in the current level.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "x": {
      "type": "number",
      "description": "Spawn location X.",
      "default": 0
    },
    "y": {
      "type": "number",
      "description": "Spawn location Y.",
      "default": 0
    },
    "z": {
      "type": "number",
      "description": "Spawn location Z.",
      "default": 0
    },
    "pitch": {
      "type": "number",
      "description": "Spawn rotation pitch.",
      "default": 0
    },
    "yaw": {
      "type": "number",
      "description": "Spawn rotation yaw.",
      "default": 0
    },
    "roll": {
      "type": "number",
      "description": "Spawn rotation roll.",
      "default": 0
    },
    "sx": {
      "type": "number",
      "description": "Spawn scale X.",
      "default": 1
    },
    "sy": {
      "type": "number",
      "description": "Spawn scale Y.",
      "default": 1
    },
    "sz": {
      "type": "number",
      "description": "Spawn scale Z.",
      "default": 1
    },
    "label": {
      "type": "string",
      "description": "Optional actor label after spawning."
    },
    "staticMeshPath": {
      "type": "string",
      "description": "Static mesh asset path to assign after spawning, for example /Game/LevelPrototyping/Meshes/SM_Cube."
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

_Provenance: fixture-derived_

```json
{
  "staticMeshPath": "/Engine/BasicShapes/Cube.Cube",
  "label": "UEvolveMcpTest_StaticMeshActor",
  "x": 0,
  "y": 0,
  "z": 120,
  "sx": 1,
  "sy": 1,
  "sz": 1
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: actors tool with controlled write or workflow side effects.
