# unreal.tools.export_package

**Category**: self-extension
**Title**: Export Tool Package
**Risk level**: medium

Exports a scaffold-backed MCP tool to a portable zip package under Saved/UnrealMcp/Packages/; expert flag allowRegistryOnly=true emits a non-portable registry-only package that import refuses by default.

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
    "toolName": {
      "type": "string",
      "description": "MCP tool name to export. Portable exports require a matching scaffold under Tools/UnrealMcpToolScaffolds or Saved/UnrealMcp/TestScaffolds."
    },
    "version": {
      "type": "string",
      "description": "Optional package version suffix. Defaults to a UTC timestamp."
    },
    "packagePath": {
      "type": "string",
      "description": "Optional project-relative package zip path. Defaults to Saved/UnrealMcp/Packages/<toolName>-<version>.zip."
    },
    "scaffoldDir": {
      "type": "string",
      "description": "Deprecated compatibility field. Portable exports resolve the reviewed scaffold roots automatically."
    },
    "outputRoot": {
      "type": "string",
      "description": "Deprecated compatibility field. Portable exports resolve Tools/UnrealMcpToolScaffolds and Saved/UnrealMcp/TestScaffolds automatically.",
      "default": "Tools/UnrealMcpToolScaffolds"
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview package manifest and entries without writing the zip.",
      "default": true
    },
    "allowRegistryOnly": {
      "type": "boolean",
      "description": "Expert mode: allow exporting a registry-only package when no portable scaffold exists. Import refuses this package unless acceptRegistryOnly=true.",
      "default": false
    }
  },
  "required": [
    "toolName"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "toolName": "unreal.configure_fps_settings",
  "version": "phase2-test",
  "dryRun": true
}
```

## Provenance
- Source docs: Docs/SelfExtensionPipeline.md#tool-sharing
- Reason: Exports only scaffold-backed portable tools by default; expert allowRegistryOnly mode emits non-portable registry-only packages.
- Notes: Tool sharing package export.
