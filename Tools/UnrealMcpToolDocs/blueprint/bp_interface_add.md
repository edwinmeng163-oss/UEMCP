# unreal.bp_interface_add

**Category**: blueprint
**Title**: Add Blueprint Interface
**Risk level**: medium

Adds a Blueprint-implementable interface to a Blueprint and conforms exposed interface function graphs.

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
      "description": "Full UInterface-derived class path to implement."
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
  "interfacePath": "/Game/__UEvolveMcpTest_chunk2b/BPI_RefactorSmoke.BPI_RefactorSmoke_C"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 2b C++ Blueprint refactor tool: interface implementation with Blueprint usability checks.
- Notes: PIE-blocked. Missing class paths return INTERFACE_NOT_FOUND; non-interface, Blueprint-unusable, prohibited, or already implemented interfaces return INTERFACE_NOT_USABLE.
