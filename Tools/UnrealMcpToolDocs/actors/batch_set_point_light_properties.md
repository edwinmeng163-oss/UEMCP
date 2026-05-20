# unreal.batch_set_point_light_properties

**Category**: actors
**Title**: Batch Set Point Lights
**Risk level**: low

Configures point-light properties such as intensity, color, attenuation, and visibility on matching light actors.

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
    "filter": {
      "type": "string",
      "description": "Optional substring filter applied to actor labels, names, classes, and paths."
    },
    "classPath": {
      "type": "string",
      "description": "Optional class path filter, for example /Script/Engine.PointLight."
    },
    "paths": {
      "type": "array",
      "description": "Optional exact actor paths to target.",
      "items": {
        "type": "string"
      }
    },
    "selectedOnly": {
      "type": "boolean",
      "description": "Whether to target only the currently selected actors. If no selectors are provided, selected actors are used automatically.",
      "default": false
    },
    "limit": {
      "type": "number",
      "description": "Maximum number of actors to affect.",
      "default": 200
    },
    "intensity": {
      "type": "number",
      "description": "Point light intensity.",
      "default": 5000
    },
    "attenuationRadius": {
      "type": "number",
      "description": "Point light attenuation radius.",
      "default": 1000
    },
    "sourceRadius": {
      "type": "number",
      "description": "Point light source radius.",
      "default": 0
    },
    "softSourceRadius": {
      "type": "number",
      "description": "Point light soft source radius.",
      "default": 0
    },
    "temperature": {
      "type": "number",
      "description": "Point light temperature in Kelvin.",
      "default": 6500
    },
    "useTemperature": {
      "type": "boolean",
      "description": "Whether the point light should use color temperature.",
      "default": false
    },
    "castShadows": {
      "type": "boolean",
      "description": "Whether the point light should cast shadows.",
      "default": true
    },
    "visible": {
      "type": "boolean",
      "description": "Whether the point light component should be visible.",
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
- Reason: Explicit registry: actors tool with controlled write or workflow side effects.
