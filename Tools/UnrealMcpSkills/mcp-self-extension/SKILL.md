# MCP Self Extension

Use this skill when extending the Unreal MCP plugin itself from Editor Chat or the external supervisor. The goal is to preserve the reviewable, reversible path from capability gap to compiled, tested MCP tool.

Reviewer/agent-side rules are in `Tools/codex-prompt-header.md` § Self-extension workflow.

## v0.26 tool tracks

- **Default path: user Python extension.** Project-local tools live under
  `<ProjectDir>/Tools/UnrealMcpPyTools/<tool_id>/` with a registry manifest and
  `main.py`. This path does not use `.patch.cpp`, does not edit plugin C++
  source, and does not require UBT or an editor restart.
- A freshly scaffolded user Python tool is not callable yet. The required gate
  is `unreal.scaffold_mcp_tool` -> `unreal.mcp_user_registry_reload` ->
  `unreal.mcp_user_tool_smoke`; only call the tool after
  `structuredContent.lifecycle.callableNow=true` and the smoke/audit result says
  it is available.
- **Core developer mode (requires approval):** built-in tools live in shared
  plugin source such as `Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpEditorTools.cpp`,
  `UnrealMcpBlueprintTools.cpp`, `UnrealMcpActorTools.cpp`, and related category
  files. They ship with the plugin and travel through normal git history.
- Core C++ scaffolds use descriptor-first `.patch.cpp` fragments under
  `<ProjectDir>/Tools/UnrealMcpToolScaffolds/<toolId>/`, then require explicit
  approval, dry-run apply, real apply, build, editor restart, audit/smoke, and
  tests before the tool can be claimed callable.
- Direct `unreal.execute_python` is a footgun for repeatable tool creation: it
  bypasses registry metadata, lifecycle state, reload/smoke, and dry-run policy.
  Use a user Python tool scaffold unless the user explicitly asked for one-off
  editor scripting.

## Scaffold location is project-local

- User Python tools and core C++ scaffold drafts are Category B local project
  artifacts and must be rooted at `FPaths::ProjectDir()`.
- If the editor runs `UEvolve.uproject`, `<ProjectDir>` is the repo root and
  user Python tools land at `<repoRoot>/Tools/UnrealMcpPyTools/`.
- If the editor runs
  `Examples/UEvolveExample57/UEvolveExample57.uproject`, `<ProjectDir>` is that
  example project and user Python tools land under
  `Examples/UEvolveExample57/Tools/UnrealMcpPyTools/`.
- Do not walk up to the repository root when writing user tools, scaffolds,
  `Saved/UnrealMcp/*`, or `SkillDrafts`.
- `UnrealMcpSharedPathResolver` is for Category A shared inputs such as skills,
  tests, and knowledge, not for project-local generated tools.
- Project-local tools let two editor sessions on different `.uproject` files
  iterate separately without colliding.

## Lifecycle state interpretation

Every tool that may change registry state can return
`structuredContent.lifecycle`. Read it before making availability claims.
`lifecycle.callableNow` is the primary callable signal; `toolListed`,
`readyForApply`, file existence, or a successful dry run are not enough.

| Lifecycle signal | Assistant action |
| --- | --- |
| `callableNow=true` and smoke/audit passed | The tool can be called. |
| `nextRequiredAction=unreal.mcp_user_registry_reload` | Run reload, then inspect lifecycle again. |
| `nextRequiredAction=unreal.mcp_user_tool_smoke` | Run smoke, then inspect lifecycle again. |
| `requiresApproval=true` or `approval_required` | Stop and surface the original approval request; do not retry. |
| `dryRun=true` result | Treat as a plan only, not proof that a write happened. |
| Core C++ state requires build/restart | Do not claim same-session availability; build, restart, audit/smoke, then test. |

## Canonical workflow

**Think first, act second**. The order below moves from understanding to
information gathering to gap decision to action; each step has a specific
existing tool. Scaffold creation is a first-class outcome when the gap
analyzer says so, not a fallback.

1. **Understand the requirement.** Restate the user's goal in your own
   words: acceptance criteria, target assets/source, risk class. If
   anything is unclear, ask before tool selection.
2. **Plan the change.** Run `unreal.preview_change_plan` to size the
   intended mutation and capture an explicit risk class. The preview is
   read-only and is the early gate that anchors everything that follows.
3. **Research existing knowledge.** Run `unreal.knowledge_search` on the
   task domain. If it reports a missing index, run
   `unreal.knowledge_index_refresh` and retry. Read the returned cards
   before deciding.
4. **Inventory capabilities and decide the gap.** Run
   `unreal.tool_recommend` and `unreal.tool_gap_analyze`. The gap analyzer
   returns one of `use_existing_tool`, `compose_existing_tools`, or
   `scaffold_new_tool`; treat that as the decision, not as advice.
5. **Self-extension audit (when the task touches the MCP plugin itself).**
   Run `unreal.mcp_tool_audit` and/or `unreal.mcp_workbench_status` to
   understand registry health, current scaffolds, and any pending
   manifests before mutating tool source.
