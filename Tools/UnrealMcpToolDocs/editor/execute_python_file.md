# unreal.execute_python_file

**Category**: editor
**Title**: Execute Python File
**Risk level**: critical

Runs a project-local Python file through the Unreal Python runtime and returns execution output.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: true
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
    "scriptPath": {
      "type": "string",
      "description": "Relative or absolute .py file path. Relative paths are resolved against the project directory."
    },
    "args": {
      "type": "array",
      "description": "Optional command-line arguments passed to the Python script.",
      "items": {
        "type": "string"
      }
    },
    "scope": {
      "type": "string",
      "description": "Python file execution scope: Private or Public.",
      "default": "Private"
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
    },
    "allowOutsideProject": {
      "type": "boolean",
      "description": "Allow scriptPath outside this Unreal project directory.",
      "default": false
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
