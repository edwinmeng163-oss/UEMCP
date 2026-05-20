# unreal.bp_add_for_each_node

**Category**: blueprint
**Title**: Add ForEach Node
**Risk level**: medium

Adds a StandardMacros ForEach-style macro node to a Blueprint graph for array iteration flow.

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
    "macroName": {
      "type": "string",
      "description": "StandardMacros macro graph name, usually ForEachLoop or ForEachLoopWithBreak.",
      "default": "ForEachLoop"
    },
    "x": {
      "type": "number",
      "description": "Graph X position.",
      "default": 400
    },
    "y": {
      "type": "number",
      "description": "Graph Y position.",
      "default": 180
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
