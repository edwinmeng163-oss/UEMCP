# unreal.configure_player_input

**Category**: editor
**Title**: Configure Player Input
**Risk level**: medium

Configures standard player-control input mappings for legacy input or an Enhanced Input mapping context, defaulting to dry-run diagnostics.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "inputSystem": {
      "type": "string",
      "description": "Input stack to configure.",
      "default": "auto",
      "enum": ["auto", "legacy", "enhanced"]
    },
    "profile": {
      "type": "string",
      "description": "Mapping profile to use before applying mappings overrides.",
      "default": "third_person_basic",
      "enum": ["third_person_basic", "custom"]
    },
    "mappings": {
      "type": "object",
      "description": "Optional overrides for MoveForward, MoveRight, LookYaw, LookPitch, and Jump.",
      "additionalProperties": false
    },
    "enhancedInputMappingContextPath": {
      "type": "string",
      "description": "Optional Enhanced Input UInputMappingContext asset path."
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview intended input config writes without mutating settings or mapping assets.",
      "default": true
    }
  },
  "required": [],
  "additionalProperties": false
}
```

## Usage example

```json
{
  "inputSystem": "legacy",
  "profile": "third_person_basic",
  "dryRun": true
}
```

## Provenance
- Source docs: Tools/UnrealMcpToolDocs/editor/configure_player_input.md
- Reason: v0.27.1 core gameplay setup primitive for reusable Legacy/Enhanced Input configuration under dry-run and postcheck evidence.
- Notes: `inputSystem=auto` configures Enhanced Input only when `enhancedInputMappingContextPath` is supplied and Enhanced Input classes are available; otherwise it uses legacy `UInputSettings`.
