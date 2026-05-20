# unreal.widget_add

**Category**: widget
**Title**: Add Widget
**Risk level**: medium

Adds a widget to a Widget Blueprint tree under the requested parent and marks the asset dirty.

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
    "parentWidgetName": {
      "type": "string",
      "description": "Parent panel widget name. Empty uses the root widget; if there is no root, the new widget becomes root."
    },
    "widgetName": {
      "type": "string",
      "description": "Name for the new widget. If omitted, a unique name is generated from the widget class."
    },
    "widgetClass": {
      "type": "string",
      "description": "Widget class simple name such as CanvasPanel, VerticalBox, Button, TextBlock, or a class path.",
      "default": "TextBlock"
    },
    "index": {
      "type": "number",
      "description": "Optional child index for panel insertion. Use -1 to append.",
      "default": -1
    },
    "isVariable": {
      "type": "boolean",
      "description": "Whether the widget should be exposed as a Blueprint variable.",
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
  "widgetBlueprintPath": "/Game/__UEvolveMcpTest/Widget/WBP_McpSmoke",
  "parentWidgetName": "RootCanvas",
  "widgetName": "SmokeLabel",
  "widgetClass": "TextBlock",
  "isVariable": true
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: Widget write tool with automated disposable sandbox category coverage.
