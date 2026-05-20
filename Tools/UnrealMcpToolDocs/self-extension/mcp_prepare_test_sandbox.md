# unreal.mcp_prepare_test_sandbox

**Category**: self-extension
**Title**: Prepare MCP Test Sandbox
**Risk level**: medium

Creates or resets constrained /Game/__UEAtelier* asset and UEvolveMcpTest_* actor sandboxes for disposable happy-path tests.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "contentPath": {
      "type": "string",
      "description": "Sandbox Content Browser path. Must be under /Game/__UEAtelier*.",
      "default": "/Game/__UEvolveMcpTest"
    },
    "reset": {
      "type": "boolean",
      "description": "Whether to delete and recreate the sandbox directory.",
      "default": true
    },
    "resetActors": {
      "type": "boolean",
      "description": "Whether to delete level actors whose labels start with actorLabelPrefix.",
      "default": false
    },
    "actorLabelPrefix": {
      "type": "string",
      "description": "Safe actor label prefix for disposable actor tests. Must start with UEvolveMcpTest_ when resetActors=true.",
      "default": "UEvolveMcpTest_"
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview sandbox preparation without mutating assets.",
      "default": false
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
  "contentPath": "/Game/__UEvolveMcpTest/Actors",
  "reset": true,
  "resetActors": true,
  "actorLabelPrefix": "UEvolveMcpTest_",
  "dryRun": false
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: constrained disposable asset sandbox setup for automated happy-path tests.
- Notes: Only allows /Game/__UEAtelier* content paths.
