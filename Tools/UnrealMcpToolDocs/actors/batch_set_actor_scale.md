# unreal.batch_set_actor_scale

**Category**: actors
**Title**: Batch Set Actor Scale
**Risk level**: medium

Sets scale values for multiple named actors in one batch, mutating their transforms in the current editor world.

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
    "scaleX": {
      "type": "number",
      "description": "New relative scale X. Omit to preserve the current value.",
      "default": 1
    },
    "scaleY": {
      "type": "number",
      "description": "New relative scale Y. Omit to preserve the current value.",
      "default": 1
    },
    "scaleZ": {
      "type": "number",
      "description": "New relative scale Z. Omit to preserve the current value.",
      "default": 1
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
