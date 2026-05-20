# unreal.actor_get_transform

**Category**: actors
**Title**: Get Actor Transform
**Risk level**: low

Reads an actor transform in world or relative space without mutating editor state.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "actorPath": {
      "type": "string",
      "description": "Full actor label, actor name, or unique actor path to inspect."
    },
    "space": {
      "type": "string",
      "description": "Transform space to read: world or relative.",
      "default": "world"
    }
  },
  "required": [
    "actorPath"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "actorPath": "Cube",
  "space": "world"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 1 C++ readback inspector: read-only actor transform access closes the get/inspect gap without Python.
- Notes: C++ only. Reads during PIE because it does not mutate editor or world state.
