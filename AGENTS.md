# UEAtelier Agent Handoff

This is the first-read briefing for AI agents. It keeps only core identity,
read-order, freshness, tool-count, file-inventory, and safety rules. Deep
operational guidance lives in `Docs/agents-guide/`.

## Project Identity

UEAtelier is an Unreal Editor MCP self-extension workbench. The main
deliverable is `Plugins/UnrealMcp`. The plugin runs inside Unreal Editor,
exposes `http://127.0.0.1:8765/mcp`, adds `Window > UEAtelier Chat`, adds
`Window > UEAtelier Workbench`, and exposes Task Atlas from the Chat panel.

Current plugin metadata:

```text
Plugins/UnrealMcp/UnrealMcp.uplugin
FriendlyName: UEAtelier
VersionName: 0.27.0
EngineVersion: 5.6.0
Type: Editor plugin
Required plugin: PythonScriptPlugin
```

The plugin supports Unreal Engine 5.6 and 5.7 from the same source tree.
`UEvolve.uproject` is the local development host and defaults to
`EngineAssociation` `5.6`. Optional sample-content hosts are
`Examples/UEvolveExample` (UE 5.6.1) and `Examples/UEvolveExample57`
(UE 5.7.4).

Multi-engine discipline:

- All `#if ENGINE_*_VERSION` goes in `UnrealMcpEngineCompat.h`.
- Run `Tools/install_git_hooks.sh` once after clone.
- `EAiProviderKind` values are append-only; do not renumber.

## Product Goal

Target:

```text
An Unreal Editor MCP self-extension workbench that lets AI add new editor
automation capabilities under audit, dry run, backup, build, test, rollback,
RAG, and long-memory safeguards.
```

Core capabilities: call Unreal Editor tools from Chat or external MCP clients;
inspect maps, assets, actors, Blueprint graphs, Widget trees, logs, tests,
metadata, memory, skills, and knowledge cards; edit Blueprint, Widget, and
Actor state through fixed schemas; scaffold, validate, dry run, apply, build,
test, classify, and roll back new MCP tools; use RAG/tool recommendation before
creating tools; write project memory; distill repeated work into skills.

## Read Order For A New Agent

Start with `AGENTS.md`, `README.md`, `Plugins/UnrealMcp/README.md`,
`Docs/Architecture.md`, and `Docs/SecurityModel.md`.

Then read on demand:

| Task type | Read next |
|---|---|
| Architecture or module split | `Docs/Architecture.md` |
| RAG, knowledge search, recommendation | `Docs/KnowledgeRag.md`, `Tools/UnrealMcpKnowledge/README.md`, `Tools/UnrealMcpKnowledge/Evals/core_rag_eval.json` |
| Security or path safety | `Docs/SecurityModel.md` |
| Self-extension, scaffolds, audit, rollback, supervisor recovery | `Docs/agents-guide/self-extension.md`, `Docs/SelfExtensionPipeline.md`, `Tools/UnrealMcpSkills/mcp-self-extension/SKILL.md` |
| Task Atlas lifecycle, schemas, clustering, promotion | `Docs/agents-guide/task-atlas.md`, `Docs/TaskAtlas.md` |
| Automation tests, PIE smoke, editor diagnostics | `Docs/agents-guide/verification.md`, `Docs/Verification.md` |
| Build, release, packaging, install, deployment | `Docs/agents-guide/packaging.md`, `Docs/DeploymentTroubleshooting.md`, `Plugins/UnrealMcp/README.md`, `Tools/install_unrealmcp_to_project.py` |
| Windows compatibility or Win packaging | `Docs/agents-guide/packaging.md`, `Docs/WindowsCompatibilityLessons.md`, `Docs/WindowsPackaging.md`, `Docs/Stage2WindowsVerify.md`, `Tools/package_plugin.ps1` |
| Build hygiene or packaging-script pitfalls | `Docs/agents-guide/packaging.md`, `Docs/BuildAndPackagingPitfalls.md` |
| Tool changes | `Tools/UnrealMcpToolRegistry/tools.json`, `Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpToolRegistrar.cpp`, then the category source file |

