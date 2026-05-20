# unreal.asset_move

**Category**: editor
**Title**: Move Asset
**Risk level**: medium

Moves or renames one asset to a new /Game package path using Unreal AssetTools, with optional dry-run planning.

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
    "sourcePath": {
      "type": "string",
      "description": "Source asset package/object path under /Game, for example /Game/Foo/OldAsset or /Game/Foo/OldAsset.OldAsset."
    },
    "destinationPath": {
      "type": "string",
      "description": "Full destination package/object path under /Game including the new asset name."
    },
    "createRedirector": {
      "type": "boolean",
      "description": "Whether the move may leave an UObjectRedirector at the old path for unresolved referencers.",
      "default": true
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview referencers, path validity, and destination collision without moving the asset.",
      "default": false
    }
  },
  "required": [
    "sourcePath",
    "destinationPath"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "sourcePath": "/Game/__UEvolveMcpTest_Migration/SourceAsset",
  "destinationPath": "/Game/__UEvolveMcpTest_Migration/MovedAsset",
  "createRedirector": true,
  "dryRun": true
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 5 migration tool: single-asset move/rename through AssetTools with dry-run planning.
- Notes: PIE-blocked. Dry run reports referencer count and destination collision. Real run uses IAssetTools::RenameAssets and can optionally fix old-path redirectors.
