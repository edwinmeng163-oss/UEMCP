# unreal.widget_set_property

**Category**: widget
**Title**: Set Widget Property
**Risk level**: medium

Sets a supported property on a named widget inside a Widget Blueprint.

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
      "description": "Target widget name. Use Root or RootWidget for the root widget."
    },
    "propertyName": {
      "type": "string",
      "description": "Property path to set on the widget, for example Text, RenderOpacity, Visibility, or RenderTransform.Translation.X."
    },
    "value": {
      "type": "string",
      "description": "Value text imported into the target property. FText properties accept plain text."
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
  "propertyName": "Text",
  "value": "Hello"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: widget tool with controlled write or workflow side effects.
