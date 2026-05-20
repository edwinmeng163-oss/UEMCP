# unreal.mcp_patch_scaffold_snippet

**Category**: self-extension
**Title**: Patch Scaffold Snippet
**Risk level**: high
**Exposure**: legacy_hidden

Legacy alias for editing scaffold patch fragments; prefer unreal.mcp_patch_scaffold_patch for new workflows.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
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
- Reason: Explicit registry: high-impact write/build/rollback/destructive editor operation.
- Notes: Legacy alias for unreal.mcp_patch_scaffold_patch; hidden from default AI tool exposure.
