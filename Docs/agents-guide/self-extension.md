# Self-Extension Agent Guide

Read this when work touches MCP tool creation, scaffold application, source
mutation, audit, rollback, the supervisor, or the Codex bridge. The canonical
deep reference remains [SelfExtensionPipeline](../SelfExtensionPipeline.md);
security rules remain in [SecurityModel](../SecurityModel.md).

## Composition First

Prefer composition before creating a tool:

1. Search or recommend existing tools with `unreal.tool_recommend`.
2. Search project knowledge with `unreal.knowledge_search`.
3. Try `unreal.workflow_recommend` plus `unreal.workflow_run`.
4. Scaffold a project-local Python user tool only when there is a real
   capability gap or the user explicitly asks for a callable tool.

## Main Tools

- `unreal.scaffold_mcp_tool`: create project-local Python user tools for the
  AI self-extension path. The advertised `implementationTrack` is `python`.
- `unreal.mcp_validate_tool_schema`: check OpenAI function-calling
  compatibility.
- `unreal.mcp_validate_cpp_patch`: static-check generated C++ patch fragments.
- `unreal.mcp_patch_scaffold_patch`: edit scaffold patch fragments with dry run
  and backup.
- `unreal.mcp_user_registry_reload`: reload project-local Python user tools.
- `unreal.mcp_user_tool_smoke`: smoke a user tool and confirm lifecycle.
- `unreal.mcp_apply_scaffold`: developer-only hidden core integration; manual
  source apply with dry run, backup, manifest, and idempotence checks.
- `unreal.mcp_build_editor`, `unreal.mcp_build_game`,
  `unreal.mcp_build_server`, `unreal.mcp_build_client`, and
  `unreal.mcp_build_packaged`: run UBT or BuildCookRun targets and parse logs.
- `unreal.mcp_run_tool_test` and `unreal.mcp_run_test_suite`: run generated MCP
  test requests.
- `unreal.mcp_rollback_last_extension` and
  `unreal.mcp_rollback_to_manifest`: restore apply backups.
- `unreal.mcp_extension_pipeline`: developer-only hidden core orchestration for
  preview, validation, dry run, apply, build, tests, snapshots, diff, and task
  outcome verification.
- `unreal.mcp_tool_audit` and `unreal.mcp_workbench_status`: inspect registry,
  handler, test, and workbench health.
- `unreal.knowledge_eval_run`: run local RAG regressions after changing docs,
  ToolRegistry metadata, search scoring, or recommendation behavior.

The same status and safety tools are exposed through `Window > UEAtelier
Workbench`. The Workbench is intentionally thin and should delegate through the
same backend path used by Chat, HTTP MCP, and tests.

## Preferred Flow

```text
tool_recommend
knowledge_search
preview_change_plan
scaffold_mcp_tool implementationTrack=python
mcp_user_registry_reload
mcp_user_tool_smoke
mcp_tool_audit
verify_task_outcome
```

Only claim the new tool is callable when reload/smoke report
`structuredContent.lifecycle.callableNow=true`.

The manual developer core-C++ flow remains available outside the AI surface:

```text
mcp_validate_tool_schema
mcp_validate_cpp_patch
mcp_apply_scaffold dryRun=true
mcp_apply_scaffold dryRun=false
mcp_build_editor
restart editor
mcp_run_test_suite
knowledge_eval_run if docs/RAG/recommendation changed
verify_task_outcome
```

`unreal.mcp_extension_pipeline` is hidden from AI-facing `tools/list` and
normally gates the manual developer core flow:

```text
preview_change_plan -> validate_schema -> generate_tests -> apply_dry_run -> capture_before_snapshot -> apply -> build -> test_suite -> capture_after_snapshot -> diff_project_snapshot -> verify_task_outcome
```

The gate can be relaxed with `enforceGate=false`, `captureSnapshots=false`, or
`verifyOutcome=false`, but manual core integration should keep all three
enabled.

## Python User Tool Path

AI-created tools live under the active project:

```text
<ProjectDir>/Tools/UnrealMcpPyTools/<tool_id>/main.py
```

The lifecycle gate is:

```text
scaffold_mcp_tool python -> mcp_user_registry_reload -> mcp_user_tool_smoke -> callableNow=true
```

Write-capable Python user tools must default to `dryRun=true` and return
`wouldWrite`. When the user explicitly requests the write, set `dryRun=false`
and state the intended mutation before calling the tool.

## Developer-Only Descriptor-first Tool Path

Core C++ scaffolds are manual/developer-only and hidden from the AI. They
produce the files needed for the registry-derived handler path:

- `ToolRegistrar.patch.cpp`: descriptor and fixed schema helper.
- `ToolRegistrarCall.patch.cpp`: call site for
  `RegisterAllMcpToolDescriptors`.
- `CategoryHandlerFunction.patch.cpp`: implementation stub for the selected
  category source file.
- `CategoryDispatcherBranch.patch.cpp`: dispatcher branch for the selected
  `TryExecute*Tool`.
- `ToolRegistryPatch.json`: reviewed policy metadata override candidate.
- `ScaffoldMetadata.json`: category, risk, source-file, and side-effect
  metadata.
- `ExtensionReport.md`: human-reviewable summary of gates, files, and risks.

Legacy ToolDefinition and ExecuteToolHandler fragments are generated only when
`includeLegacyCompatibility=true`. Manual core self-extension work should use
the descriptor-first path and should not put tool logic into
`UnrealMcpModule.cpp`.

## Path Resolution Domains

Runtime path resolution is split into four trust domains:

