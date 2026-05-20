# unreal.mcp_build_editor

**Category**: self-extension
**Title**: Build Editor Target
**Risk level**: high

Runs Unreal Build Tool for the editor target, captures logs, parses errors, and writes build status memory.

## Capabilities

- Requires write: false
- Requires build: true
- Requires external process: true
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
    "target": {
      "type": "string",
      "description": "UBT target to build.",
      "default": "UEvolveEditor"
    },
    "platform": {
      "type": "string",
      "description": "UBT platform. Empty/default uses the host editor platform.",
      "default": "Mac"
    },
    "configuration": {
      "type": "string",
      "description": "UBT configuration.",
      "default": "Development"
    },
    "extraArgs": {
      "type": "string",
      "description": "Optional additional UBT arguments appended to the build command."
    },
    "toolName": {
      "type": "string",
      "description": "Optional newly integrated tool name to persist into project memory for post-restart testing."
    },
    "testRequestPath": {
      "type": "string",
      "description": "Optional project-local TestRequest.json path to persist for post-restart testing."
    },
    "testsDir": {
      "type": "string",
      "description": "Optional project-local Tests directory to persist for post-restart suite testing."
    },
    "scaffoldDir": {
      "type": "string",
      "description": "Optional scaffold directory. If testRequestPath is empty, scaffoldDir/TestRequest.json is remembered."
    },
    "memoryKey": {
      "type": "string",
      "description": "Project memory key used for restart handoff.",
      "default": "mcp.extension.build_test"
    },
    "writeProjectMemory": {
      "type": "boolean",
      "description": "Whether to write restart handoff state before and after the build.",
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
- Reason: Explicit registry: high-impact write/build/rollback/destructive editor operation.
