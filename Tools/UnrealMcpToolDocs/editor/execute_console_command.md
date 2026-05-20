# unreal.execute_console_command

**Category**: editor
**Title**: Execute Console Command
**Risk level**: critical

Runs an Unreal console command in the editor session; use only for explicit high-trust operations.

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
  "properties": {
    "command": {
      "type": "string",
      "description": "Editor or PIE console command to execute."
    },
    "target": {
      "type": "string",
      "description": "Where to run the command: auto, editor, or pie.",
      "default": "auto"
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
- Reason: Explicit registry: can execute dynamic code or orchestrate source/build/restart self-extension.
