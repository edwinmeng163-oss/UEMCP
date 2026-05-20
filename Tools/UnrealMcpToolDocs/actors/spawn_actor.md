# unreal.spawn_actor

**Category**: actors
**Title**: Spawn Actor
**Risk level**: medium
**Exposure**: legacy_hidden

Spawns an actor from a class path with optional transform and freeform property overrides; legacy flexible path for expert use.

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
- Notes: Legacy spawn tool supports freeform property overrides; prefer unreal.spawn_actor_basic or unreal.spawn_static_mesh_actor.
