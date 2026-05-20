# unreal.pie_smoke

**Category**: verification
**Title**: Run PIE Smoke
**Risk level**: high

Queues a Play In Editor smoke verification run, observes BeginPIE/alive-window/EndPIE, and returns a runId for automation_report polling.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "mapPath": {
      "type": "string",
      "description": "Optional /Game/... UWorld asset path to open before PIE smoke."
    },
    "timeoutSeconds": {
      "type": "number",
      "description": "Total wall-clock timeout. Defaults to 60 and is clamped to 10..300.",
      "default": 60,
      "minimum": 10,
      "maximum": 300
    },
    "aliveWindowSeconds": {
      "type": "number",
      "description": "PIE alive-window duration. Defaults to 5 and is clamped to 1..30; must be less than timeoutSeconds.",
      "default": 5,
      "minimum": 1,
      "maximum": 30
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
  "timeoutSeconds": 60,
  "aliveWindowSeconds": 5
}
```

## Provenance
- Source docs: Docs/Verification.md
- Reason: v0.22 C1b PIE runtime smoke verification with shared automation_report polling.
- Notes: Shares the AutomationRuns state-file namespace and active-run lock with automation_run; poll with unreal.automation_report.
