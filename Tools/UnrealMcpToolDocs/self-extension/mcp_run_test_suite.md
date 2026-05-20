# unreal.mcp_run_test_suite

**Category**: self-extension
**Title**: Run MCP Test Suite
**Risk level**: medium

Runs a directory of MCP JSON test requests and reports pass/fail results with optional project memory handoff.

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
      "description": "Optional tool name. If empty, read from project memory or scaffold TestRequest.json."
    },
    "testsDir": {
      "type": "string",
      "description": "Project-relative or absolute Tests directory containing *.json test cases."
    },
    "scaffoldDir": {
      "type": "string",
      "description": "Project-relative or absolute scaffold directory. Defaults testsDir to scaffoldDir/Tests."
    },
    "outputRoot": {
      "type": "string",
      "description": "Project-relative scaffold root used with toolName.",
      "default": "Tools/UnrealMcpToolScaffolds"
    },
    "memoryKey": {
      "type": "string",
      "description": "Project memory key used to resume after editor restart.",
      "default": "mcp.extension.build_test"
    },
    "readProjectMemory": {
      "type": "boolean",
      "description": "Whether to read tool/scaffold/tests paths from project memory.",
      "default": true
    },
    "writeProjectMemory": {
      "type": "boolean",
      "description": "Whether to write suite result back to project memory.",
      "default": true
    },
    "executeTool": {
      "type": "boolean",
      "description": "Whether each test should execute the tools/call request.",
      "default": true
    },
    "stopOnFailure": {
      "type": "boolean",
      "description": "Stop after the first failed test case.",
      "default": false
    },
    "fallbackToSingleTest": {
      "type": "boolean",
      "description": "If no Tests/*.json files exist, fall back to scaffoldDir/TestRequest.json.",
      "default": true
    },
    "includePassedStructuredContent": {
      "type": "boolean",
      "description": "Include structuredContent for passed cases, not only failed cases.",
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
