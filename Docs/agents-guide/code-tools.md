# Code Tools Agent Guide

How an AI agent should use the `code` tool category to edit code files safely.
Full reference: `Docs/CodeTools.md`.

## When To Use

Use Code Tools when the task is to read, search, or edit **code files**
(`.cpp`, `.h`, `.Build.cs`, `.uplugin`, `.uproject`, `.py`, `.json`, `.ini`) in the
project or its user extension plugins. Do NOT use them to:

- edit the UEAtelier plugin itself (`Plugins/UnrealMcp/**` is forbidden),
- create a new MCP tool (use the Python user-tool scaffold path instead),
- touch generated/binary files (`*.generated.h`, `*.uasset`, `*.umap`, `Binaries/`).

## Tools

Read-only: `code_workspace_status` (rules), `code_list_files` (scoped listing),
`code_read_file` (text + whole-file sha256), `code_search`
(literal/regex/filename). Write: `code_preview_change` (structured edits, no disk
write), `code_apply_change` (apply a preview), `code_rollback_change` (undo).

## Preferred Flow

1. `code_workspace_status` — learn the writable/forbidden roots for this project.
2. `code_read_file` — read the target; keep the returned `sha256`.
3. `code_preview_change` — describe the edit as structured `edits[]`
   (`replace_exact` / `insert_before` / `insert_after` / `create_file`) with the
   `expectedSha256` from step 2. Inspect the returned `unifiedDiff`, `riskLevel`,
   `requiresApproval`, `wouldRequireBuild`.
4. `code_apply_change` with `dryRun=true` — confirm the plan (no write yet).
5. `code_apply_change` with `dryRun=false` (+ `confirmHighRisk=true` if the path is
   high-risk) — applies, backs up, manifests.
6. If `buildRecommended`: `unreal.mcp_build_editor`; on failure,
   `unreal.mcp_compile_error_fix_plan`, then iterate.
7. If anything is wrong: `code_rollback_change` with the returned `editId`.

## Path Domains

Classification is on the resolved canonical path (symlinks resolved, `..`
collapsed, casefold + NFC), and the fall-through is deny.

- **Default-writable**: `Tools/UnrealMcpPyToolSamples/**`, `Tools/UnrealMcpPyTools/**`.
- **High-risk** (set `confirmHighRisk=true`): `Source/**`, `Plugins/*/Source/**`,
  `*.uplugin`, `*.uproject`, `Config/*.ini`.
- **Forbidden**: `Plugins/UnrealMcp/**`, `Binaries`/`Intermediate`/`Saved`/`Content`,
  Engine install, generated/asset files.

`.uproject` EngineAssociation-only edits: use `unreal.project_version_migration`.

## Safety You Can Rely On

- **Stale-context guard**: `expectedSha256` must match the current file; an edit
  built on an out-of-date read is rejected (`staleExpectedSha`).
- **Byte-exact**: CRLF and BOM are preserved; edits are matched and written as raw
  bytes, never text-normalized.
- **Dry-run first**: `code_apply_change` defaults to `dryRun=true` — it plans without
  writing, locking, or backing up.
- **Backup + rollback**: every real apply backs up originals and writes a manifest;
  `code_rollback_change` restores them (and refuses on drift unless `force=true`).
- **Transaction**: a multi-file apply rolls back already-written files if a later one
  fails.
- **Lock**: real apply/rollback hold the extension session lock, so they never race
  scaffold apply, builds, or rollbacks.

## Relation To Self-Extension

Code Tools are NOT the self-extension pipeline. Self-extension creates Python user
tools (`Docs/agents-guide/self-extension.md`); Code Tools edit existing code files.
They have separate manifests (`Saved/UnrealMcp/CodeChanges/` vs
`Saved/UnrealMcp/LastExtensionApply.json`) and separate purposes. Neither edits core
UEAtelier plugin source.
