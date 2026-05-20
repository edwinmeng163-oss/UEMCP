# unreal.diff_project_snapshot

**Category**: self-extension
**Title**: Diff Project Snapshot
**Risk level**: read_only

Diffs two captured project snapshots and reports added/removed identities by area.

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
    "beforeSnapshotPath": {
      "type": "string",
      "description": "Before snapshot path. If omitted, the previous latest snapshot is used."
    },
    "afterSnapshotPath": {
      "type": "string",
      "description": "After snapshot path. If omitted, the latest snapshot is used."
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
  "beforeSnapshotPath": "Saved/UnrealMcp/ProjectSnapshots/uevolve-selfext-before.json",
  "afterSnapshotPath": "Saved/UnrealMcp/ProjectSnapshots/uevolve-selfext-after.json"
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: read-only diff over local project snapshots.
