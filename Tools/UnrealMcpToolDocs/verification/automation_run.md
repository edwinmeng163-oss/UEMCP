# unreal.automation_run

**Category**: verification
**Title**: Run Automation Test
**Risk level**: medium

Queues one Unreal Automation Framework test and returns immediately with a runId for polling.

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
    "fullName": {
      "type": "string",
      "description": "Exact canonical FAutomationTestFramework test name to run."
    },
    "timeoutSeconds": {
      "type": "number",
      "description": "Wall-clock timeout before reports surface timed_out and stale recovery can later free the slot.",
      "default": 120
    },
    "tags": {
      "type": "array",
      "description": "Caller metadata echoed into the run report; not passed to UE Automation filters.",
      "items": {
        "type": "string"
      }
    }
  },
  "required": [
    "fullName"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "fullName": "UnrealMcp.AutomationTools.InternalPass",
  "timeoutSeconds": 30,
  "tags": [
    "release-gate",
    "manual"
  ]
}
```

## Provenance
- Source docs: Docs/Verification.md
- Reason: v0.20 C1a verification foundation async single automation test runner with state-file reports.
- Notes: Only one queued/running automation run is accepted at a time. tags are caller metadata only.
