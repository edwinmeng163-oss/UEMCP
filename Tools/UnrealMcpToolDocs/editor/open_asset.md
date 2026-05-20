# unreal.open_asset

**Category**: editor
**Title**: Open Asset
**Risk level**: low

Opens an asset in the Unreal Editor asset editor by Content Browser or object path.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {
    "path": {
      "type": "string",
      "description": "Asset path such as /Game/Variant_TwinStick/Blueprints/BP_TwinStickCharacter."
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
- Reason: Explicit registry: low-risk editor session operation.
