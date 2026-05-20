# unreal.bp_interface_remove

**Category**: blueprint
**Title**: Remove Blueprint Interface
**Risk level**: medium

Removes an implemented interface from a Blueprint and optionally preserves its function graphs.

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
    "interfacePath": {
      "type": "string",
      "description": "Full implemented interface class path to remove."
    },
    "preserveFunctions": {
      "type": "boolean",
      "description": "Keep interface function graphs as orphan functions instead of deleting them.",
      "default": false
    }
  },
  "required": [
    "blueprintPath",
    "interfacePath"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "blueprintPath": "/Game/__UEvolveMcpTest_chunk2b/BP_RefactorSmoke",
  "interfacePath": "/Game/__UEvolveMcpTest_chunk2b/BPI_RefactorSmoke.BPI_RefactorSmoke_C",
  "preserveFunctions": false
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 2b C++ Blueprint refactor tool: interface removal with optional function preservation.
- Notes: PIE-blocked. Missing Blueprint assets return BLUEPRINT_NOT_FOUND; absent or non-interface implementations return INTERFACE_NOT_FOUND.
