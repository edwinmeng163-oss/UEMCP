# unreal.tools.import_package

**Category**: self-extension
**Title**: Import Tool Package
**Risk level**: high

Validates and imports a portable tool package zip into the local registry/scaffold/test tree; registry-only packages are refused unless caller explicitly opts in.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "packagePath": {
      "type": "string",
      "description": "Project-relative or absolute tool package zip path."
    },
    "dryRun": {
      "type": "boolean",
      "description": "Validate and preview the import plan without mutating registry, scaffold, or test files.",
      "default": true
    },
    "overwriteScaffold": {
      "type": "boolean",
      "description": "Allow scaffold files from the package to overwrite existing scaffold files.",
      "default": false
    },
    "acceptRegistryOnly": {
      "type": "boolean",
      "description": "Expert mode: allow importing a registry-only package whose handler implementation must already exist locally.",
      "default": false
    },
    "skipLock": {
      "type": "boolean",
      "description": "Testing-only escape hatch for in-process test execution; normal Chat use should leave this false.",
      "default": false
    }
  },
  "required": [
    "packagePath"
  ],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "packagePath": "Saved/UnrealMcp/Packages/unreal.configure_fps_settings-phase2-test.zip",
  "dryRun": true,
  "skipLock": true
}
```

## Provenance
- Source docs: Docs/SelfExtensionPipeline.md#tool-sharing
- Reason: Validates a tool package manifest before importing registry, scaffold, and test assets; refuses registry-only packages unless explicitly accepted.
- Notes: Tool sharing package import.
