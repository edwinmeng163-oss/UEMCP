# unreal.workflow_run

**Category**: self-extension
**Title**: Run MCP Workflow
**Risk level**: high

Runs a bounded, policy-checked sequence of MCP tool calls with dry-run planning, risk gates, failure pause, and project-memory handoff.

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
    "workflowName": {
      "type": "string",
      "description": "Human-readable workflow name.",
      "default": "mcp_workflow"
    },
    "workflowJson": {
      "type": "string",
      "description": "Optional full workflow JSON object string. Useful when step arguments are complex."
    },
    "workflowPath": {
      "type": "string",
      "description": "Optional project-local workflow JSON file path. Ignored when workflowJson is provided."
    },
    "steps": {
      "type": "array",
      "description": "Bounded list of workflow steps. Each step can call one registered MCP tool.",
      "items": {
        "type": "object",
        "properties": {
          "name": {
            "type": "string",
            "description": "Human-readable step label."
          },
          "tool": {
            "type": "string",
            "description": "MCP tool name to call, for example unreal.editor_status."
          },
          "argumentsJson": {
            "type": "string",
            "description": "JSON object string used as the step tool arguments. Use {} for no arguments.",
            "default": "{}"
          },
          "skip": {
            "type": "boolean",
            "description": "Whether to skip this step.",
            "default": false
          },
          "expectError": {
            "type": "boolean",
            "description": "Whether the step is expected to return a tool error.",
            "default": false
          },
          "continueOnError": {
            "type": "boolean",
            "description": "Whether to continue the workflow if this step fails.",
            "default": false
          }
        },
        "additionalProperties": false
      }
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview the workflow without executing step tools. Defaults to true for safety.",
      "default": true
    },
    "stopOnFailure": {
      "type": "boolean",
      "description": "Stop at the first failed step unless the step has continueOnError=true.",
      "default": true
    },
    "maxSteps": {
      "type": "number",
      "description": "Maximum number of steps allowed in this run, clamped to 1-100.",
      "default": 20
    },
    "writeMemory": {
      "type": "boolean",
      "description": "Write workflow status to project memory for long-task continuation.",
      "default": true
    },
    "memoryKey": {
      "type": "string",
      "description": "Project memory key used when writeMemory=true.",
      "default": "chat.active_task"
    },
    "allowHighRisk": {
      "type": "boolean",
      "description": "Allow high-risk step tools to execute when dryRun=false.",
      "default": false
    },
    "allowCritical": {
      "type": "boolean",
      "description": "Allow critical-risk step tools to execute when dryRun=false.",
      "default": false
    },
    "includeStepStructuredContent": {
      "type": "boolean",
      "description": "Include each step structuredContent in the workflow result. Off by default to keep context compact.",
      "default": false
    },
    "maxResultChars": {
      "type": "number",
      "description": "Maximum text preview characters retained per executed step.",
      "default": 1200
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
  "workflowName": "core_readonly_plan",
  "dryRun": true,
  "writeMemory": false,
  "steps": [
    {
      "name": "status",
      "tool": "unreal.editor_status",
      "argumentsJson": "{}"
    },
    {
      "name": "audit",
      "tool": "unreal.mcp_tool_audit",
      "argumentsJson": "{}"
    }
  ]
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: generic high-level workflow executor that composes existing MCP tools behind dry-run, policy gates, and memory handoff.
- Notes: Default dryRun=true. High/critical step tools require explicit allowHighRisk or allowCritical.
