# unreal.install_doctor

**Category**: self-extension
**Title**: Run Install Doctor
**Risk level**: low

Runs read-only runtime install checks for registry mirrors, schemas, Python plugin state, MCP ports, and duplicate UnrealMcp plugin copies.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "includeDetails": {
      "type": "boolean",
      "description": "Include full per-check details and recommended fixes when available.",
      "default": false
    },
    "refresh": {
      "type": "boolean",
      "description": "Run checks now instead of using a recent cached result.",
      "default": false
    },
    "deepScanEnginePlugins": {
      "type": "boolean",
      "description": "Opt in to a bounded two-level scan under Engine/Plugins for duplicate UnrealMcp copies.",
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
  "includeDetails": true,
  "refresh": true,
  "deepScanEnginePlugins": false
}
```

## Provenance
- Source docs: Docs/DeploymentTroubleshooting.md
- Reason: Read-only runtime install diagnostics for mirrors, plugins, ports, and duplicate installs.
- Notes: Reuses the C5 install integrity vocabulary for editor-runtime diagnostics.
