# unreal.scaffold_recipe

**Category**: scaffold
**Title**: Scaffold High-Level Recipe
**Risk level**: low

Prepares a bounded high-level editor recipe with ordered tools, verification gates, and optional chat.active_task memory handoff.

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
    "recipeName": {
      "type": "string",
      "description": "Recipe name: first_person_ground_character, widget_hud, or mcp_self_extension_pipeline.",
      "default": "first_person_ground_character"
    },
    "rootPath": {
      "type": "string",
      "description": "Content Browser root used in generated example tool calls.",
      "default": "/Game/UEvolveRecipes"
    },
    "taskName": {
      "type": "string",
      "description": "Optional human-readable task label to store with the recipe."
    },
    "writeMemory": {
      "type": "boolean",
      "description": "Whether to write this recipe into project memory for Chat continuation.",
      "default": true
    },
    "memoryKey": {
      "type": "string",
      "description": "Project memory key to write when writeMemory=true.",
      "default": "chat.active_task"
    },
    "includeToolCalls": {
      "type": "boolean",
      "description": "Whether to include example tool-call JSON for the recipe.",
      "default": true
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
  "recipeName": "first_person_ground_character",
  "rootPath": "/Game/__UEvolveMcpTest/Recipes",
  "writeMemory": false,
  "includeToolCalls": true
}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: high-level recipe scaffold that writes optional continuation memory and reduces long-loop tool exploration.
- Notes: First batch recipes: first_person_ground_character, widget_hud, mcp_self_extension_pipeline.
