# unreal.automation_list

**Category**: verification
**Title**: List Automation Tests
**Risk level**: read_only

Lists runnable Unreal Automation Framework tests with canonical fullName, display name, flags, and optional details.

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
    "filter": {
      "type": "string",
      "description": "Optional case-insensitive substring filter matched against fullName and displayName."
    },
    "includeDetails": {
      "type": "boolean",
      "description": "Include category and best-effort requirement metadata.",
      "default": false
    },
    "limit": {
      "type": "number",
      "description": "Maximum automation tests to return.",
      "default": 200
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
  "filter": "UnrealMcp",
  "includeDetails": true,
  "limit": 25
}
```

## Provenance
- Source docs: Docs/Verification.md
- Reason: v0.20 C1a verification foundation read-only automation test discovery.
- Notes: Uses FAutomationTestFramework::GetValidTestNames. includeDetails adds category and best-effort requirement hints.
