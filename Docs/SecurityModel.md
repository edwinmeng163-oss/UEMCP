# Unreal MCP Security Model

## Trust Boundary

The MCP endpoint is intended for local development by default. It runs inside Unreal Editor and can perform powerful editor actions. Treat it as a developer tool, not a public network service.

## Local-First Defaults

- Default host is localhost.
- Optional auth token and allowed origins are configured through Unreal MCP settings.
- Generated runtime state is stored under `Saved/UnrealMcp`.

## Tool Risk Levels

Recommended policy categories:

- Read-only: status, list assets, map check, audit.
- Editor write: actor transforms, Blueprint edits, widget edits, save packages.
- Code write: scaffold patch/apply, patch-fragment edits, rollback.
- Build/process: build editor, supervisor launch/restart.
- Dynamic execution: Python and console command execution.
- Local Task Atlas writes: ActivityLog annotation, task rating, and task pinning
  mutate only ignored `Saved/UnrealMcp` JSON/JSONL state.

Each AI-facing tool now carries the same policy object in `tools/list`, `unreal.mcp_tool_audit`, and `unreal.mcp_workbench_status`:

- `riskLevel`
- `requiresWrite`
- `requiresBuild`
- `requiresExternalProcess`
- `requiresRestart`
- `requiresProjectMemory`
- `requiresLock`

## Schema Safety

AI-facing tools should use fixed JSON schemas. OpenAI function calling rejects schemas that rely on `additionalProperties=true`.

Legacy flexible-schema tools are kept for compatibility but hidden from AI-facing `tools/list`:

- `unreal.batch_set_actor_properties`
- `unreal.spawn_actor`
- `unreal.spawn_actor_batch`

Use fixed-schema wrappers instead.

## Source Mutation Safety

AI self-extension is limited to project-local Python user tools:

- `unreal.scaffold_mcp_tool` advertises `implementationTrack=python`.
- `unreal.mcp_user_registry_reload` and `unreal.mcp_user_tool_smoke` are the
  required gates before a new user tool is callable.
- `unreal.mcp_apply_scaffold` and `unreal.mcp_extension_pipeline` are hidden
  from AI-facing `tools/list` and are manual/developer-only core integration
  paths.
- `unreal.workflow_run` rejects steps targeting hidden tools, so the AI cannot
  relay through workflow composition to reach hidden core apply/pipeline tools.

Manual developer source-mutation tools should preserve these rules:

- Dry run before apply.
- Static patch-fragment validation before source insertion.
- Lock during apply/build/test/rollback.
- Backup before source mutation.
- Manifest after source mutation.
- Manifest includes `schemaVersion`, `manifestSchema`, active `sessionId`, conflict counts, conflict policy, and source hashes.
- Build log capture after compile.
- Test suite after restart.
- Rollback path if build or test fails.

`unreal.execute_python` and `unreal.execute_python_file` remain visible for
editor automation, but the central assistant prompt forbids using them to modify
plugin source under `Plugins/UnrealMcp/Source` or ToolRegistry JSON. A hard
runtime sandbox for that Python path is deferred hardening work.

## Code File Edit Safety (Code Tools)

The `code` tool category (v0.29) edits code files (`.cpp`/`.h`/`.uplugin`/`.py`/...)
under path policy, distinct from self-extension. Guarantees:

- **Canonical-path classification**: every edit path is resolved (collapse `..`,
  resolve symlinks to their target, macOS casefold, Unicode NFC) before
  classification, and the fall-through is DENY. A symlink in a writable root pointing
  at a forbidden target is forbidden by its target.
- **Three writable tiers**: default-writable (`Tools/UnrealMcpPyToolSamples`,
  `Tools/UnrealMcpPyTools`); high-risk requiring `confirmHighRisk` (`Source/**`,
  `Plugins/*/Source/**`, `*.uplugin`, `*.uproject`, `Config/*.ini`); forbidden
  (`Plugins/UnrealMcp/**`, `Binaries`/`Intermediate`/`Saved`/`Content`, the Engine
  install, `*.generated.h`/`*.gen.cpp`/`*.uasset`/`*.umap`).
- **Stale-context + byte safety**: apply requires the file's whole-file
  `expectedSha256` to still match (TOCTOU re-checked per file immediately before each
  write); content is read/matched/written as raw bytes, so CRLF and BOM are preserved
  and never normalized.
- **Backup + atomic manifest + rollback**: real apply backs up originals, writes an
  atomic manifest (`<target>.tmp.<guid>` + rename) with an `applyState` recovery
  marker, and is reversible via `code_rollback_change` (drift-checked, `force` to
  override). Multi-file applies are transactional. Independent area:
  `Saved/UnrealMcp/CodeChanges/`.
- **Lock + assistant-scoped approval**: real apply/rollback hold the extension
  session lock (mutex with scaffold apply / build / rollback); an autonomous-AI
  `dryRun=false` write requires approval, while reads and dry runs do not.
- **Core plugin off-limits**: `Plugins/UnrealMcp/**` is forbidden to Code Tools —
  core C++ tool development stays a deliberate PM-directed hand-edit (see
  `Tools/codex-prompt-header.md`), never an AI Code-Tools write.

See `Docs/CodeTools.md` and `Docs/agents-guide/code-tools.md`.

## Tool Outcome Verification

Write-capable tools build structured `preflight` results before handler execution and attach `postcheck` results after execution. Generic checks come from ToolRegistry metadata. Blueprint, Widget, Actor, Memory, Skill, Scaffold, and Self-extension tools additionally inspect real editor/file/workflow state before and after execution, so Chat and Workbench can distinguish "the tool returned success" from "the target asset, graph, widget, actor, transform, memory key, skill file, manifest, build log, or test result actually exists as expected."

Task Atlas v0.17 does not promote tasks into skills, RAG, or new tools. The
`To Skills`, `To RAG`, and `Make Tool` UI buttons are placeholders only.

## Remaining Hardening Work

- Add CI coverage for ToolRegistry/ToolHandlerRegistry validation.
- Add optional policy enforcement that blocks high-risk tools unless enabled.
- Add an audit log for all write-capable tool calls.
- Add CI checks for schema compatibility and missing documentation.
- Add a Workbench UI warning surface for risky actions.
- Add a hard sandbox preventing Python execution tools from modifying plugin
  source or ToolRegistry JSON.
