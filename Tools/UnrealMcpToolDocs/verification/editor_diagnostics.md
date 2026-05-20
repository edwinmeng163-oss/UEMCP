# unreal.editor_diagnostics

**Category**: verification
**Title**: Read Editor Diagnostics
**Risk level**: read_only

Returns recent warning, error, and fatal Output Log diagnostics from an in-memory listener-backed ring buffer.

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
    "since": {
      "type": "string",
      "description": "Optional ISO-8601 UTC lower bound; entries with ts >= since are returned."
    },
    "classes": {
      "type": "array",
      "description": "Optional diagnostic class filter.",
      "items": {
        "type": "string",
        "enum": [
          "compile",
          "map_check",
          "content",
          "automation",
          "log_warning",
          "log_error"
        ]
      }
    },
    "limit": {
      "type": "number",
      "description": "Maximum diagnostics entries to return. Defaults to 200 and is clamped to 1..1000.",
      "default": 200,
      "minimum": 1,
      "maximum": 1000
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
  "limit": 25
}
```

## Provenance
- Source docs: Docs/Verification.md
- Reason: v0.21 build diagnostics ring buffer for Output Log warning/error/fatal events.
- Notes: In-memory only; the listener stores warning/error/fatal entries in a 5000-entry ring buffer and computes suggested hints at read time.