| Domain | What lives here | Resolver |
|---|---|---|
| Reader | Versioned `Tools/...` assets: PyTools, ToolRegistry source, scaffold recipes, skills, tests, knowledge | `ResolveToolsReadSubpath(subpath, sentinels)` in `UnrealMcpSharedPathResolver.h`; project-local first, then shared repo root, with `SourceKind` and `Candidates` surfaced in visible results |
| Writer | Generated drafts, session output, backups, manifests, logs, locks | Active `<ProjectDir>` and `<ProjectSavedDir>` paths such as `ResolveProjectOutputDirectory`; do not walk up |
| Plugin source | Plugin C++ source and plugin `Resources/` writes | `ResolvePluginSourceRoot()` / `ResolvePluginBaseDir()` from `IPluginManager::FindPlugin("UnrealMcp")->GetBaseDir()` |
| Saved | ActivityLog, ProjectMemory, KnowledgeIndex, ExtensionBackups, AutomationRuns | `FPaths::ProjectSavedDir()` / `Saved/UnrealMcp/` |

Manual core apply crosses three domains in one operation: read scaffold content
through Reader, write `.cpp` / `.h` source through Plugin source, and write
backups/manifests through Writer/Saved.

Scaffold location is project-local by design. `unreal.scaffold_mcp_tool` lands
under the active project:

```text
<ProjectDir>/Tools/UnrealMcpToolScaffolds/<toolId>/
```

For example-host sessions loaded through `AdditionalPluginDirectories`, the
active example project owns new drafts while reader tools can still fall back
to shared repo-root `Tools/` content.

## Rollback And Failure Flow

Real apply writes a manifest under:

```text
Saved/UnrealMcp/ExtensionBackups/<timestamp>_<toolId>/Manifest.json
```

When a pipeline step fails, `unreal.mcp_extension_pipeline` attaches
`failureAnalyses` with `unreal.mcp_classify_error` output and next-step
guidance. If source changes were already applied, it also attaches a dry-run
rollback plan using `unreal.mcp_rollback_to_manifest`.

If build fails:

1. Run `unreal.mcp_compile_error_fix_plan`.
2. Patch the relevant descriptor-first patch or source if the fix is
   deterministic.
3. Rebuild.
4. Roll back to manifest if unsafe or unclear.

If tests fail:

1. Inspect failed test output.
2. Compare expected vs actual structured content.
3. Patch handler or tests.
4. Rebuild/restart only if C++ changed.

If source state is inconsistent:

1. Run `unreal.mcp_diff_last_apply`.
2. Run `unreal.mcp_rollback_to_manifest` with `dryRun=true`.
3. Roll back with `force=true` only after manual review.

## Restart And Supervisor Recovery

Unreal Editor cannot keep executing while its own process is closed. For strict
self-extension, use the external supervisor:

```bash
python3 Tools/unreal_mcp_supervisor.py pipeline --auto-restart --args-json '{"toolName":"unreal.my_custom_tool","memoryKey":"mcp.extension.pipeline"}'
```

The supervisor can restart the editor and resume tests through project memory.
Generate local launchers with:

```bash
python3 Tools/unreal_mcp_supervisor.py install --platform all --output-dir Tools/UnrealMcpSupervisor --overwrite
```

Generated launchers are local and ignored. Versioned templates live under
`Tools/UnrealMcpSupervisorTemplates`.

PIE process crashes are not recoverable in-process. Use the same supervisor to
restart the editor and re-run verification after an editor crash.

## Tool Sharing

Reviewed tools move between projects with package export/import instead of
loose scaffold copies:

```text
/tool unreal.tools.export_package {"toolName":"unreal.my_tool","dryRun":true}
/tool unreal.tools.export_package {"toolName":"unreal.my_tool","version":"reviewed-1","dryRun":false}
/tool unreal.tools.import_package {"packagePath":"Saved/UnrealMcp/Packages/unreal.my_tool-reviewed-1.zip","dryRun":true}
/tool unreal.tools.import_package {"packagePath":"Saved/UnrealMcp/Packages/unreal.my_tool-reviewed-1.zip","dryRun":false}
```

Packages live under `Saved/UnrealMcp/Packages`, contain `manifest.json`,
`registry/tool.json`, and optional scaffold, tests, and docs entries. Real
import requires the self-extension lock and rejects duplicate ToolRegistry
names.

After real import, run `python3 Tools/validate_tool_registry.py`, build the
editor, restart, then run `unreal.mcp_tool_audit` and the relevant MCP test
suite.

## Codex App Server Bridge

`Tools/UnrealMcpCodexBridge` is a Bun daemon outside the UE plugin. It spawns a
fresh `codex app-server` subprocess, connects through Codex's App Server
protocol, and exposes a UE-facing WebSocket at:

```text
ws://127.0.0.1:8766/uevolve
```

macOS/Linux use a Unix socket outbound transport; Windows uses stdio outbound
transport because current Codex builds reject `--listen ws://...`. The
UE-facing inbound listener is always WebSocket.

Start and smoke test:

```bash
bun run --cwd Tools/UnrealMcpCodexBridge src/index.ts
bun run --cwd Tools/UnrealMcpCodexBridge test-client.ts
```

The bridge defaults to `UEVOLVE_CODEX_MODEL=gpt-5.5`,
`UEVOLVE_CODEX_EFFORT=xhigh`, and an approval policy that rejects command
execution, file changes, permission requests, MCP elicitations, dynamic tool
calls, and tool user-input requests. If the spawned app-server exits, health
becomes `failed`; V1 does not auto-restart it.