## Documentation Freshness Rule

After every meaningful project change, update the AI-facing docs before
handoff:

1. Update `AGENTS.md` when project structure, tool-extension workflow, safety
   rules, build/test commands, RAG behavior, or current project status changes.
2. Update `README.md` when user-facing install, usage, feature coverage,
   deployment, or product positioning changes.
3. Update the focused doc under `Docs/` when the change belongs to a specific
   system such as architecture, self-extension, RAG, security, supervisor, or
   deployment troubleshooting.
4. If the change adds or changes tools, update ToolRegistry metadata, tests, and
   relevant docs in the same patch.

Treat stale docs as a product bug.

## Repository Map

Important versioned paths:

```text
README.md, AGENTS.md, UEvolve.uproject, open_uevolve.command
Docs/agents-guide/
Docs/Architecture.md, Docs/KnowledgeRag.md, Docs/SecurityModel.md
Docs/SelfExtensionPipeline.md, Docs/TaskAtlas.md, Docs/Verification.md
Docs/WindowsCompatibilityLessons.md, Docs/WindowsPackaging.md
Plugins/UnrealMcp/UnrealMcp.uplugin
Plugins/UnrealMcp/README.md
Plugins/UnrealMcp/Resources/
Plugins/UnrealMcp/Source/UnrealMcp/
Schemas/
Tools/UEAtelierCli/
Tools/UnrealMcpCodexBridge/, Tools/UnrealMcpKnowledge/
Tools/UnrealMcpSkills/, Tools/UnrealMcpTests/
Tools/UnrealMcpToolRegistry/, Tools/UnrealMcpToolScaffoldStarters/
Tools/UnrealMcpToolDocs/
Tools/UnrealMcpSupervisorTemplates/
Tools/extract_tool_schemas.py, Tools/generate_tool_docs.py
Tools/install_unrealmcp_to_project.py, Tools/validate_tool_registry.py
Tools/unreal_mcp_fetch_docs.py, Tools/unreal_mcp_stdio_proxy.py
Tools/unreal_mcp_supervisor.py
```

Important local-only or generated paths:

```text
Saved/UnrealMcp/, Content/, Tools/UnrealMcpToolScaffolds/
Tools/UnrealMcpSupervisor/, Binaries/, Intermediate/, DerivedDataCache/
Plugins/*/Binaries/, Plugins/*/Intermediate/
```

Do not commit local runtime state, fetched docs caches, KnowledgeIndex files,
API keys, generated launchers, local test content, or unreviewed scaffold
drafts unless explicitly asked.

## Current High-Level Feature Set

- Editor/inspection: status, engine version, settings, player input
  configuration, logs, maps, assets, selection, PIE, console, Python,
  Content Browser sync, save dirty packages, asset move, redirector fixup,
  dependency remap, and migration helpers.
- Actor, Blueprint, Widget, and Material tools: readback, guarded edits,
  creation, layout, graph/pin inspection, parameter inspection, and strict
  schema writes.
- Self-extension: schema/C++ patch validation, Python user-extension scaffolds,
  `unreal.mcp_user_registry_reload`, `unreal.mcp_user_tool_smoke`, patch
  editing, dry-run apply, backups, build matrix, tests, pipeline, audit,
  rollback, locks, error classification, supervisor install, and reviewed
  package export/import.
- RAG/recommendation, memory, skills, Task Atlas, and verification:
  knowledge index/search/eval, tool/workflow recommend, project memory CRUD,
  skill activity/drafts/promote, task extract/list/describe/rate/pin/promote,
  `unreal.automation_list`, `unreal.automation_run`,
  `unreal.automation_report`, `unreal.pie_smoke`,
  `unreal.verify_player_controls`, and `unreal.editor_diagnostics`.

## Tool Registry Status

The explicit ToolRegistry is central. Do not bypass it:
`Tools/UnrealMcpToolRegistry/tools.json`,
`Plugins/UnrealMcp/Resources/ToolRegistry/tools.json`,
`Tools/UnrealMcpToolRegistry/schema.json`, and
`Schemas/UnrealMcpToolRegistry.schema.json`.

