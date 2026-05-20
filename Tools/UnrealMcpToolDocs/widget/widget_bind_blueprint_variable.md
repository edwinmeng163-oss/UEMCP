# unreal.widget_bind_blueprint_variable

**Category**: widget
**Title**: Bind Widget Variable
**Risk level**: medium

Binds a Widget Blueprint variable to a named widget so graph and designer code can reference it.

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
      "description": "Widget name to expose or hide as a Blueprint variable."
    },
    "variableName": {
      "type": "string",
      "description": "Optional new variable/widget name. Leave blank to keep the current widget name."
    },
    "expose": {
      "type": "boolean",
      "description": "Whether the widget should be exposed as a Blueprint variable.",
      "default": true
    },
    "compile": {
      "type": "boolean",
      "description": "Whether to compile after changing variable exposure.",
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
