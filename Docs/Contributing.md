# Contributing to Unreal MCP

## Development Principle

Prefer small, reviewable tool changes over broad source edits. The self-extension pipeline is powerful, but it should still produce human-reviewable diffs.

## Before Adding a Tool

1. Define the tool purpose and owner category.
2. Prefer fixed schemas with `additionalProperties=false`.
3. Use existing fixed-schema wrappers before adding flexible inputs.
4. Add docs and at least one test request.
5. Run schema validation and tool audit.

## Recommended Extension Flow

1. Generate a scaffold with `unreal.scaffold_mcp_tool`.
2. Inspect with `unreal.mcp_inspect_scaffold`.
3. Validate schema with `unreal.mcp_validate_tool_schema`.
4. Validate snippets with `unreal.mcp_validate_cpp_snippet`.
5. Preview with `unreal.mcp_apply_scaffold` using `dryRun=true`.
6. Apply with backups.
7. Build with `unreal.mcp_build_editor`.
8. Restart and run `unreal.mcp_run_test_suite`.
9. Run `unreal.mcp_tool_audit`.
10. Check `unreal.mcp_workbench_status`.

## Ownership Guidelines

Suggested ownership domains:

- MCP protocol and settings.
- Editor and asset tools.
- Actor tools.
- Blueprint graph tools.
- Widget Blueprint tools.
- Self-extension pipeline.
- Supervisor and cross-platform launch.
- Documentation and tests.

Avoid having multiple people edit `UnrealMcpModule.cpp` in unrelated regions at the same time until the module is split.

## Git Hygiene

- Do not commit `Saved/`, `Intermediate/`, `Binaries/`, or generated local supervisor files.
- Commit source, docs, stable test fixtures, and project-local skills.
- Use Git LFS for Unreal binary assets.
- Keep generated runtime manifests local unless they are intentionally promoted to documentation or tests.

## Review Checklist

- Tool appears in `tools/list` if AI-facing.
- Tool has a handler.
- Tool is documented in root or plugin README.
- Schema passes OpenAI function calling compatibility checks.
- Audit output includes appropriate ToolRegistry policy metadata.
- Write-capable tools use dry run or clear safety controls where practical.
- Self-extension source changes have a backup/rollback path.
