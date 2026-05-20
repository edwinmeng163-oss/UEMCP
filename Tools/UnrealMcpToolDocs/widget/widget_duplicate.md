# unreal.widget_duplicate

**Category**: widget
**Title**: Duplicate Widget
**Risk level**: medium

Duplicates a widget node inside the same Widget Blueprint, optionally copying its child subtree and assigning unique names. Use this when duplicating a UMG button, text block, card, panel, or layout group.

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
    "sourceName": {
      "type": "string",
      "description": "Widget name to duplicate."
    },
    "newName": {
      "type": "string",
      "description": "Optional requested duplicate name; a suffix is added if needed."
    },
    "includeSubtree": {
      "type": "boolean",
      "description": "Whether to duplicate child widgets recursively.",
      "default": true
    }
  },
  "required": [
    "widgetBlueprintPath",
    "sourceName"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "widgetBlueprintPath": "/Game/__UEvolveMcpTest/Widget/WBP_BindingSmoke",
  "sourceName": "BoundButton",
  "newName": "BoundButton",
  "includeSubtree": true
}
```

## Provenance
- Source docs: Plugins/UnrealMcp/README.md#editor-action-tools
- Reason: UMG parity tool: duplicate a button, text block, panel, or complete widget subtree with collision-safe generated names.
- Notes: The deep binding-integrity fixture is deferred to C++ helper coverage plus widget_dump_tree readback because JSON fixtures cannot construct arbitrary UMG bindings by themselves.
