# unreal.mcp_apply_scaffold

**Category**: self-extension
**Title**: Apply MCP Scaffold
**Risk level**: high
**Exposure**: developer/manual only; hidden from AI-facing `tools/list`

Validates and applies a generated MCP scaffold to plugin source and registry files, with dry-run and backup manifest support. This is a manual developer core-integration tool; AI self-extension uses project-local Python user tools instead.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "toolName": {
      "type": "string",
      "description": "Tool name whose scaffold should be applied. Used when scaffoldDir is empty."
    },
    "scaffoldDir": {
      "type": "string",
      "description": "Project-local scaffold directory containing generated descriptor-first patch files. To apply shared repo recipes, pass toolName and leave scaffoldDir empty."
    },
    "outputRoot": {
      "type": "string",
      "description": "Project-relative scaffold root used with toolName.",
      "default": "Tools/UnrealMcpToolScaffolds"
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview changes without modifying source.",
      "default": true
    },
    "applyChatCommand": {
      "type": "boolean",
      "description": "Whether to apply optional ChatCommand.patch.cpp.",
      "default": false
    },
    "createBackup": {
      "type": "boolean",
      "description": "Whether to create rollback backup and manifest when dryRun=false.",
      "default": true
    },
    "validatePatches": {
      "type": "boolean",
      "description": "Whether to run C++ patch-fragment safety validation before applying.",
      "default": true
    },
    "allowUnsafePatches": {
      "type": "boolean",
      "description": "Allow applying patch fragments that fail static validation. Use only after manual review.",
      "default": false
    },
    "targetDiffPreviewLines": {
      "type": "number",
      "description": "Maximum target source diff preview lines returned during dry run/apply.",
      "default": 120
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
  "toolName": "unreal.fps.bootstrap",
  "dryRun": true
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: high-impact write/build/rollback/destructive editor operation.
