# unreal.bp_delete_variable

**Category**: blueprint
**Title**: Delete Blueprint Variable
**Risk level**: low

Deletes a Blueprint member variable after checking local reference nodes unless force=true.

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
    "variableName": {
      "type": "string",
      "description": "Blueprint member variable name to delete."
    },
    "force": {
      "type": "boolean",
      "description": "Delete even when reference nodes are present.",
      "default": false
    }
  },
  "required": [
    "blueprintPath",
    "variableName"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "blueprintPath": "/Game/__UEvolveMcpTest_chunk2a/BP_RefactorSmoke",
  "variableName": "RefactorCounter",
  "force": false
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 2a C++ Blueprint refactor tool: bounded variable deletion with reference-count guard.
- Notes: PIE-blocked. force=false returns REFERENCES_PRESENT with referenceCount instead of mutating when references exist.
