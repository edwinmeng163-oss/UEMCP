# unreal.bp_rename_function

**Category**: blueprint
**Title**: Rename Blueprint Function
**Risk level**: medium

Renames a user function graph and rewrites local caller nodes.

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
    "oldName": {
      "type": "string",
      "description": "Existing user function graph name."
    },
    "newName": {
      "type": "string",
      "description": "New valid Blueprint identifier for the function graph."
    }
  },
  "required": [
    "blueprintPath",
    "oldName",
    "newName"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "blueprintPath": "/Game/__UEvolveMcpTest_chunk2a/BP_RefactorSmoke",
  "oldName": "RefactorFunction",
  "newName": "RefactorFunctionRenamed"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 2a C++ Blueprint refactor tool: function graph rename plus local caller rewrite without Python.
- Notes: PIE-blocked. Built-in graph renames return FUNCTION_NOT_RENAMABLE; name collisions with graphs or functions return RENAME_COLLISION.
