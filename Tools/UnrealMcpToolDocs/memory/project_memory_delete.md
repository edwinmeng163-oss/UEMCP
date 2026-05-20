# unreal.project_memory_delete

**Category**: memory
**Title**: Delete Project Memory Entry
**Risk level**: medium

Deletes a key from persistent Saved/UnrealMcp project memory used for cross-session AI handoff.

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
      "description": "Memory entry key to delete."
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview deletion without writing ProjectMemory.json.",
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
- Reason: Explicit registry: memory tool with controlled write or workflow side effects.
