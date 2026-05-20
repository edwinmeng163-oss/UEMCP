# unreal.widget_build_template

**Category**: widget
**Title**: Build Widget Template
**Risk level**: medium

Builds a predefined Widget Blueprint layout template for common HUD or panel workflows.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "widgetBlueprintPath": {
      "type": "string",
      "description": "Widget Blueprint asset path to create or rebuild."
    },
    "templateName": {
      "type": "string",
      "description": "Template preset name. Currently supports mcp_demo_hud.",
      "default": "mcp_demo_hud"
    },
    "title": {
      "type": "string",
      "description": "Title text for the generated template.",
      "default": "MCP Demo"
    },
    "replaceRoot": {
      "type": "boolean",
      "description": "Whether to replace the existing root widget tree.",
      "default": true
    },
    "compile": {
      "type": "boolean",
      "description": "Whether to compile after building the template. For large first-time templates, prefer false then call unreal.bp_compile_save.",
      "default": false
    },
    "savePackage": {
      "type": "boolean",
      "description": "Whether to save the Widget Blueprint package after building. For large first-time templates, prefer false then call unreal.bp_compile_save.",
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
  "widgetBlueprintPath": "/Game/__UEvolveMcpTest/Widget/WBP_McpSmoke",
  "templateName": "mcp_demo_hud",
  "title": "UEAtelier MCP Smoke",
  "replaceRoot": true,
  "compile": false,
  "savePackage": false
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: Widget template write tool with automated disposable sandbox category coverage.
