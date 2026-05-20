# unreal.bp_delete_node

**Category**: blueprint
**Title**: Delete Blueprint Node
**Risk level**: low

Deletes a user-deletable Blueprint graph node by NodeGuid and reports the severed pin links.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: core

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
      "description": "Graph name containing the node. Defaults to EventGraph.",
      "default": "EventGraph"
    },
    "nodeGuid": {
      "type": "string",
      "description": "Node GUID to delete, as 32 hex digits or hyphenated GUID text."
    }
  },
  "required": [
    "blueprintPath",
    "nodeGuid"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "blueprintPath": "/Game/__UEvolveMcpTest_chunk2a/BP_RefactorSmoke",
  "graphName": "EventGraph",
  "nodeGuid": "00000000000000000000000000000000"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 2a C++ Blueprint refactor tool: bounded node deletion by GUID without Python.
- Notes: PIE-blocked. Returns BLUEPRINT_NOT_FOUND, GRAPH_NOT_FOUND, NODE_NOT_FOUND, or NODE_NOT_DELETABLE on structured errors.
