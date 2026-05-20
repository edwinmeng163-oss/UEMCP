# unreal.spawn_actor_batch_basic

**Category**: actors
**Title**: Spawn Actor Batch (Basic)
**Risk level**: medium

Spawns multiple fixed-schema actors in one call using class paths, labels, and transforms without freeform property maps.

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
    "classPath": {
      "type": "string",
      "description": "Default actor class path used by any batch item that does not override classPath."
    },
    "selectSpawned": {
      "type": "boolean",
      "description": "Whether to select the spawned actors after the batch completes.",
      "default": true
    },
    "items": {
      "type": "array",
      "description": "Array of actor spawn specs with fixed transform, scale, class, and label fields.",
      "items": {
        "type": "object",
        "properties": {
          "classPath": {
            "type": "string",
            "description": "Native or Blueprint actor class path to spawn."
          },
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
          }
        },
        "additionalProperties": false
      }
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
- Handler: unreal.spawn_actor_batch
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: actors tool with controlled write or workflow side effects.
- Notes: AI-facing fixed-schema wrapper routed to the legacy handler.
