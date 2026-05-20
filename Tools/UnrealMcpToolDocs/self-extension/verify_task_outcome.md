# unreal.verify_task_outcome

**Category**: self-extension
**Title**: Verify Task Outcome
**Risk level**: read_only

Checks task completion evidence using tools/list, snapshot diffs, and required text evidence.

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
    "task": {
      "type": "string",
      "description": "Original task goal to verify."
    },
    "beforeSnapshotPath": {
      "type": "string",
      "description": "Optional before snapshot path."
    },
    "afterSnapshotPath": {
      "type": "string",
      "description": "Optional after snapshot path."
    },
    "expectedChangedAreas": {
      "type": "array",
      "description": "Areas expected to change: actors, assets, blueprints, widgets, memory, or skills.",
      "items": {
        "type": "string"
      }
    },
    "expectedTools": {
      "type": "array",
      "description": "Tool names expected to exist in tools/list.",
      "items": {
        "type": "string"
      }
    },
    "evidenceText": {
      "type": "string",
      "description": "Optional tool output or summary text to inspect."
    },
    "requiredEvidenceText": {
      "type": "string",
      "description": "Optional substring that must appear in evidenceText."
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
  "task": "Verify self-extension precision tools are registered.",
  "expectedTools": [
    "unreal.preview_change_plan",
    "unreal.capture_project_snapshot",
    "unreal.diff_project_snapshot",
    "unreal.verify_task_outcome"
  ],
  "evidenceText": "descriptor-backed precision tools are registered",
  "requiredEvidenceText": "precision tools"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: read-only final task verification based on tools/list, snapshots, and evidence text.
