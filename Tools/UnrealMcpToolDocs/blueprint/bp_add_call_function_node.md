# unreal.bp_add_call_function_node

**Category**: blueprint
**Title**: Add Function Call Node
**Risk level**: medium

Adds a call node for a target function to a Blueprint graph so later pin-connection tools can wire it.

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
    "functionClassPath": {
      "type": "string",
      "description": "Class path that owns the function, for example /Script/Engine.KismetSystemLibrary."
    },
    "functionName": {
      "type": "string",
      "description": "Function name to call."
    },
    "x": {
      "type": "number",
      "description": "Graph X position.",
      "default": 200
    },
    "y": {
      "type": "number",
      "description": "Graph Y position.",
      "default": 0
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
