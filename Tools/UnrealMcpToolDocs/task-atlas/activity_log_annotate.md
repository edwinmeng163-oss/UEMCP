# unreal.activity_log_annotate

**Category**: task-atlas
**Title**: Annotate Activity Log
**Risk level**: low

Writes a user_intent or ai_summary ActivityLog event for Task Atlas extraction and refreshes local task JSON files.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "kind": {
      "type": "string",
      "description": "Activity annotation kind.",
      "default": "user_intent",
      "enum": [
        "user_intent",
        "ai_summary"
      ]
    },
    "content": {
      "type": "string",
      "description": "Annotation content to write into ActivityLog."
    },
    "sessionId": {
      "type": "string",
      "description": "Optional ActivityLog sessionId. Defaults to the current launch session."
    }
  },
  "required": [
    "kind",
    "content"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "kind": "user_intent",
  "content": "Task Atlas fixture intent."
}
```

## Provenance
- Source docs: Docs/TaskAtlas.md
- Reason: v0.17 Task Atlas annotation tool for user intent and assistant completion summaries.
- Notes: v0.17 only writes frozen ActivityLog annotations and refreshes local Saved/UnrealMcp/Tasks JSON. It does not call an LLM.
