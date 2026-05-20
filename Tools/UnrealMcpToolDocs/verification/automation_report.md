# unreal.automation_report

**Category**: verification
**Title**: Get Automation Run Report
**Risk level**: read_only

Returns the current or persisted report for an async automation test run.

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
    "runId": {
      "type": "string",
      "description": "Automation runId returned by unreal.automation_run."
    }
  },
  "required": [
    "runId"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "runId": "20990101T000000Z-ffffff"
}
```

## Provenance
- Source docs: Docs/Verification.md
- Reason: v0.20 C1a verification foundation read-only automation run polling/report tool.
- Notes: The public report shape excludes internal state-file bookkeeping such as timeoutSecondsConfigured.
