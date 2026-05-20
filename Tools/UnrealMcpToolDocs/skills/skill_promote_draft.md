# unreal.skill_promote_draft

**Category**: skills
**Title**: Promote Skill Draft
**Risk level**: high

Promotes a reviewed skill draft into Tools/UnrealMcpSkills so it becomes team-shared project guidance.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "skillName": {
      "type": "string",
      "description": "Skill name to promote under Tools/UnrealMcpSkills."
    },
    "draftPath": {
      "type": "string",
      "description": "Optional project-local draft path. Defaults to Saved/UnrealMcp/SkillDrafts/<skillName>/SKILL.md."
    },
    "overwrite": {
      "type": "boolean",
      "description": "Overwrite existing promoted skill.",
      "default": false
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview promotion without writing Tools/UnrealMcpSkills.",
      "default": true
    },
    "createBackup": {
      "type": "boolean",
      "description": "Back up existing promoted SKILL.md and write a manifest before overwriting.",
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
  "skillName": "core-skill-test-workflow",
  "dryRun": true,
  "overwrite": false,
  "skipLock": true
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: high-impact write/build/rollback/destructive editor operation.
