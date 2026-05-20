# unreal.mcp_build_server

**Category**: self-extension
**Title**: Build Server Target
**Risk level**: medium

Runs Unreal Build Tool for the dedicated-server target, captures logs, parses errors, and writes build status memory.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: true
- Requires restart: false
- Requires lock: true
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: manual

## Input schema

```json
{
  "type": "object",
  "properties": {
    "target": {
      "type": "string",
      "description": "UBT target to build.",
      "default": "UEvolveServer"
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
      "description": "Optional tool name to persist into project memory with the build result."
    },
    "testRequestPath": {
      "type": "string",
      "description": "Optional project-local TestRequest.json path to persist in project memory."
    },
    "testsDir": {
      "type": "string",
      "description": "Optional project-local Tests directory to persist in project memory."
    },
    "scaffoldDir": {
      "type": "string",
      "description": "Optional scaffold directory. If testRequestPath is empty, scaffoldDir/TestRequest.json is remembered."
    },
    "memoryKey": {
      "type": "string",
      "description": "Project memory key used for build status handoff.",
      "default": "mcp.extension.build_server"
    },
    "writeProjectMemory": {
      "type": "boolean",
      "description": "Whether to write build status before and after the build.",
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
{}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: UBT target matrix build tool for dedicated-server compile coverage.
