# unreal.compile_blueprint

**Category**: blueprint
**Title**: Compile Blueprint
**Risk level**: medium

Compiles one Blueprint asset by path and returns its resulting Blueprint status without saving the package.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {
    "path": {
      "type": "string",
      "description": "Blueprint asset path to compile."
    }
  },
  "required": [],
  "additionalProperties": false
}
```

## Usage example

_Provenance: schema-minimal_

```json
{}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: blueprint tool with controlled write or workflow side effects.
