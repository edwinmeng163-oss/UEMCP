# unreal.select_actors

**Category**: actors
**Title**: Select Actors
**Risk level**: low

Selects actors in the level editor by label or path, optionally replacing the current selection.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: category

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
      "description": "Optional class path filter, for example /Script/Engine.PlayerStart."
    },
    "paths": {
      "type": "array",
      "description": "Optional exact actor paths to select.",
      "items": {
        "type": "string"
      }
    },
    "clearSelection": {
      "type": "boolean",
      "description": "Whether to clear the current actor selection before selecting matches.",
      "default": true
    },
    "limit": {
      "type": "number",
      "description": "Maximum number of actors to select.",
      "default": 200
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
  "filter": "UEvolveMcpSmokeNonexistent",
  "clearSelection": true,
  "limit": 5
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: actors tool with controlled write or workflow side effects.
