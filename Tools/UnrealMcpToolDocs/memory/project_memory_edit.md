# unreal.project_memory_edit

**Category**: memory
**Title**: Edit Project Memory Entry
**Risk level**: medium

Updates an existing project memory entry with revised JSON or text content for cross-session handoff.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {
    "key": {
      "type": "string",
      "description": "Memory entry key to edit."
    },
    "summary": {
      "type": "string",
      "description": "Optional new summary. Omit to preserve."
    },
    "status": {
      "type": "string",
      "description": "Optional new status. Omit to preserve."
    },
    "nextStep": {
      "type": "string",
      "description": "Optional new next step. Omit to preserve."
    },
    "contentJson": {
      "type": "string",
      "description": "Optional JSON object to merge or replace into content."
    },
    "contentMode": {
      "type": "string",
      "description": "Content update mode: merge or replace.",
      "default": "merge"
    },
    "tags": {
      "type": "array",
      "description": "Optional tags to replace, append, or remove.",
      "items": {
        "type": "string"
      }
    },
    "tagsMode": {
      "type": "string",
      "description": "Tags update mode: replace, append, or remove.",
      "default": "replace"
    },
    "createIfMissing": {
      "type": "boolean",
      "description": "Create the memory entry if it does not exist.",
      "default": false
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview the edit without writing ProjectMemory.json.",
      "default": false
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
- Reason: Explicit registry: memory tool with controlled write or workflow side effects.
