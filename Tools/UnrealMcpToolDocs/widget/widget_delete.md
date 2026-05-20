# unreal.widget_delete

**Category**: widget
**Title**: Delete Widget
**Risk level**: high

Deletes a widget subtree from a Widget Blueprint after checking bindings, bound events, and animation references. Use force only when the user explicitly wants to remove referenced UMG widgets.

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
      "description": "Widget name to delete."
    },
    "force": {
      "type": "boolean",
      "description": "Delete even when widget bindings, bound events, or animation references exist.",
      "default": false
    }
  },
  "required": [
    "widgetBlueprintPath",
    "widgetName"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "widgetBlueprintPath": "/Game/__UEvolveMcpTest/Widget/WBP_BindingSmoke",
  "widgetName": "BoundButton",
  "force": false
}
```

## Provenance
- Source docs: Plugins/UnrealMcp/README.md#editor-action-tools
- Reason: UMG parity tool: safely delete a button, text block, panel, or widget subtree and refuse when bindings would break unless force=true.
- Notes: Mirrors Blueprint delete discipline: force=false refuses referenced widgets instead of silently breaking UMG bindings.
