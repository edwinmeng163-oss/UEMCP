# unreal.compile_blueprints_in_path

**Category**: blueprint
**Title**: Compile Blueprints In Path
**Risk level**: medium

Compiles Blueprint assets under a Content Browser path, with recursive scanning and a limit to bound large projects.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {
    "path": {
      "type": "string",
      "description": "Content Browser path to scan for Blueprints.",
      "default": "/Game"
    },
    "recursive": {
      "type": "boolean",
      "description": "Whether to include child paths.",
      "default": true
    },
    "limit": {
      "type": "number",
      "description": "Maximum number of Blueprints to compile in one call.",
      "default": 100
    }
  },
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
- Reason: Explicit registry: blueprint tool with controlled write or workflow side effects.
