# unreal.tools.list_exportable

**Category**: self-extension
**Title**: List Exportable Tools
**Risk level**: read_only

Lists registered MCP tools that qualify as scaffold-backed and can be cleanly exported via unreal.tools.export_package.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: category

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

_Provenance: fixture-derived_

```json
{}
```

## Provenance
- Source docs: Docs/SelfExtensionPipeline.md#tool-sharing
- Reason: Lists scaffold-backed MCP tools that can be exported as portable packages.
- Notes: Tool sharing package picker helper.
