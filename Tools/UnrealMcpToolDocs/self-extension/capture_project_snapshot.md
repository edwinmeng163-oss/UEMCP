# unreal.capture_project_snapshot

**Category**: self-extension
**Title**: Capture Project Snapshot
**Risk level**: low

Captures a project state snapshot for later objective diffing of actors, assets, Blueprints, widgets, memory, and skills.

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
    "snapshotName": {
      "type": "string",
      "description": "Optional snapshot name. Defaults to a UTC timestamp."
    },
    "assetPath": {
      "type": "string",
      "description": "Content path to scan for assets.",
      "default": "/Game"
    },
    "includeActors": {
      "type": "boolean",
      "description": "Include current level actors.",
      "default": true
    },
    "includeAssets": {
      "type": "boolean",
      "description": "Include assets under assetPath.",
      "default": true
    },
    "includeBlueprints": {
      "type": "boolean",
      "description": "Include Blueprint assets under assetPath.",
      "default": true
    },
    "includeWidgets": {
      "type": "boolean",
      "description": "Include Widget Blueprint assets under assetPath.",
      "default": true
    },
    "includeMemory": {
      "type": "boolean",
      "description": "Include project memory summaries.",
      "default": true
    },
    "includeSkills": {
      "type": "boolean",
      "description": "Include promoted project skills.",
      "default": true
    },
    "actorLimit": {
      "type": "number",
      "description": "Maximum actors to serialize.",
      "default": 500
    },
    "assetLimit": {
      "type": "number",
      "description": "Maximum assets per asset category to serialize.",
      "default": 1000
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
  "snapshotName": "uevolve-selfext-before",
  "actorLimit": 25,
  "assetLimit": 25
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: writes local Saved/UnrealMcp project snapshots for objective before/after verification.
- Notes: Writes only under Saved/UnrealMcp/ProjectSnapshots.