At the time this file was written, the registry contained 164 entries across:
actors, blueprint, editor, material, memory, scaffold, self-extension, skills,
task-atlas, verification, and widget.

Recent v0.14-v0.24 project work includes Python runtime smoke,
readback inspectors, Blueprint refactor and macro/interface tools, UBT target
matrix, migration tools, install doctor, UMG/material parity tools, Task Atlas
foundation/backfill tools, verification foundation tools, diagnostics, PIE
smoke, AI provider presets, Kimi `reasoning_content` compatibility, enriched
input schemas, generated per-tool docs under `Tools/UnrealMcpToolDocs/`, and
the `Tools/UEAtelierCli/` CLI-Anything package.

Current project status: v0.27.1 adds core player input configuration and
existence-only player control verification; v0.27 walls core apply/pipeline off from the AI,
makes AI self-extension the Python user-tool track only, keeps core promotion
manual/developer-only and deferred, adds a `workflow_run` hidden-tool guard, and
merges the three project skills into `mcp-self-extension`; v0.26 completes
Reform C with centralized server-message provider system prompt assembly, six
baked safety rules, the AssistantRun approval gate, Python user-extension
default scaffolds, lifecycle-aware reload/smoke controls, and the 11-code audit
taxonomy; v0.25 rewrites Codex CLI exec; v0.24 adds AI provider presets, Kimi
`reasoning_content` compatibility, enriched input schemas, generated per-tool
docs under `Tools/UnrealMcpToolDocs/`, and progressive-disclosure agent docs;
v0.23 adds `cli-anything-ueatelier`; v0.22 adds `unreal.pie_smoke`; v0.21 adds
`unreal.editor_diagnostics`; v0.20 hardens async automation runs and watchdog
stale recovery; v0.19 completes Task Atlas Make Tool, To RAG ingestion, and
label backfill; v0.19.1 disables Unity build for the UnrealMcp module after a
UE 5.6 collision. `unreal.configure_fps_settings` and
`unreal.bp_add_input_axis_event_node` remain scaffold-only pending functional
verification.

Reform C v0.26 resolves the v0.25 self-extension incident by centralizing
server-message provider prompts in
`UnrealMcpAssistantSystemPromptBuilder`, baking in the six lifecycle/dry-run/
approval safety rules, enforcing the approval gate at the AssistantRun provider
seam, and making project-local Python user extensions the default path. v0.27
finishes the wall-off: AI self-extension is Python user tools only, while core
C++ apply/pipeline and promotion are hidden, manual, developer-only, and
deferred.

Visible tool counts can differ because hidden entries and aliases are filtered.
Trust `/tool unreal.mcp_workbench_status {}` and
`/tool unreal.mcp_tool_audit {}`. Validate registry changes with:

```bash
python3 Tools/validate_tool_registry.py
```

## C++ Architecture File Inventory

`UnrealMcpModule.cpp` is intentionally thin. Do not add tool logic there.

```text
Lifecycle/protocol: UnrealMcpModule.cpp, UnrealMcpProtocol.cpp
Tool metadata: UnrealMcpToolDefinitions.cpp, UnrealMcpToolDescriptor.h,
  UnrealMcpToolRegistrar.cpp/.h, UnrealMcpToolRegistry.cpp/.h,
  UnrealMcpToolHandlerRegistry.cpp/.h, UnrealMcpToolDispatcher.cpp
Execution: UnrealMcpToolExecutionGuard.cpp/.h, UnrealMcp*OutcomeVerifier.cpp,
  UnrealMcpSession.h, UnrealMcpActivityLog.h
Task/verification: UnrealMcpTaskAtlasTools.cpp/.h,
  UnrealMcpTaskLabelBackfillTool.cpp/.h, UnrealMcpAutomationTools.cpp/.h,
  UnrealMcpPieSmokeTools.cpp/.h, UnrealMcpDiagnosticsTools.cpp/.h
Knowledge: UnrealMcpKnowledgeBridge.h, UnrealMcpKnowledgeTools.cpp,
  UnrealMcpWorkflowTools.cpp
Category handlers: UnrealMcpEditorTools.cpp,
  UnrealMcpEditorEngineVersionTool.cpp, UnrealMcpActorTools.cpp,
  UnrealMcpBlueprintTools.cpp, UnrealMcpWidgetTools.cpp,
  UnrealMcpScaffoldTools.cpp, UnrealMcpSelfExtension*.cpp,
  UnrealMcpMemoryTools.cpp, UnrealMcpSkillTools.cpp
UI/assistant/tests: UnrealMcpChatPanel.cpp/.h,
  UnrealMcpWorkbenchPanel.cpp/.h, STaskAtlasWindow.cpp/.h,
  UnrealMcpEditorTabs.cpp, UnrealMcpAssistantRun.cpp/.h, Private/Tests/*.cpp
```

