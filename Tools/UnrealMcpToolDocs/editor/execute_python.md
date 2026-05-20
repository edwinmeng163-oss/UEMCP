# unreal.execute_python

**Category**: editor
**Title**: Execute Python
**Risk level**: critical

Executes a Python command string inside Unreal Editor and returns captured output or errors.

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
      "description": "Literal Python code, an expression, or a .py file path with optional arguments. Multiline code is automatically run as ExecuteFile unless autoMode=false."
    },
    "mode": {
      "type": "string",
      "description": "Python execution mode: Auto, ExecuteFile, ExecuteStatement, or EvaluateStatement.",
      "default": "Auto"
    },
    "scope": {
      "type": "string",
      "description": "Python file execution scope: Private or Public.",
      "default": "Private"
    },
    "autoMode": {
      "type": "boolean",
      "description": "Whether to protect explicit ExecuteStatement/EvaluateStatement requests by switching multiline code to ExecuteFile.",
      "default": true
    },
    "forceEnable": {
      "type": "boolean",
      "description": "Whether to force Python initialization if the plugin is loaded but not initialized.",
      "default": true
    },
    "unattended": {
      "type": "boolean",
      "description": "Whether to run the Python command in unattended mode.",
      "default": true
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
