# unreal.material_instance_list

**Category**: material
**Title**: List Material Instances
**Risk level**: read_only

Lists Material Instance assets under a content path with parent material and class metadata. Use this first when a user asks to edit a mesh material, make a material redder, or find editable material instances.

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
    "contentRoot": {
      "type": "string",
      "description": "Content Browser root to search for material instances.",
      "default": "/Game"
    },
    "recursive": {
      "type": "boolean",
      "description": "Whether to include child folders.",
      "default": true
    },
    "classFilter": {
      "type": "string",
      "description": "Optional class path or class-name substring filter."
    },
    "limit": {
      "type": "number",
      "description": "Maximum material instances to return.",
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
  "contentRoot": "/Game",
  "recursive": true,
  "limit": 25
}
```

## Provenance
- Source docs: Docs/MaterialInstanceTools.md
- Reason: Material discovery tool for workflows like list material instances, inspect parameters, then set scalar or vector color/tint values.
- Notes: Phase 1.5 ships list/get/set_scalar/set_vector. Texture and static switch setters are deferred for asset-reference and permutation safety.
