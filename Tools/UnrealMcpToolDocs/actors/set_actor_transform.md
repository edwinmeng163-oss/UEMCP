# unreal.set_actor_transform

**Category**: actors
**Title**: Set Actor Transform
**Risk level**: medium

Sets the world transform of a named actor, including location, rotation, and scale fields supplied by the caller.

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
    "actorPath": {
      "type": "string",
      "description": "Exact actor path to move. If omitted, a single selected actor or actorLabel will be used."
    },
    "actorLabel": {
      "type": "string",
      "description": "Exact actor label to move when actorPath is not provided."
    },
    "x": {
      "type": "number",
      "description": "New world X location. Omit to keep current value.",
      "default": 0
    },
    "y": {
      "type": "number",
      "description": "New world Y location. Omit to keep current value.",
      "default": 0
    },
    "z": {
      "type": "number",
      "description": "New world Z location. Omit to keep current value.",
      "default": 0
    },
    "pitch": {
      "type": "number",
      "description": "New world pitch. Omit to keep current value.",
      "default": 0
    },
    "yaw": {
      "type": "number",
      "description": "New world yaw. Omit to keep current value.",
      "default": 0
    },
    "roll": {
      "type": "number",
      "description": "New world roll. Omit to keep current value.",
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
