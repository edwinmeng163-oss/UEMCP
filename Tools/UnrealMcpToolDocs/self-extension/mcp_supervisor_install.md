# unreal.mcp_supervisor_install

**Category**: self-extension
**Title**: Install MCP Supervisor
**Risk level**: high

Installs project-local supervisor launch templates and scripts used for restart-aware self-extension automation.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: true
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
    "platform": {
      "type": "string",
      "description": "Launcher platform to generate: all, macos, or windows.",
      "default": "all"
    },
    "outputDir": {
      "type": "string",
      "description": "Project-relative output directory for generated supervisor launchers.",
      "default": "Tools/UnrealMcpSupervisor"
    },
    "label": {
      "type": "string",
      "description": "macOS LaunchAgent label."
    },
    "memoryKey": {
      "type": "string",
      "description": "Pipeline memory key embedded in generated commands.",
      "default": "mcp.extension.pipeline"
    },
    "argsJson": {
      "type": "string",
      "description": "Pipeline args JSON embedded in generated commands. Defaults to {\"memoryKey\": memoryKey}."
    },
    "endpointUrl": {
      "type": "string",
      "description": "MCP endpoint URL used by generated supervisor commands.",
      "default": "http://127.0.0.1:8765/mcp"
    },
    "supervisorLogDir": {
      "type": "string",
      "description": "Directory where generated supervisor commands should write logs.",
      "default": "Saved/UnrealMcp/SupervisorLogs"
    },
    "editorCmd": {
      "type": "string",
      "description": "Optional UnrealEditor executable path for generated commands."
    },
    "installLaunchAgent": {
      "type": "boolean",
      "description": "Also copy the generated macOS plist to ~/Library/LaunchAgents.",
      "default": false
    },
    "launchAtLoad": {
      "type": "boolean",
      "description": "Set RunAtLoad=true in the generated macOS LaunchAgent.",
      "default": false
    },
    "autoRestart": {
      "type": "boolean",
      "description": "Generate commands with supervisor pipeline --auto-restart.",
      "default": true
    },
    "overwrite": {
      "type": "boolean",
      "description": "Overwrite existing generated launcher files.",
      "default": true
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview the install generator command without writing launcher files.",
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
  "platform": "windows",
  "outputDir": "Tools/UnrealMcpSupervisor",
  "endpointUrl": "http://127.0.0.1:8765/mcp",
  "supervisorLogDir": "Saved/UnrealMcp/SupervisorLogs",
  "dryRun": true,
  "skipLock": true
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: high-impact write/build/rollback/destructive editor operation.
