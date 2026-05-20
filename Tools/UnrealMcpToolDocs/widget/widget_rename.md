# unreal.widget_rename

**Category**: widget
**Title**: Rename Widget
**Risk level**: medium

Renames a widget node inside a Widget Blueprint, preserving bindings, variable references, animation bindings, and designer hierarchy references. Use this to rename a button, text block, panel, or other UMG widget by name.

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
    "oldName": {
      "type": "string",
      "description": "Existing widget name inside the Widget Blueprint designer tree."
    },
    "newName": {
      "type": "string",
      "description": "New unique widget name to apply."
    },
    "force": {
      "type": "boolean",
      "description": "Reserved safety override for future rename conflict handling.",
      "default": false
    }
  },
  "required": [
    "widgetBlueprintPath",
    "oldName",
    "newName"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "widgetBlueprintPath": "/Game/__UEvolveMcpTest/Widget/WBP_BindingSmoke",
  "oldName": "BoundButton",
  "newName": "RenamedButton",
  "force": false
}
```

## Provenance
- Source docs: Plugins/UnrealMcp/README.md#editor-action-tools
- Reason: UMG parity tool: rename a button widget, text block, panel, image, or designer widget while preserving bindings and Blueprint variable references.
- Notes: Binding preservation is covered by C++ automation helper tests and Widget fixture notes; graph/event reference coverage uses existing widget_dump_tree readback.