Prefer cautious single-category edits. The largest files remain ChatPanel,
ActorTools, KnowledgeTools, ScaffoldTools, BlueprintTools, WidgetTools,
ToolDefinitions, AssistantRun, and several SelfExtension helpers.

## Tool-count Discipline

The registry tool count must stay synced across the canonical registry, plugin
mirror, the `"the registry contained N entries"` line in this AGENTS.md, the
`N registered MCP tools` line in `README.md` (EN + Chinese + Japanese), and
the tool lists in `Plugins/UnrealMcp/README.md`.

```bash
python3 -c 'import json; print(len(json.load(open("Tools/UnrealMcpToolRegistry/tools.json"))["tools"]))'
grep -nE "registry contained|registered MCP tools" AGENTS.md README.md Docs/agents-guide/*.md
```

Before any commit that adds or removes a tool, bump all tool-count references
in the same commit.

## Safety Rules For Future AI

Always: run `git status --short` before editing; respect existing uncommitted
changes; prefer `rg`/`rg --files`; use `apply_patch` for manual edits; keep
generated local data out of Git; use fixed schemas; ensure write tools have
policy, preflight, and postcheck metadata; dry run, backup, manifest, build,
test, and rollback source mutations; use `unreal.preview_change_plan` or
document the plan for high-risk tasks; write/read project memory for long work;
run `unreal.knowledge_eval_run` if RAG/tool recommendation changes; run
`python3 Tools/validate_tool_registry.py` if ToolRegistry changes; update
`AGENTS.md`, `README.md`, and focused docs after meaningful changes.

Avoid: putting new tools directly into `UnrealMcpModule.cpp`; committing
`Saved/`, generated KnowledgeIndex, local fetched docs, API keys, local
supervisor launchers, or unreviewed scaffolds; assuming `Content/` is clean
distributable plugin state; installing over both engine-level and project-level
plugin copies; assuming newly built C++ tools are visible until Editor restarts;
treating ChatHistory as canonical product docs.

### Coding discipline (Karpathy guidelines)

Full text + attribution now lives in the merged
`Tools/UnrealMcpSkills/mcp-self-extension/SKILL.md` playbook (MIT, from Andrej
Karpathy's observations on LLM coding pitfalls). These are the behavioral
counter to the v0.26 incident class (a 291-line embedded-Python handler =
"Simplicity First" violation; hand-merging into core = "Surgical Changes"
violation):

1. **Think Before Coding** — state assumptions; surface tradeoffs and multiple
   interpretations; push back when a simpler path exists; stop and ask when confused.
2. **Simplicity First** — minimum code that solves the problem; no speculative
   features/abstractions/flexibility; if 200 lines could be 50, rewrite.
3. **Surgical Changes** — touch only what the task requires; don't "improve"
   adjacent code/comments/formatting; mention unrelated dead code, don't delete it;
   only remove orphans your own change created. Every changed line traces to the request.
4. **Goal-Driven Execution** — turn tasks into verifiable success criteria
   (tests-first, dual-engine build + automation green) and loop until verified.

## Current Local Caveat

Re-check current state with `git status --short`, `git branch --show-current`,
and `python3 Tools/validate_tool_registry.py`. If the worktree is dirty,
inspect diffs before editing.
