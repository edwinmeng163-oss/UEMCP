# unreal.save_dirty_packages

**Category**: editor
**Title**: Save Dirty Packages
**Risk level**: medium

Saves currently dirty packages in the editor, optionally constrained by path or package type.

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
    "saveMaps": {
      "type": "boolean",
      "description": "Whether to save dirty maps.",
      "default": true
    },
    "saveAssets": {
      "type": "boolean",
      "description": "Whether to save dirty content assets.",
      "default": true
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
