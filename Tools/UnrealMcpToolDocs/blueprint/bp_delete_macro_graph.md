# unreal.bp_delete_macro_graph

**Category**: blueprint
**Title**: Delete Blueprint Macro Graph
**Risk level**: medium

Deletes a Blueprint macro graph after checking local macro instance references unless force=true.

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
    "macroName": {
      "type": "string",
      "description": "Existing Blueprint macro graph name to delete."
    },
    "force": {
      "type": "boolean",
      "description": "Delete even when UK2Node_MacroInstance references are present.",
      "default": false
    }
  },
  "required": [
    "blueprintPath",
    "macroName"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "blueprintPath": "/Game/__UEvolveMcpTest_chunk2b/BP_RefactorSmoke",
  "macroName": "RefactorMacro",
  "force": false
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 2b C++ Blueprint refactor tool: guarded macro graph deletion with reference-count reporting.
- Notes: PIE-blocked. Missing macros return MACRO_NOT_FOUND; force=false returns REFERENCES_PRESENT with referenceCount when macro instance nodes still reference the graph.
