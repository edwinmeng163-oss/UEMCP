# unreal.skill_read

**Category**: skills
**Title**: Read Skill
**Risk level**: read_only

Reads a promoted project skill or draft by name and returns its instruction text.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {
    "skillName": {
      "type": "string",
      "description": "Project skill name to read. Used when skillPath is empty."
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
    "includeText": {
      "type": "boolean",
      "description": "Include full skill text.",
      "default": true
    },
    "maxPreviewChars": {
      "type": "number",
      "description": "Maximum preview characters.",
      "default": 4000
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
- Reason: Explicit registry: read-only inspection, audit, status, schema validation, memory read, or skill read tool.
