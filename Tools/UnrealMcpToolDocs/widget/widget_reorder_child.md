# unreal.widget_reorder_child

**Category**: widget
**Title**: Reorder Widget
**Risk level**: low

Moves a widget to a new sibling index inside its parent panel in a Widget Blueprint. Use this when a user asks to move a button, text block, image, or panel earlier or later in a UMG layout.

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
    "widgetBlueprintPath": {
      "type": "string",
      "description": "Widget Blueprint asset path to edit."
    },
    "widgetName": {
      "type": "string",
      "description": "Widget name to move within its parent panel."
    },
    "newIndex": {
      "type": "number",
      "description": "New zero-based child index inside the parent panel.",
      "default": 0
    }
  },
  "required": [
    "widgetBlueprintPath",
    "widgetName",
    "newIndex"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "widgetBlueprintPath": "/Game/__UEvolveMcpTest/Widget/WBP_McpSmoke",
  "widgetName": "DebugText",
  "newIndex": 0
}
```

## Provenance
- Source docs: Plugins/UnrealMcp/README.md#editor-action-tools
- Reason: UMG parity tool: reorder children inside a Canvas, VerticalBox, HorizontalBox, Overlay, or other panel by widget name.
- Notes: Low-risk hierarchy edit; root widgets without a parent panel are refused.
