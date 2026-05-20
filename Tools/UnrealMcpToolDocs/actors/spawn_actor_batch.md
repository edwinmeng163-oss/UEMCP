# unreal.spawn_actor_batch

**Category**: actors
**Title**: Spawn Actor Batch
**Risk level**: medium
**Exposure**: legacy_hidden

Spawns multiple actors from class paths with optional transforms and property overrides; legacy flexible batch path.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {},
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
- Reason: Explicit registry: actors tool with controlled write or workflow side effects.
- Notes: Legacy batch spawn supports freeform item objects; prefer unreal.spawn_actor_batch_basic.
