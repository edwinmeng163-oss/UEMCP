# unreal.clear_level_environment

**Category**: actors
**Title**: Clear Level Environment
**Risk level**: high

Deletes common level environment actors such as sky, fog, lights, and floor meshes to prepare a clean sandbox; destructive in the open map.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "filter": {
      "type": "string",
      "description": "Optional substring filter applied to actor labels, names, classes, and paths. If omitted with no classPath or paths, all level actors are targeted."
    },
    "classPath": {
      "type": "string",
      "description": "Optional class path filter, for example RecastNavMesh or /Script/Engine.PlayerStart."
    },
    "paths": {
      "type": "array",
      "description": "Optional exact actor paths to clear.",
      "items": {
        "type": "string"
      }
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview actors that would be destroyed without deleting them.",
      "default": true
    },
    "confirmClearAll": {
      "type": "boolean",
      "description": "Required with dryRun=false when no filter, classPath, or paths are provided.",
      "default": false
    },
    "clearSelection": {
      "type": "boolean",
      "description": "Whether to clear actor selection before and after the operation.",
      "default": true
    },
    "maxPasses": {
      "type": "number",
      "description": "Maximum destroy passes. Extra passes catch actors such as navigation data that can be recreated after the first pass.",
      "default": 3
    },
    "limit": {
      "type": "number",
      "description": "Maximum actors to destroy per pass.",
      "default": 10000
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
  "dryRun": true,
  "filter": "UEvolveMcpSmokeNonexistent",
  "limit": 10
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: high-impact write/build/rollback/destructive editor operation.
