# unreal.mcp_generate_tests

**Category**: self-extension
**Title**: Generate MCP Tests
**Risk level**: medium

Generates a test request or suite scaffold for an MCP tool under the project test workspace.

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
    "toolName": {
      "type": "string",
      "description": "Tool name whose schema/scaffold should drive generated MCP tests."
    },
    "scaffoldDir": {
      "type": "string",
      "description": "Project-relative or absolute scaffold directory. Tests are written under scaffoldDir/Tests by default."
    },
    "testsDir": {
      "type": "string",
      "description": "Project-relative or absolute test output directory. Defaults to scaffoldDir/Tests."
    },
    "outputRoot": {
      "type": "string",
      "description": "Project-relative scaffold root used with toolName.",
      "default": "Tools/UnrealMcpToolScaffolds"
    },
    "schemaJson": {
      "type": "string",
      "description": "Optional raw input schema JSON. If omitted, the loaded tool schema, scaffold README schema, or TestRequest.json is used."
    },
    "overwrite": {
      "type": "boolean",
      "description": "Whether to overwrite generated test files when content changes.",
      "default": true
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview generated test files without writing them.",
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
- Reason: Explicit registry: self-extension tool with controlled write or workflow side effects.
