# unreal.project_version_migration

**Category**: editor
**Title**: Project Version Migration
**Risk level**: high

Updates a .uproject EngineAssociation between UE 5.6 and UE 5.7 and reports remaining manual rebuild steps.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "targetEngineVersion": {
      "type": "string",
      "description": "Target EngineAssociation value. Supported values: 5.6 or 5.7."
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview the EngineAssociation edit and compatibility warnings without writing the .uproject.",
      "default": true
    },
    "projectFilePath": {
      "type": "string",
      "description": "Absolute .uproject path to edit. Defaults to the current project file."
    }
  },
  "required": [
    "targetEngineVersion"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "targetEngineVersion": "5.7",
  "dryRun": true
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 5 migration tool: reversible .uproject EngineAssociation edit between UE 5.6 and UE 5.7.
- Notes: PIE-blocked. Default dryRun=true. Does not cook, regenerate project files, run UnrealVersionSelector, or rebuild binaries.
