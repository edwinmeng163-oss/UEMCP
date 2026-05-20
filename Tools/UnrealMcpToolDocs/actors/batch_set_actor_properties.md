# unreal.batch_set_actor_properties

**Category**: actors
**Title**: Batch Set Actor Properties
**Risk level**: medium
**Exposure**: legacy_hidden

Sets arbitrary property maps on multiple actors in one call; legacy flexible path for bulk edits when fixed-schema actor tools are not enough.

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
- Notes: Legacy flexible property map uses additionalProperties=true; prefer fixed-schema batch actor tools.
