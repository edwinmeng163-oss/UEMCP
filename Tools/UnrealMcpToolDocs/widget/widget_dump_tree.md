# unreal.widget_dump_tree

**Category**: widget
**Title**: Dump Widget Tree
**Risk level**: read_only

Read-only inspection of a Widget Blueprint tree, widget variables, slots, and optional EventGraph nodes.

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
  "properties": {
    "widgetBlueprintPath": {
      "type": "string",
      "description": "Widget Blueprint asset path to inspect."
    },
    "includeDesignerTree": {
      "type": "boolean",
      "description": "Whether to include recursive designer tree children.",
      "default": true
    },
    "includeGraphNodes": {
      "type": "boolean",
      "description": "Whether to include EventGraph node summaries.",
      "default": false
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
  "widgetBlueprintPath": "/Game/__UEvolveMcpTest/Widget/WBP_McpSmoke",
  "includeDesignerTree": true,
  "includeGraphNodes": false
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: Widget tree inspection tool for AI self-verification before and after UMG edits.
