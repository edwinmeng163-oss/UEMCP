# unreal.mcp_extension_pipeline

**Category**: self-extension
**Title**: Run Extension Pipeline
**Risk level**: critical

Orchestrates scaffold validation, apply, build, restart handoff, and test execution for a new MCP tool.

## Capabilities

- Requires write: true
- Requires build: true
- Requires external process: true
- Requires restart: true
- Requires lock: true
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {
    "mode": {
      "type": "string",
      "description": "Pipeline mode: auto, apply_build, dry_run, resume_test, test_only.",
      "default": "auto"
    },
    "toolName": {
      "type": "string",
      "description": "MCP tool name to integrate/test."
    },
    "scaffoldDir": {
      "type": "string",
      "description": "Project-relative or absolute scaffold directory containing descriptor-first patches and TestRequest.json."
    },
    "outputRoot": {
      "type": "string",
      "description": "Project-relative scaffold root used with toolName.",
      "default": "Tools/UnrealMcpToolScaffolds"
    },
    "schemaJson": {
      "type": "string",
      "description": "Optional schema JSON to validate before applying patches. If omitted, the scaffold README schema is used when present."
    },
    "testRequestPath": {
      "type": "string",
      "description": "Optional TestRequest.json path. Defaults to scaffoldDir/TestRequest.json."
    },
    "testsDir": {
      "type": "string",
      "description": "Optional Tests directory. Defaults to scaffoldDir/Tests."
    },
    "memoryKey": {
      "type": "string",
      "description": "Project memory key for restart handoff.",
      "default": "mcp.extension.pipeline"
    },
    "task": {
      "type": "string",
      "description": "Natural-language task goal used by preview_change_plan and verify_task_outcome gates."
    },
    "apply": {
      "type": "boolean",
      "description": "Whether to apply scaffold patches after dry run.",
      "default": true
    },
    "build": {
      "type": "boolean",
      "description": "Whether to run Unreal Build Tool after applying patches.",
      "default": true
    },
    "runTest": {
      "type": "boolean",
      "description": "Whether to run the generated tool test when safe in the current editor session.",
      "default": true
    },
    "runTestSuite": {
      "type": "boolean",
      "description": "Run Tests/*.json suite instead of only TestRequest.json.",
      "default": true
    },
    "generateTests": {
      "type": "boolean",
      "description": "Generate or refresh Tests/*.json before apply/build/test.",
      "default": true
    },
    "overwriteTests": {
      "type": "boolean",
      "description": "Overwrite generated test files when content changes.",
      "default": true
    },
    "dryRunOnly": {
      "type": "boolean",
      "description": "Only run validate and apply dry run; skip apply/build/test.",
      "default": false
    },
    "applyChatCommand": {
      "type": "boolean",
      "description": "Whether to apply optional ChatCommand.patch.cpp.",
      "default": false
    },
    "createBackup": {
      "type": "boolean",
      "description": "Whether to create rollback backup during real apply.",
      "default": true
    },
    "backupProjectState": {
      "type": "boolean",
      "description": "Create a broad project-state snapshot before real apply/build/test changes.",
      "default": true
    },
    "writeProjectMemory": {
      "type": "boolean",
      "description": "Whether to write pipeline state into project memory.",
      "default": true
    },
    "enforceGate": {
      "type": "boolean",
      "description": "Require preview_change_plan before schema validation and dry-run/apply.",
      "default": true
    },
    "captureSnapshots": {
      "type": "boolean",
      "description": "Capture before/after project snapshots around real pipeline work.",
      "default": true
    },
    "verifyOutcome": {
      "type": "boolean",
      "description": "Run verify_task_outcome after tests when no restart deferral is required.",
      "default": true
    },
    "classifyFailures": {
      "type": "boolean",
      "description": "Classify failed pipeline steps and attach fix/rollback guidance.",
      "default": true
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
- Reason: Explicit registry: can execute dynamic code or orchestrate source/build/restart self-extension.
