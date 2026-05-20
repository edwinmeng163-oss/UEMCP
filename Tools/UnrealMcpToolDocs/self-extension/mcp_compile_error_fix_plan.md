# unreal.mcp_compile_error_fix_plan

**Category**: self-extension
**Title**: Plan Compile Error Fix
**Risk level**: medium

Reads compile error text or logs and returns a bounded fix plan for the MCP self-extension workflow.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {
    "buildLogPath": {
      "type": "string",
      "description": "Project-local/absolute build log path. Defaults to newest Saved/UnrealMcp/BuildLogs/*.log."
    },
    "maxErrors": {
      "type": "number",
      "description": "Maximum compiler errors to analyze.",
      "default": 8
    },
    "contextLines": {
      "type": "number",
      "description": "Source context lines before/after each error.",
      "default": 4
    },
    "includeSourceContext": {
      "type": "boolean",
      "description": "Include nearby source lines for each parsed error.",
      "default": true
    },
    "autoPatch": {
      "type": "boolean",
      "description": "Attempt only deterministic safe patches when available.",
      "default": false
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview any autoPatch changes without writing files.",
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
- Reason: Explicit registry: self-extension tool with controlled write or workflow side effects.
