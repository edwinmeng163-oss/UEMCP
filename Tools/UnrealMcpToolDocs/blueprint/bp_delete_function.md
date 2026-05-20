# unreal.bp_delete_function

**Category**: blueprint
**Title**: Delete Blueprint Function
**Risk level**: low

Deletes a user function graph and reports caller nodes found in the same Blueprint.

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
    "functionName": {
      "type": "string",
      "description": "User function graph name to delete."
    }
  },
  "required": [
    "blueprintPath",
    "functionName"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "blueprintPath": "/Game/__UEvolveMcpTest_chunk2a/BP_RefactorSmoke",
  "functionName": "RefactorFunction"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 2a C++ Blueprint refactor tool: bounded user function graph deletion without Python.
- Notes: PIE-blocked. Built-in graphs such as EventGraph and UserConstructionScript return FUNCTION_NOT_DELETABLE.
