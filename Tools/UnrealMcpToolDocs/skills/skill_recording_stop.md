# unreal.skill_recording_stop

**Category**: skills
**Title**: Stop Skill Recording
**Risk level**: medium

Stops the active skill recording session and leaves recorded events available for distillation.

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
    "reason": {
      "type": "string",
      "description": "Optional stop reason or session summary."
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
  "reason": "Core skill activity test teardown."
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: skills tool with controlled write or workflow side effects.
