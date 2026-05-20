# unreal.bp_compile_save

**Category**: blueprint
**Title**: Compile And Save Blueprint
**Risk level**: medium

Compiles a Blueprint and optionally saves its package, returning compile status and save evidence.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "blueprintPath": {
      "type": "string",
      "description": "Blueprint asset path to compile and optionally save."
    },
    "savePackage": {
      "type": "boolean",
      "description": "Whether to save the Blueprint package after compiling.",
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
  "savePackage": false
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: blueprint tool with controlled write or workflow side effects.
