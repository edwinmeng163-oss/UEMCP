# unreal.bp_add_variable

**Category**: blueprint
**Title**: Add Blueprint Variable
**Risk level**: medium

Adds a typed member variable to a Blueprint with an optional default value and marks the asset structurally modified.

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
    "blueprintPath": {
      "type": "string",
      "description": "Blueprint asset path to edit."
    },
    "name": {
      "type": "string",
      "description": "New member variable name."
    },
    "pinCategory": {
      "type": "string",
      "description": "Blueprint pin category: bool, int, int64, float, double, string, name, text, object, class, struct, enum, byte, or wildcard.",
      "default": "bool"
    },
    "pinSubCategory": {
      "type": "string",
      "description": "Optional pin subcategory. For real pins use float or double; otherwise usually blank."
    },
    "subCategoryObjectPath": {
      "type": "string",
      "description": "Optional class, struct, or enum path used by object/class/struct/enum pins."
    },
    "containerType": {
      "type": "string",
      "description": "Optional container type: none, array, set, or map.",
      "default": "none"
    },
    "defaultValue": {
      "type": "string",
      "description": "Optional default value stored as Blueprint default text."
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
  "pinCategory": "bool"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: blueprint tool with controlled write or workflow side effects.
