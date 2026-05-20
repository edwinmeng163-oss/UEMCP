# unreal.redirector_fixup

**Category**: editor
**Title**: Fix Up Redirectors
**Risk level**: high

Scans a /Game subtree for UObjectRedirector assets and runs AssetTools fixup to rewrite referencers and remove fixed redirectors.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: core

## Input schema

```json
{
  "type": "object",
  "properties": {
    "path": {
      "type": "string",
      "description": "Content Browser subtree to scan for UObjectRedirector assets.",
      "default": "/Game"
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview redirectors and affected referencers without mutating packages.",
      "default": true
    },
    "recursive": {
      "type": "boolean",
      "description": "Whether to include child folders.",
      "default": true
    },
    "failOnAnyError": {
      "type": "boolean",
      "description": "Return FIXUP_FAILED when any redirector remains after a real fixup.",
      "default": false
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
  "path": "/Game",
  "recursive": true,
  "dryRun": true
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: v0.15 chunk 5 migration tool: Content Browser redirector cleanup through AssetTools fixup.
- Notes: PIE-blocked. Default dryRun=true. Real run reports remaining redirectors as failures and supports failOnAnyError.
