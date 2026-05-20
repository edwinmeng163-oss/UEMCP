# unreal.preview_change_plan

**Category**: self-extension
**Title**: Preview Change Plan
**Risk level**: read_only

Converts a natural-language task into a structured plan with likely tools, risks, backups, and verification steps.

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
    "task": {
      "type": "string",
      "description": "Natural-language task to turn into a structured change plan."
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
  "task": "Add a Blueprint variable and update a Widget, then verify the result."
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: read-only planning tool that improves AI precision before mutation.
