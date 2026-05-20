# unreal.bp_rename_variable

**Category**: blueprint
**Title**: Rename Blueprint Variable
**Risk level**: medium

Renames a Blueprint member variable and rewrites local graph references.

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
      "description": "Existing Blueprint member variable name."
    },
    "newName": {
      "type": "string",
      "description": "New valid Blueprint identifier for the member variable."
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
  "oldName": "RefactorCounter",
  "newName": "RefactorCounterRenamed"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 2a C++ Blueprint refactor tool: variable rename plus local graph reference rewrite without Python.
- Notes: PIE-blocked. Invalid identifiers return INVALID_NAME with reason not-an-identifier or reserved-word; existing variables return RENAME_COLLISION.
