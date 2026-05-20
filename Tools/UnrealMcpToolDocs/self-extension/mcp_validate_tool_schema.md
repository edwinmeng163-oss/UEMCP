# unreal.mcp_validate_tool_schema

**Category**: self-extension
**Title**: Validate Tool Schema
**Risk level**: read_only

Validates an MCP tool schema for fixed OpenAI-compatible JSON object shape and required metadata.

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
    "toolName": {
      "type": "string",
      "description": "Existing MCP tool name to validate. Used when schemaJson is empty."
    },
    "schemaJson": {
      "type": "string",
      "description": "Raw JSON object schema to validate. If set, this takes precedence over toolName."
    },
    "returnNormalizedSchema": {
      "type": "boolean",
      "description": "Whether to include the normalized schema in structured output.",
      "default": true
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
  "schemaJson": "{\"type\":\"object\",\"additionalProperties\":false,\"required\":[\"mode\",\"scope\",\"overallSeverity\",\"blockingIssueCount\",\"warningCount\",\"lastRunUtc\",\"validatorVersion\",\"checks\"],\"properties\":{\"mode\":{\"type\":\"string\",\"enum\":[\"source\",\"full-win\"]},\"scope\":{\"type\":\"string\"},\"overallSeverity\":{\"type\":\"string\",\"enum\":[\"pass\",\"warning\",\"error\"]},\"blockingIssueCount\":{\"type\":\"integer\",\"minimum\":0},\"warningCount\":{\"type\":\"integer\",\"minimum\":0},\"lastRunUtc\":{\"type\":\"string\"},\"validatorVersion\":{\"type\":\"string\",\"enum\":[\"0.15.0-c5-1a\"]},\"checks\":{\"type\":\"array\",\"items\":{\"type\":\"object\",\"additionalProperties\":false,\"required\":[\"id\",\"severity\",\"summary\"],\"properties\":{\"id\":{\"type\":\"string\"},\"severity\":{\"type\":\"string\",\"enum\":[\"pass\",\"warning\",\"error\"]},\"summary\":{\"type\":\"string\"},\"details\":{\"type\":\"object\"},\"recommendedFix\":{\"type\":\"string\"}}}}}}",
  "returnNormalizedSchema": false
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: read-only inspection, audit, status, schema validation, memory read, or skill read tool.
