# unreal.skill_distill_from_activity

**Category**: skills
**Title**: Distill Skill From Activity
**Risk level**: medium

Distills recent ActivityLog events into a local skill draft under Saved/UnrealMcp for review.

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
    "sessionId": {
      "type": "string",
      "description": "Activity session id to distill. Defaults to current session."
    },
    "skillName": {
      "type": "string",
      "description": "Output skill folder name. Defaults to sanitized title/goal."
    },
    "title": {
      "type": "string",
      "description": "Draft SKILL.md title."
    },
    "goal": {
      "type": "string",
      "description": "Override learned goal text."
    },
    "writeDraft": {
      "type": "boolean",
      "description": "Write draft SKILL.md under Saved/UnrealMcp/SkillDrafts.",
      "default": true
    },
    "includeEvents": {
      "type": "boolean",
      "description": "Append event summaries to the draft for review.",
      "default": false
    },
    "overwrite": {
      "type": "boolean",
      "description": "Overwrite an existing draft when writeDraft=true.",
      "default": true
    },
    "maxEvents": {
      "type": "number",
      "description": "Maximum JSONL events to distill.",
      "default": 200
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
  "skillName": "core-skill-test-workflow",
  "title": "Core Skill Test Workflow",
  "writeDraft": false,
  "includeEvents": false,
  "maxEvents": 50
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: skills tool with controlled write or workflow side effects.
