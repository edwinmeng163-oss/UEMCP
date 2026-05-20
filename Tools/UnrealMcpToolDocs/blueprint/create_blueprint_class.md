# unreal.create_blueprint_class

**Category**: blueprint
**Title**: Create Blueprint Class
**Risk level**: medium

Creates a Blueprint asset from a parent class, optionally compiling it and opening the editor after creation.

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
    "assetPath": {
      "type": "string",
      "description": "Blueprint asset path to create, for example /Game/Blueprints/BP_NewActor."
    },
    "parentClass": {
      "type": "string",
      "description": "Native or Blueprint parent class path.",
      "default": "/Script/Engine.Actor"
    },
    "openAfterCreate": {
      "type": "boolean",
      "description": "Whether to open the asset editor after creation.",
      "default": true
    },
    "compile": {
      "type": "boolean",
      "description": "Whether to compile the new Blueprint immediately.",
      "default": true
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
  "assetPath": "/Game/__UEvolveMcpTest/Blueprint/BP_McpGraphSmoke",
  "parentClass": "/Script/Engine.Actor",
  "openAfterCreate": false,
  "compile": true
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: Blueprint write tool with automated disposable sandbox category coverage.
