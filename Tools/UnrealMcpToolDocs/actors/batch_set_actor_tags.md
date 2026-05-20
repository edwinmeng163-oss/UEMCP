# unreal.batch_set_actor_tags

**Category**: actors
**Title**: Batch Set Actor Tags
**Risk level**: medium

Replaces or appends actor tags for multiple named actors so later selection, layout, or verification can target the same group.

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
    "tags": {
      "type": "array",
      "description": "Tags to add or assign to the target actors.",
      "items": {
        "type": "string"
      }
    },
    "replaceExisting": {
      "type": "boolean",
      "description": "Whether to replace the existing actor tags instead of appending unique tags.",
      "default": false
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
