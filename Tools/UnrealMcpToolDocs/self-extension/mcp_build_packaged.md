# unreal.mcp_build_packaged

**Category**: self-extension
**Title**: Build Packaged Target
**Risk level**: high

Runs RunUAT BuildCookRun for a cooked packaged build under Saved/StagedBuilds or a project-local archive directory.

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
    "platform": {
      "type": "string",
      "description": "Alias for targetPlatform. Empty/default uses the host editor platform.",
      "default": "Mac"
    },
    "targetPlatform": {
      "type": "string",
      "description": "Target platform for BuildCookRun: Win64, Mac, Linux, Android, or IOS.",
      "default": "Mac"
    },
    "configuration": {
      "type": "string",
      "description": "BuildCookRun configuration.",
      "default": "Development"
    },
    "extraArgs": {
      "type": "string",
      "description": "Optional additional RunUAT arguments appended to the BuildCookRun command."
    },
    "outputDirectory": {
      "type": "string",
      "description": "Optional project-local archive directory. Defaults to Saved/StagedBuilds."
    },
    "map": {
      "type": "string",
      "description": "Optional single map to cook. If omitted, BuildCookRun uses configured maps to cook."
    },
    "memoryKey": {
      "type": "string",
      "description": "Project memory key used for packaged build status.",
      "default": "mcp.extension.build_packaged"
    },
    "writeProjectMemory": {
      "type": "boolean",
      "description": "Whether to write packaged build status before and after BuildCookRun.",
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
- Reason: Explicit registry: high-impact RunUAT BuildCookRun packaging tool that writes staged build artifacts.
