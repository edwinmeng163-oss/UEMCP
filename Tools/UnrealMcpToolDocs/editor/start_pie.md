# unreal.start_pie

**Category**: editor
**Title**: Start PIE
**Risk level**: low

Starts Play-In-Editor for the current map and returns the resulting PIE state.

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
    "simulate": {
      "type": "boolean",
      "description": "Whether to start Simulate In Editor instead of Play In Editor.",
      "default": false
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
- Reason: Explicit registry: low-risk editor session operation.
