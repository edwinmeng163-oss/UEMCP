# unreal.mcp_validate_cpp_snippet

**Category**: self-extension
**Title**: Validate C++ Snippet
**Risk level**: read_only
**Exposure**: legacy_hidden

Legacy alias for validating scaffold C++ patch fragments; prefer unreal.mcp_validate_cpp_patch for new workflows.

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
  "properties": {},
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
- Reason: Explicit registry: read-only inspection, audit, status, schema validation, memory read, or skill read tool.
- Notes: Legacy alias for unreal.mcp_validate_cpp_patch; hidden from default AI tool exposure.
