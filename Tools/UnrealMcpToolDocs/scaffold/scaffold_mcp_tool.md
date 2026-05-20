# unreal.scaffold_mcp_tool

**Category**: scaffold
**Title**: Scaffold MCP Tool
**Risk level**: medium

Generates a project-local MCP tool scaffold with metadata, registry patch, C++ patch fragments, tests, and docs.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
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
      "description": "New MCP tool name. Must start with unreal., for example unreal.my_custom_tool."
    },
    "title": {
      "type": "string",
      "description": "Human-readable tool title."
    },
    "description": {
      "type": "string",
      "description": "Short tool description for tools/list and AI tool selection."
    },
    "outputRoot": {
      "type": "string",
      "description": "Project-relative output directory for generated scaffold files.",
      "default": "Tools/UnrealMcpToolScaffolds"
    },
    "argumentSchemaJson": {
      "type": "string",
      "description": "Reference JSON schema for the intended arguments. Keep it fixed-schema for AI compatibility."
    },
    "exampleArgumentsJson": {
      "type": "string",
      "description": "Example arguments object JSON for the generated test request.",
      "default": "{\"message\":\"hello\"}"
    },
    "implementationNotes": {
      "type": "string",
      "description": "Optional implementation notes to include in the generated README."
    },
    "category": {
      "type": "string",
      "description": "Tool category/dispatcher owner: actors, blueprint, editor, memory, scaffold, self-extension, skills, or widget.",
      "default": "self-extension"
    },
    "riskLevel": {
      "type": "string",
      "description": "Tool risk level: read_only, low, medium, high, or critical.",
      "default": "low"
    },
    "requiresWrite": {
      "type": "boolean",
      "description": "Whether the tool mutates project/editor state.",
      "default": false
    },
    "requiresBuild": {
      "type": "boolean",
      "description": "Whether the tool requires a build step.",
      "default": false
    },
    "requiresExternalProcess": {
      "type": "boolean",
      "description": "Whether the tool starts or depends on an external process.",
      "default": false
    },
    "requiresRestart": {
      "type": "boolean",
      "description": "Whether the tool requires an editor restart to fully verify.",
      "default": false
    },
    "requiresProjectMemory": {
      "type": "boolean",
      "description": "Whether the tool should read/write project memory as part of normal operation.",
      "default": false
    },
    "requiresLock": {
      "type": "boolean",
      "description": "Whether the tool must acquire the self-extension session lock.",
      "default": false
    },
    "dryRunSupport": {
      "type": "boolean",
      "description": "Whether the generated tool should expose a dryRun argument.",
      "default": false
    },
    "overwrite": {
      "type": "boolean",
      "description": "Whether to overwrite an existing scaffold folder.",
      "default": false
    },
    "includeChatCommandSnippet": {
      "type": "boolean",
      "description": "Whether to generate an optional direct slash-command patch fragment.",
      "default": true
    },
    "includeLegacyCompatibility": {
      "type": "boolean",
      "description": "Also generate legacy ToolDefinition/ExecuteToolHandler fragments. Disabled by default; new tools should use descriptor-first patches.",
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
- Reason: Explicit registry: scaffold tool with controlled write or workflow side effects.
