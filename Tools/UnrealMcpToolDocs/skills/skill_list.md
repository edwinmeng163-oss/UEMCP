# unreal.skill_list

**Category**: skills
**Title**: List Skills
**Risk level**: read_only

Lists promoted project skills and local drafts available to the Unreal MCP skill workflow.

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
    "roots": {
      "type": "array",
      "description": "Project-relative skill roots to scan. Defaults to Tools/UnrealMcpSkills.",
      "items": {
        "type": "string"
      }
    },
    "nameFilter": {
      "type": "string",
      "description": "Optional case-insensitive skill name filter."
    },
    "includeText": {
      "type": "boolean",
      "description": "Include full skill text in results.",
      "default": false
    },
    "maxPreviewChars": {
      "type": "number",
      "description": "Maximum preview characters per skill.",
      "default": 1200
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
  "nameFilter": "mcp-self-extension",
  "includeText": false
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: read-only inspection, audit, status, schema validation, memory read, or skill read tool.
