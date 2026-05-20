# unreal.bp_set_pin_default

**Category**: blueprint
**Title**: Set Blueprint Pin Default
**Risk level**: medium

Sets the default value for a Blueprint node pin by graph, node GUID, and pin name.

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
    "blueprintPath": {
      "type": "string",
      "description": "Blueprint asset path to edit."
    },
    "graphName": {
      "type": "string",
      "description": "Target graph name. Defaults to EventGraph.",
      "default": "EventGraph"
    },
    "nodeGuid": {
      "type": "string",
      "description": "Node GUID returned by a bp_add_* tool."
    },
    "pinName": {
      "type": "string",
      "description": "Pin name or display name to update."
    },
    "value": {
      "type": "string",
      "description": "New default value text."
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
- Reason: Explicit registry: blueprint tool with controlled write or workflow side effects.
