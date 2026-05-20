# unreal.skill_apply

**Category**: skills
**Title**: Apply Skill
**Risk level**: medium

Applies a promoted project skill by reading its instructions and returning the workflow guidance for the caller.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {
    "skillName": {
      "type": "string",
      "description": "Project skill name to apply. Used when skillPath is empty."
    },
    "skillPath": {
      "type": "string",
      "description": "Project-relative or absolute path to SKILL.md or *.skill."
    },
    "roots": {
      "type": "array",
      "description": "Project-relative skill roots to search by skillName.",
      "items": {
        "type": "string"
      }
    },
    "task": {
      "type": "string",
      "description": "Current task/context this skill should be applied to."
    },
    "memoryKey": {
      "type": "string",
      "description": "Optional project memory key for recording skill application."
    },
    "writeMemory": {
      "type": "boolean",
      "description": "Record skill application into project memory.",
      "default": true
    },
    "includeFullText": {
      "type": "boolean",
      "description": "Return full skill text instead of a shorter preview.",
      "default": true
    }
  },
  "required": [],
  "additionalProperties": false
}
```

## Usage example

_Provenance: schema-minimal_

```json
{}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: skills tool with controlled write or workflow side effects.
