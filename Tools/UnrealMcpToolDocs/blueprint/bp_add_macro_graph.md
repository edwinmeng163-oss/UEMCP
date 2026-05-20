# unreal.bp_add_macro_graph

**Category**: blueprint
**Title**: Add Blueprint Macro Graph
**Risk level**: low

Creates a user macro graph on a Blueprint after fixed-name validation and collision checks.

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
      "description": "New valid Blueprint macro graph name."
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
  "macroName": "RefactorMacro"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 2b C++ Blueprint refactor tool: macro graph creation with identifier validation and collision checks.
- Notes: PIE-blocked. Invalid identifiers return INVALID_NAME with reason not-an-identifier or reserved-word; existing graph/function/macro/variable names return RENAME_COLLISION.
