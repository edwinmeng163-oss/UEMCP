# unreal.material_instance_set_scalar

**Category**: material
**Title**: Set Material Scalar
**Risk level**: medium

Sets a scalar parameter on a Material Instance Constant with strict numeric input and previous-value readback. Use this for brightness, roughness, metallic, opacity, intensity, or other numeric material edits.

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
    "materialInstancePath": {
      "type": "string",
      "description": "Material Instance Constant asset path to edit."
    },
    "parameterName": {
      "type": "string",
      "description": "Scalar parameter name to set."
    },
    "value": {
      "type": "number",
      "description": "New scalar value. Must be a JSON number, not a string.",
      "default": 1
    },
    "save": {
      "type": "boolean",
      "description": "Whether to save the Material Instance package after editing.",
      "default": false
    }
  },
  "required": [
    "materialInstancePath",
    "parameterName",
    "value"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "materialInstancePath": "/Game/__UEvolveMcpTest/Materials/MI_ParameterSmoke",
  "parameterName": "Brightness",
  "value": 0.5,
  "save": false
}
```

## Provenance
- Source docs: Docs/MaterialInstanceTools.md
- Reason: Type-safe scalar setter for material instance parameter changes; rejects value strings with errorKind TypeMismatch instead of coercing.
- Notes: Does not set texture or static switch parameters; those are future v0.16.x work because they need asset-reference and permutation invalidation discipline.
