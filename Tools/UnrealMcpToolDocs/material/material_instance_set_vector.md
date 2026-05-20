# unreal.material_instance_set_vector

**Category**: material
**Title**: Set Material Vector
**Risk level**: medium

Sets an RGBA vector parameter on a Material Instance Constant with strict object input and previous-value readback. Use this for color, tint, emissive color, or make this material redder requests.

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
      "description": "Vector parameter name to set."
    },
    "value": {
      "type": "object",
      "properties": {
        "r": {
          "type": "number",
          "description": "Red channel value.",
          "default": 1
        },
        "g": {
          "type": "number",
          "description": "Green channel value.",
          "default": 1
        },
        "b": {
          "type": "number",
          "description": "Blue channel value.",
          "default": 1
        },
        "a": {
          "type": "number",
          "description": "Alpha channel value.",
          "default": 1
        }
      },
      "required": [
        "r",
        "g",
        "b",
        "a"
      ],
      "additionalProperties": false
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
  "parameterName": "BaseColor",
  "value": {
    "r": 1.0,
    "g": 0.25,
    "b": 0.2,
    "a": 1.0
  },
  "save": false
}
```

## Provenance
- Source docs: Docs/MaterialInstanceTools.md
- Reason: Type-safe vector setter for material colors and tints; useful when the user says make this material redder, bluer, brighter, or change its emissive color.
- Notes: Does not set texture or static switch parameters; those are future v0.16.x work because they need asset-reference and permutation invalidation discipline.
