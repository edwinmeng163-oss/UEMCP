# unreal.editor.python_runtime_info

**Category**: editor
**Title**: Editor Python Runtime Info
**Risk level**: critical

Reports read-only Python runtime interpreter, module-count, and Unreal module details to confirm the python implementation track is working.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: true
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: false
- Postcheck support: false
- Test coverage: manual

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
- Reason: Python implementation track smoke tool. Underlying operation is read-only, but Lane E defaults python-track registry entries to critical risk and external-process handling.
- Notes: Read-only canonical first Python-track smoke tool; use to verify PythonScriptPlugin bridge execution and handler hash enforcement.
