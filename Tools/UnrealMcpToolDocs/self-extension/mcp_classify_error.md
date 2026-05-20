# unreal.mcp_classify_error

**Category**: self-extension
**Title**: Classify MCP Error
**Risk level**: read_only

Classifies UBT, MCP, JSON schema, UE Python, HTTP endpoint, OpenAI API, and editor-state errors with next-step suggestions.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "text": {
      "type": "string",
      "description": "Raw error text to classify."
    },
    "logPath": {
      "type": "string",
      "description": "Optional project-local log file to tail and classify."
    },
    "tailLines": {
      "type": "number",
      "description": "Lines to read from logPath.",
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
  "text": "tools/list skipped a schema because additionalProperties=true, then HTTP request timed out at 127.0.0.1:8765."
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: read-only classifier for UBT, MCP, schema, UE Python, HTTP, OpenAI API, and editor-state failures.
