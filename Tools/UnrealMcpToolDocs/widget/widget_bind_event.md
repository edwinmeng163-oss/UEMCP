# unreal.widget_bind_event

**Category**: widget
**Title**: Bind Widget Event
**Risk level**: medium

Binds a widget event to a generated Blueprint graph handler and reports the created binding evidence.

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
      "description": "Widget variable name to bind the event on, for example RefreshButton."
    },
    "eventName": {
      "type": "string",
      "description": "Multicast delegate property name, for example OnClicked on Button.",
      "default": "OnClicked"
    },
    "functionName": {
      "type": "string",
      "description": "Optional generated event function name override. Usually leave blank."
    },
    "x": {
      "type": "number",
      "description": "Optional event node X position after creation.",
      "default": 0
    },
    "y": {
      "type": "number",
      "description": "Optional event node Y position after creation.",
      "default": 0
    },
    "compile": {
      "type": "boolean",
      "description": "Whether to compile first so exposed widget variables exist in the skeleton class.",
      "default": true
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
