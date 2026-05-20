# unreal.dependency_remap

**Category**: editor
**Title**: Remap Asset Dependencies
**Risk level**: high

Replaces references to one asset with references to another asset using ObjectTools consolidation without deleting the source by default.

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
    "fromAssetPath": {
      "type": "string",
      "description": "Source asset whose referencers should be rewritten."
    },
    "toAssetPath": {
      "type": "string",
      "description": "Destination asset that should replace source references."
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview type compatibility and referencing asset count without rewriting references.",
      "default": true
    },
    "deleteSourceAfter": {
      "type": "boolean",
      "description": "Delete the source asset after references are consolidated in a real run.",
      "default": false
    }
  },
  "required": [
    "fromAssetPath",
    "toAssetPath"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "fromAssetPath": "/Game/__UEvolveMcpTest_Migration/SourceAsset",
  "toAssetPath": "/Game/__UEvolveMcpTest_Migration/TargetAsset",
  "dryRun": true,
  "deleteSourceAfter": false
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 5 migration tool: replace references to one asset with another using ObjectTools consolidation.
- Notes: PIE-blocked. Default dryRun=true. Source deletion is opt-in with deleteSourceAfter=true.
