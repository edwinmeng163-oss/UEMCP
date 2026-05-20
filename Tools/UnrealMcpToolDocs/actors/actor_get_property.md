# unreal.actor_get_property

**Category**: actors
**Title**: Get Actor Property
**Risk level**: low

Reads a single UProperty value from a named actor, including supported dot-paths through struct or object properties.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "actorPath": {
      "type": "string",
      "description": "Full actor label, actor name, or unique actor path to inspect."
    },
    "propertyName": {
      "type": "string",
      "description": "UProperty name or dot-path, for example bHidden, Tags, or StaticMeshComponent.StaticMesh."
    }
  },
  "required": [
    "actorPath",
    "propertyName"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "actorPath": "Cube",
  "propertyName": "bHidden"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 1 C++ readback inspector: read-only actor property access closes the get/inspect gap without Python.
- Notes: C++ only. Supports dot notation through reflected struct and object properties; unsupported values fall back to exported text where possible.
