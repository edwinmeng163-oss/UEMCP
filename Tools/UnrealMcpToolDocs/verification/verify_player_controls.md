# unreal.verify_player_controls

**Category**: verification
**Title**: Verify Player Controls
**Risk level**: medium

Verifies that player controls are set up in PIE by checking possession, the pawn class, camera/spring-arm components, and existing jump, movement, and look bindings. This tool does not inject input and does not measure movement deltas.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "expectedPawnClass": {
      "type": "string",
      "description": "Optional expected pawn class path, for example /Game/BP_Player.BP_Player_C."
    },
    "startIfNeeded": {
      "type": "boolean",
      "description": "Start PIE if runtime verification is needed and PIE is not already active.",
      "default": false
    },
    "stopAfter": {
      "type": "boolean",
      "description": "Stop PIE after verification. When omitted, defaults to true only when this call started PIE.",
      "default": false
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
  "startIfNeeded": true,
  "stopAfter": true
}
```

## Provenance

- Source docs: Docs/Verification.md
- Reason: v0.27.1 Wave 2 core gameplay setup primitive for possession and binding existence checks.
- Notes: Uses a private BeginPIE wait helper when `startIfNeeded=true`; intentionally performs no input injection or movement-delta validation.
