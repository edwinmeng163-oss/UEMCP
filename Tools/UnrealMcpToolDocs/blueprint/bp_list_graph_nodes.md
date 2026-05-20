# unreal.bp_list_graph_nodes

**Category**: blueprint
**Title**: List Blueprint Graph Nodes
**Risk level**: read_only

Read-only inspection of Blueprint graphs, nodes, pins, and existing links.

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
      "description": "Optional graph name. If omitted, all standard Blueprint graphs are listed."
    },
    "includePins": {
      "type": "boolean",
      "description": "Whether to include pin details and links for each node.",
      "default": true
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
  "blueprintPath": "/Game/__UEvolveMcpTest/Blueprint/BP_McpGraphSmoke",
  "includePins": false
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: Blueprint inspection tool for AI self-verification before and after graph edits.
