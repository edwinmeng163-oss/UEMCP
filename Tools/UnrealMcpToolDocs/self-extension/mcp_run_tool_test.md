# unreal.mcp_run_tool_test

**Category**: self-extension
**Title**: Run MCP Tool Test
**Risk level**: medium

Runs one MCP tool test request file against the live editor endpoint and records structured pass/fail evidence.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: false
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
      "description": "Optional tool name. If empty, read from TestRequest.json or project memory."
    },
    "testRequestPath": {
      "type": "string",
      "description": "Project-relative or absolute TestRequest.json path."
    },
    "testsDir": {
      "type": "string",
      "description": "Project-relative or absolute Tests directory. Used when runSuite=true."
    },
    "scaffoldDir": {
      "type": "string",
      "description": "Project-relative or absolute scaffold directory containing TestRequest.json."
    },
    "outputRoot": {
      "type": "string",
      "description": "Project-relative scaffold root used with toolName if no testRequestPath/scaffoldDir is provided.",
      "default": "Tools/UnrealMcpToolScaffolds"
    },
    "memoryKey": {
      "type": "string",
      "description": "Project memory key used to resume after editor restart.",
      "default": "mcp.extension.build_test"
    },
    "readProjectMemory": {
      "type": "boolean",
      "description": "Whether to read test path/tool name from project memory when arguments are omitted.",
      "default": true
    },
    "writeProjectMemory": {
      "type": "boolean",
      "description": "Whether to write test result back to project memory.",
      "default": true
    },
    "expectToolListed": {
      "type": "boolean",
      "description": "Whether missing tools/list entry should fail the test.",
      "default": true
    },
    "executeTool": {
      "type": "boolean",
      "description": "Whether to execute the tools/call request from TestRequest.json after tools/list check.",
      "default": true
    },
    "runSuite": {
      "type": "boolean",
      "description": "Delegate to unreal.mcp_run_test_suite instead of running one test request.",
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
