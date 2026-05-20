# unreal.mcp_patch_scaffold_patch

**Category**: self-extension
**Title**: Patch Scaffold Fragment
**Risk level**: high

Edits a generated scaffold patch fragment with validation and dry-run diff support; the tool is not registered until apply runs.

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
      "description": "MCP tool name whose scaffold patch fragment should be patched. Used when scaffoldDir is empty."
    },
    "scaffoldDir": {
      "type": "string",
      "description": "Project-local scaffold directory containing the patch fragment. To patch shared repo recipes, pass toolName and leave scaffoldDir empty only after copying to a project-local draft."
    },
    "outputRoot": {
      "type": "string",
      "description": "Project-relative scaffold root used with toolName.",
      "default": "Tools/UnrealMcpToolScaffolds"
    },
    "patchName": {
      "type": "string",
      "description": "Patch file or alias: ToolRegistrar.patch.cpp, ToolRegistrarCall.patch.cpp, CategoryHandlerFunction.patch.cpp, CategoryDispatcherBranch.patch.cpp, ChatCommand.patch.cpp, or legacy fragments LegacyToolDefinition.legacy.cpp / LegacyExecuteToolHandler.legacy.cpp."
    },
    "snippetName": {
      "type": "string",
      "description": "Legacy alias for patchName."
    },
    "mode": {
      "type": "string",
      "description": "Patch mode: replace_all, replace_text, append, or prepend. Auto-selected when empty."
    },
    "newText": {
      "type": "string",
      "description": "Replacement text for replace_all mode."
    },
    "findText": {
      "type": "string",
      "description": "Exact text to find for replace_text mode."
    },
    "replaceText": {
      "type": "string",
      "description": "Replacement text for replace_text mode."
    },
    "appendText": {
      "type": "string",
      "description": "Text to append when mode=append."
    },
    "prependText": {
      "type": "string",
      "description": "Text to prepend when mode=prepend."
    },
    "replaceAll": {
      "type": "boolean",
      "description": "Replace all findText occurrences instead of just the first.",
      "default": false
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview patch changes without writing the file.",
      "default": true
    },
    "createBackup": {
      "type": "boolean",
      "description": "Create a timestamped patch backup before writing.",
      "default": true
    },
    "allowUnsafe": {
      "type": "boolean",
      "description": "Allow writing patches that fail static validation. Use only after manual review.",
      "default": false
    },
    "diffPreviewLines": {
      "type": "number",
      "description": "Maximum patch diff preview lines.",
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
  "toolName": "unreal.registry_patch_fixture",
  "scaffoldDir": "Tools/UnrealMcpTests/Fixtures/PatchScaffold",
  "patchName": "CategoryHandlerFunction.patch.cpp",
  "findText": "Registry patch fixture completed",
  "replaceText": "Registry patch fixture completed",
  "dryRun": true
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: high-impact write/build/rollback/destructive editor operation.
- Notes: Primary descriptor-first patch-fragment editor for generated MCP tool scaffolds.
