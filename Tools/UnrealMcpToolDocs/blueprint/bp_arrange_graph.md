# unreal.bp_arrange_graph

**Category**: blueprint
**Title**: Arrange Blueprint Graph
**Risk level**: medium

Repositions nodes in a Blueprint graph into a simple grid layout using caller-supplied spacing and column count.

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
    "originX": {
      "type": "number",
      "description": "Layout origin X.",
      "default": 0
    },
    "originY": {
      "type": "number",
      "description": "Layout origin Y.",
      "default": 0
    },
    "columnSpacing": {
      "type": "number",
      "description": "Horizontal graph spacing.",
      "default": 320
    },
    "rowSpacing": {
      "type": "number",
      "description": "Vertical graph spacing.",
      "default": 180
    },
    "columns": {
      "type": "number",
      "description": "Number of columns before wrapping.",
      "default": 4
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
