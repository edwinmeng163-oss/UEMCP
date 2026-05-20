# unreal.skill_recording_start

**Category**: skills
**Title**: Start Skill Recording
**Risk level**: medium

Starts local activity recording for a skill-distillation session under Saved/UnrealMcp.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "goal": {
      "type": "string",
      "description": "Human-readable goal for this activity recording session."
    },
    "sessionId": {
      "type": "string",
      "description": "Optional explicit session id. Defaults to timestamp-guid."
    },
    "recordIntervalSeconds": {
      "type": "number",
      "description": "Heartbeat interval in seconds. Clamped to 10..3600; default 60.",
      "default": 60
    },
    "reset": {
      "type": "boolean",
      "description": "Start a new session instead of resuming current state.",
      "default": true
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
  "sessionId": "core-skill-test-session",
  "goal": "Core skill activity test workflow.",
  "recordIntervalSeconds": 60,
  "reset": true
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: skills tool with controlled write or workflow side effects.
