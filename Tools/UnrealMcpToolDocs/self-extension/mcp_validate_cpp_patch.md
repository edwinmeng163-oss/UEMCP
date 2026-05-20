# unreal.mcp_validate_cpp_patch

**Category**: self-extension
**Title**: Validate C++ Patch
**Risk level**: read_only

Validates scaffold C++ patch fragments for required anchors, unsafe operations, and self-extension integration risks.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "patchText": {
      "type": "string",
      "description": "Raw C++ patch/legacy fragment text to validate. If empty, reads scaffoldDir/toolName + patchName."
    },
    "patchName": {
      "type": "string",
      "description": "Patch file or alias: ToolRegistrar.patch.cpp, ToolRegistrarCall.patch.cpp, CategoryHandlerFunction.patch.cpp, CategoryDispatcherBranch.patch.cpp, ChatCommand.patch.cpp, or legacy fragments LegacyToolDefinition.legacy.cpp / LegacyExecuteToolHandler.legacy.cpp.",
      "default": "ToolRegistrar.patch.cpp"
    },
    "snippetText": {
      "type": "string",
      "description": "Legacy alias for patchText."
    },
    "snippetName": {
      "type": "string",
      "description": "Legacy alias for patchName.",
      "default": "ToolRegistrar.patch.cpp"
    },
    "toolName": {
      "type": "string",
      "description": "Expected MCP tool name for tool-name literal checks."
    },
    "scaffoldDir": {
      "type": "string",
      "description": "Project-local scaffold directory to read when snippetText is empty. To validate shared repo recipes, pass toolName and leave scaffoldDir empty."
    },
    "outputRoot": {
      "type": "string",
      "description": "Project-relative scaffold root used with toolName.",
      "default": "Tools/UnrealMcpToolScaffolds"
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
  "patchName": "CategoryHandlerFunction.patch.cpp"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: read-only inspection, audit, status, schema validation, memory read, or skill read tool.
- Notes: Primary descriptor-first C++ patch-fragment validator.
