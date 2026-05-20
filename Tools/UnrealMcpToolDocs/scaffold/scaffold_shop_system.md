# unreal.scaffold_shop_system

**Category**: scaffold
**Title**: Scaffold Shop System
**Risk level**: medium
**Exposure**: legacy_hidden

Legacy gameplay scaffold that creates shop-system content; retained for compatibility and hidden from default AI tool lists.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
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
- Reason: Legacy gameplay/demo scaffold retained for direct compatibility; hidden from AI-facing tools/list so UEAtelier can focus on the MCP self-extension loop.
- Notes: Legacy demo/gameplay content scaffold. Prefer unreal.scaffold_mcp_tool and mcp_* self-extension tools for product work.
