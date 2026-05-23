# MCP Self Extension

UEAtelier AI playbook for "do X in Unreal", "make a tool", and code changes: use disciplined, surgical engineering; route goals through existing MCP capabilities first; create new AI-made tools only as project-local Python user tools with reload and smoke gates.

## Coding discipline

Apply these four principles before editing code or creating tools:

1. **Think Before Coding**: state assumptions, surface ambiguity, and ask when the requirement is unclear.
2. **Simplicity First**: write the minimum code that solves the task; avoid speculative options, abstractions, and flexibility.
3. **Surgical Changes**: touch only files and lines required by the task; do not clean up unrelated code.
4. **Goal-Driven Execution**: define verifiable success criteria, add or run the focused checks, and loop until they pass or a blocker is explicit.

Attribution: coding-discipline content adapted under the MIT license from https://github.com/multica-ai/andrej-karpathy-skills, derived from Andrej Karpathy's observations at https://x.com/karpathy/status/2015883857489522876. Imported into UEAtelier 2026-05-23.

## Capability routing

For a normal "do X in Unreal" request, find before building:

1. Run `unreal.preview_change_plan` when the task may mutate state.
2. Run `unreal.knowledge_search` for relevant project knowledge.
3. Run `unreal.tool_recommend`.
4. Run `unreal.tool_gap_analyze` and treat its verdict as the decision:
   - `use_existing_tool`: call the existing tool.
   - `compose_existing_tools`: compose with `unreal.workflow_recommend` and probe with `unreal.workflow_run` using `dryRun=true` and `writeMemory=false`.
   - `scaffold_new_tool`: build a Python user tool through the self-extension gate below.

Most goals end with an existing tool or a composition. Do not jump straight to `execute_python` or tool scaffolding because a tool name seems plausible.

## Self-extension = Python user track only

AI-created tools use this path:

```text
unreal.scaffold_mcp_tool implementationTrack=python
-> unreal.mcp_user_registry_reload
-> unreal.mcp_user_tool_smoke
-> usable only when structuredContent.lifecycle.callableNow=true
```

Project-local Python tools live under:

```text
<ProjectDir>/Tools/UnrealMcpPyTools/<tool_id>/main.py
```

Read `structuredContent.lifecycle` after every scaffold, reload, smoke, or audit step. `toolListed`, `readyForApply`, file existence, and dry-run success are not enough. Write-capable user Python tools must default to `dryRun=true` and return `wouldWrite`; only set `dryRun=false` after the user explicitly requests the write and you state the intended mutation.

Core C++ apply/build/restart is manual, developer-only, hidden from the AI surface, and not the AI self-extension path. Do not call or route to `unreal.mcp_apply_scaffold` or `unreal.mcp_extension_pipeline`; do not use `unreal.execute_python` or `unreal.execute_python_file` to modify `Plugins/UnrealMcp/Source` or ToolRegistry JSON.

## Anti-patterns

- Treating a direct "make a tool" request as unsafe by itself. The request is valid; the safe implementation path is the Python user track.
- Hand-merging generated `.patch.cpp` content into `UnrealMcp*Tools.cpp`, `UnrealMcpToolRegistrar.cpp`, or ToolRegistry JSON.
- Claiming a tool is callable before reload, smoke, and `lifecycle.callableNow=true`.
- Using `execute_python` as a back door into plugin source, registry metadata, or core C++ promotion.
- Rebuilding a primitive when `tool_gap_analyze` says `use_existing_tool` or `compose_existing_tools`.
- Writing project-local generated tools, scaffolds, `Saved/UnrealMcp/*`, or skill drafts through shared repo walk-up paths.

## Cross-developer transfer

- Transfer reviewed user tools with `unreal.tools.export_package` and `unreal.tools.import_package`, not by pushing loose local drafts.
- `unreal.tools.export_package` reads the registry entry, scaffold or tool files, tests, and docs, then writes a SHA-256-verified zip under `Saved/UnrealMcp/Packages/<toolName>-<version>.zip`.
- Share the zip outside git; `Saved/UnrealMcp/Packages` is ignored on purpose.
- `unreal.tools.import_package` validates hashes and safe paths, then writes into the receiver's project-local tool/scaffold area.
- A real import requires the self-extension lock and rejects duplicate registry names; dry-run import first.
- After real import, reload the user registry, smoke the tool, and confirm `lifecycle.callableNow=true` before use.
