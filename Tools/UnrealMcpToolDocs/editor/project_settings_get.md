# unreal.project_settings_get

**Category**: editor
**Title**: Get Project Setting
**Risk level**: low

Reads a project setting; pass `effective=true` to get the current runtime/PIE value when available (else returns the configured default).

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "category": {
      "type": "string",
      "description": "Project settings category: engine, editor, game, input, rendering, or physics.",
      "default": "engine"
    },
    "key": {
      "type": "string",
      "description": "Project settings key or dot-path inside the category, for example DefaultGameMode, DefaultInputAxisMappings, or DefaultRHI."
    },
    "effective": {
      "type": "boolean",
      "description": "Return the current runtime/PIE value when available; otherwise return the configured default.",
      "default": false
    }
  },
  "required": [
    "category",
    "key"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "category": "input",
  "key": "DefaultPlayerInputClass"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 1 C++ readback inspector: read-only project settings access closes the get/inspect gap without Python.
- Notes: C++ only. MVP maps common categories to UInputSettings, URendererSettings, and hardcoded config sections, returning KEY_NOT_FOUND for unsupported edge cases.
