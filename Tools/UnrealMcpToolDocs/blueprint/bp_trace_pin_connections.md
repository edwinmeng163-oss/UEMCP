# unreal.bp_trace_pin_connections

**Category**: blueprint
**Title**: Trace Blueprint Pin Connections
**Risk level**: read_only

Read-only inspection of pin defaults and linked node/pin targets for one Blueprint node.

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
    "blueprintPath": {
      "type": "string",
      "description": "Blueprint asset path to inspect."
    },
    "graphName": {
      "type": "string",
      "description": "Graph name containing the node.",
      "default": "EventGraph"
    },
    "nodeGuid": {
      "type": "string",
      "description": "Node GUID to inspect."
    },
    "pinName": {
      "type": "string",
      "description": "Optional pin name. If omitted, all node pins are traced."
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
  "blueprintPath": "/Game/__UEvolveMcpTest/Blueprint/BP_McpGraphSmoke"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: Blueprint pin-link inspection tool for AI self-verification.
