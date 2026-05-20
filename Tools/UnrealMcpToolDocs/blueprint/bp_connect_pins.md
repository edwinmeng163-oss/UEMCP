# unreal.bp_connect_pins

**Category**: blueprint
**Title**: Connect Blueprint Pins
**Risk level**: medium

Connects two Blueprint node pins by node GUID and pin name after K2 schema validation.

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
    "blueprintPath": {
      "type": "string",
      "description": "Blueprint asset path to edit."
    },
    "graphName": {
      "type": "string",
      "description": "Target graph name. Defaults to EventGraph.",
      "default": "EventGraph"
    },
    "fromNodeGuid": {
      "type": "string",
      "description": "Source node GUID returned by a bp_add_* tool."
    },
    "fromPin": {
      "type": "string",
      "description": "Source pin name or display name."
    },
    "toNodeGuid": {
      "type": "string",
      "description": "Target node GUID returned by a bp_add_* tool."
    },
    "toPin": {
      "type": "string",
      "description": "Target pin name or display name."
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
  "blueprintPath": "/Game/UEvolveMcpTests/Blueprint/BP_DoesNotExist",
  "graphName": "EventGraph",
  "fromNodeGuid": "not-a-guid",
  "fromPin": "Then",
  "toNodeGuid": "also-not-a-guid",
  "toPin": "Execute"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: blueprint tool with controlled write or workflow side effects.
