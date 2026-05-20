# unreal.widget_set_slot_layout

**Category**: widget
**Title**: Set Widget Slot Layout
**Risk level**: medium

Updates slot layout fields such as anchors, offsets, alignment, or size for a widget with a parent slot.

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
  "properties": {
    "widgetBlueprintPath": {
      "type": "string",
      "description": "Widget Blueprint asset path to edit."
    },
    "widgetName": {
      "type": "string",
      "description": "Target widget name whose parent slot layout should be edited."
    },
    "x": {
      "type": "number",
      "description": "Canvas slot X position.",
      "default": 0
    },
    "y": {
      "type": "number",
      "description": "Canvas slot Y position.",
      "default": 0
    },
    "width": {
      "type": "number",
      "description": "Canvas slot width.",
      "default": 100
    },
    "height": {
      "type": "number",
      "description": "Canvas slot height.",
      "default": 40
    },
    "autoSize": {
      "type": "boolean",
      "description": "Canvas slot auto-size flag.",
      "default": false
    },
    "zOrder": {
      "type": "number",
      "description": "Canvas slot z-order.",
      "default": 0
    },
    "alignmentX": {
      "type": "number",
      "description": "Canvas slot alignment X 0..1.",
      "default": 0
    },
    "alignmentY": {
      "type": "number",
      "description": "Canvas slot alignment Y 0..1.",
      "default": 0
    },
    "anchorMinX": {
      "type": "number",
      "description": "Canvas slot minimum anchor X.",
      "default": 0
    },
    "anchorMinY": {
      "type": "number",
      "description": "Canvas slot minimum anchor Y.",
      "default": 0
    },
    "anchorMaxX": {
      "type": "number",
      "description": "Canvas slot maximum anchor X.",
      "default": 0
    },
    "anchorMaxY": {
      "type": "number",
      "description": "Canvas slot maximum anchor Y.",
      "default": 0
    },
    "paddingLeft": {
      "type": "number",
      "description": "Panel slot left padding.",
      "default": 0
    },
    "paddingTop": {
      "type": "number",
      "description": "Panel slot top padding.",
      "default": 0
    },
    "paddingRight": {
      "type": "number",
      "description": "Panel slot right padding.",
      "default": 0
    },
    "paddingBottom": {
      "type": "number",
      "description": "Panel slot bottom padding.",
      "default": 0
    },
    "hAlign": {
      "type": "string",
      "description": "Horizontal alignment for box/overlay slots: left, center, right, fill."
    },
    "vAlign": {
      "type": "string",
      "description": "Vertical alignment for box/overlay slots: top, center, bottom, fill."
    },
    "sizeRule": {
      "type": "string",
      "description": "Box slot size rule: fill or automatic.",
      "default": "fill"
    },
    "sizeValue": {
      "type": "number",
      "description": "Box slot fill size value.",
      "default": 1
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
- Reason: Explicit registry: widget tool with controlled write or workflow side effects.