6. **Compose existing tools when possible.** If `tool_gap_analyze` says
   `compose_existing_tools`, use `unreal.workflow_recommend` for the
   composition shape. If you want to probe a composition without
   side effects, call `unreal.workflow_run` with `dryRun=true` AND
   `writeMemory=false`; the default `writeMemory=true` writes
   `chat.active_task` even in dry-run, which is not what a probe wants.
7. **Scaffold a user Python tool when the gap analyzer says so.** Run
   `unreal.scaffold_mcp_tool` on the Python user-extension track. The output
   should be project-local under
   `<ProjectDir>/Tools/UnrealMcpPyTools/<tool_id>/`; it should not create or
   require `.patch.cpp` fragments.
8. **Reload the user registry.** Run `unreal.mcp_user_registry_reload`, then
   inspect `structuredContent.lifecycle`. If `callableNow` is still false,
   follow `nextRequiredAction` instead of calling the tool.
9. **Smoke the tool.** Run `unreal.mcp_user_tool_smoke`. Only after smoke
   passes and lifecycle reports `callableNow=true` may you call the new tool or
   tell the user it is available.
10. **Default user writes to dry run.** Write-capable user Python tools must
    default to `dryRun=true` and return `wouldWrite`. When the user explicitly
    requests the write, set `dryRun=false` and state the intended mutation
    before the call.
11. **Core developer mode (requires approval).** Use the C++ scaffold path only
    when the user or reviewer explicitly asks for a built-in plugin tool. The
    required gate is dry-run -> `unreal.mcp_apply_scaffold` real apply -> build
    -> editor restart -> audit/smoke -> generated/category tests. Never claim a
    core C++ tool is usable in the same editor session in which it was applied.
12. **Cross-engine warning.** Both user Python tools and core C++ tools must use
    APIs that work on UE 5.6 and UE 5.7. Any C++ engine-version shim belongs
    only in `UnrealMcpEngineCompat.h`; Python should prefer stable `unreal`
    module APIs and be smoke-tested in the target host.
13. **Finish and hand off.** Run `unreal.knowledge_eval_run` only when the
    change touched docs, ToolRegistry metadata, search scoring, or
    recommendation behavior. Finish with `unreal.verify_task_outcome`, then
    record restart handoff or next steps with `unreal.project_memory_write` or
    `unreal.project_memory_edit`.

## Cross-developer transfer

- Transfer reviewed user tools with `unreal.tools.export_package` and
  `unreal.tools.import_package`, not by pushing loose scaffold drafts.
- `unreal.tools.export_package` reads the scaffold, registry entry, tests, and
  docs, then writes a SHA-256-verified zip under
  `Saved/UnrealMcp/Packages/<toolName>-<version>.zip`.
- Share the zip outside git, such as by file transfer, email, or chat. The zip
  is gitignored on purpose.
- `unreal.tools.import_package` validates hashes and safe paths, then writes
  the scaffold into the receiver's project-local
  `<ProjectDir>/Tools/UnrealMcpToolScaffolds/`.
- A real import requires the self-extension lock and rejects duplicate registry
  names; always dry-run import first.
- After a real import, validate the registry, build, restart, audit, and run
  the relevant MCP test suite before treating the tool as available.

## Anti-patterns

- Do not hand-merge into main module: never paste `.patch.cpp` content directly
  into `UnrealMcp*Tools.cpp`, `UnrealMcpToolRegistrar.cpp`, or
  `Tools/UnrealMcpToolRegistry/tools.json` to add a tool.
- Hand-merging bypasses manifest creation, breaks rollback, confuses scaffold
  inspection, and can make future apply or import conflict.
- Do not re-apply promoted scaffolds. If `toolListed=true`, the running binary
  already has the tool; applying again risks duplicate registrations or a
  conflict detector failure.
- Do not route scaffold writes through walk-up shared-path resolution.
- Do not commit `Saved/UnrealMcp/*` artifacts such as manifests, packages,
  `ChatHistory.json`, project memory, or generated knowledge indexes.
- Do not force `unreal.mcp_rollback_to_manifest` until
  `dryRun=true` has been reviewed; rollback expects source hashes to match the
  backup snapshot.
- Do not assume a newly built C++ tool is dispatchable until the editor has
  restarted and `tools/list` includes it.

## Safety notes

- Keep generated launchers and `Saved/` artifacts out of git unless they are
  intentionally generic.
- Treat scaffold `dryRun=false` as a commit point: the next action should be
  build, test, restart, or rollback.
- Before high-risk work, create a snapshot with
  `unreal.mcp_backup_project_state`.
- If a build fails, run `unreal.mcp_classify_error`, then
  `unreal.mcp_compile_error_fix_plan` against the newest build log before
  patching.
- If integration becomes unsafe, prefer manifest rollback over ad-hoc manual
  source edits.
- For RAG-sensitive changes, run `unreal.knowledge_eval_run` before and after
  editing docs, ToolRegistry metadata, search scoring, or recommendation
  behavior.
- When context is long, write `chat.active_task` with
  `unreal.project_memory_write` or `unreal.project_memory_edit`, then continue
  in one bounded next step.
