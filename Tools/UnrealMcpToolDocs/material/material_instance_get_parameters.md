# unreal.material_instance_get_parameters

**Category**: material
**Title**: Get Material Parameters
**Risk level**: read_only

Reads scalar, vector, texture, and static switch parameters from a Material Instance. Use this before changing brightness, roughness, color, tint, emissive color, or other material parameters.

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
    "materialInstancePath": {
      "type": "string",
      "description": "Material Instance asset path to inspect."
    },
    "includeInherited": {
      "type": "boolean",
      "description": "Include inherited parent parameter values as fromParent=true.",
      "default": true
    }
  },
  "required": [
    "materialInstancePath"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "materialInstancePath": "/Game/__UEvolveMcpTest/Materials/MI_ParameterSmoke",
  "includeInherited": true
}
```

## Provenance
- Source docs: Docs/MaterialInstanceTools.md
- Reason: Material inspection tool for choosing whether to set a scalar value such as brightness or a vector value such as BaseColor/Tint to make a material redder.
- Notes: Read-only. Returns fromParent=true when a value is inherited and includeInherited=true.
