# Code Tools

The `code` tool category gives AI a safe, auditable way to read, search, preview,
apply, and roll back edits to **code files** inside an Unreal project and its user
extension plugins — `.cpp`, `.h`, `.Build.cs`, `.uplugin`, `.uproject`, `.py`,
`.json`, `.ini`, and similar. It is NOT an arbitrary filesystem remote and NOT an
in-editor IDE; it is a bounded edit capability with path policy, dry-run, backups,
manifests, and rollback.

Introduced in v0.29 across two waves: Wave A (read-only + path policy), Wave B
(preview/apply/rollback write closure).

## The closed loop

```
inspect -> search -> read -> preview change -> apply change -> build -> fix errors -> rollback if needed
```

| Stage | Tool | Risk |
|---|---|---|
| inspect workspace rules | `unreal.code_workspace_status` | read_only |
| list code files | `unreal.code_list_files` | read_only |
| read a file (+ sha) | `unreal.code_read_file` | read_only |
| search names/content | `unreal.code_search` | read_only |
| preview a structured edit | `unreal.code_preview_change` | low (no disk write) |
| apply a previewed edit | `unreal.code_apply_change` | high |
| roll back an applied edit | `unreal.code_rollback_change` | high |

Build is not a new tool: after `code_apply_change` returns `buildRecommended`,
drive `unreal.mcp_build_editor`, and on failure `unreal.mcp_compile_error_fix_plan`.

## Tools

### `code_workspace_status` (read_only)
Returns the live workspace rules: `projectDir`, `allowedReadRoots`,
`defaultWritableRoots`, `highRiskWritableRoots`, `forbiddenRoots`,
`allowedExtensions`, `latestCodeChangeManifest`, and the extension-lock status.
Call this first to learn what the current project permits.

### `code_list_files` (read_only)
Lists code files by `scope` (`project` / `source` / `plugins` / `user_tools` /
`python_tools`), with optional `extensions` / `glob` / `maxResults`. Always excludes
`Binaries/`, `Intermediate/`, `DerivedDataCache/`, `Saved/`, `Content/`, and plugin
binaries/intermediates.

### `code_read_file` (read_only)
Reads a file with optional `startLine` / `lineCount` / `maxChars`. Returns
`resolvedPath`, `projectRelativePath`, `lineCount`, **`sha256` of the whole file**,
and `text`. The whole-file sha is what a later `code_apply_change` checks against —
read a file, edit from that snapshot, and the sha guards against applying stale
context.

### `code_search` (read_only)
Searches file names or content. `mode` is `literal` / `regex` / `filename`, with
`scope` / `extensions` / `contextLines` / `maxMatches`. First version uses an
in-tree C++ scan (no external `rg`), with hard caps on files scanned, bytes per
file, and matches returned; results carry a truncation marker when capped. Regex
mode fails closed on a bad pattern.

### `code_preview_change` (low — never writes user files)
Takes structured `edits[]`, never a shell patch. Each edit is
`{path, expectedSha256, operation, oldText?, newText?, anchorText?}` where
`operation` is `replace_exact`, `insert_before`, `insert_after`, or `create_file`.
Matching is **byte-exact** (CRLF and BOM preserved, never normalized); a duplicate
`oldText` match rejects as `ambiguousMatch`, zero matches as `missingMatch`, and a
stale `expectedSha256` as `staleExpectedSha`. Returns `previewId`, `unifiedDiff`,
`touchedFiles`, `riskLevel`, `requiresApproval`, `wouldRequireBuild`,
`pathPolicyResult`, and per-file `expectedNewSha256`. The only thing it writes is the
preview artifact under `Saved/UnrealMcp/CodeChanges/Previews/<previewId>.json`.

### `code_apply_change` (high)
Applies a previewed change. `dryRun` defaults to **true**: a dry run re-validates
everything (preview-snapshot sha, current-disk sha, path policy) but writes nothing,
takes no lock, and creates no backup or manifest. A real run (`dryRun=false`) takes
the workspace lock, then for the transaction: generate an `editId` (fail-if-exists
backup dir), write the manifest at `applyState=started`, write+verify all backups,
mark `backupComplete`, then for each file re-check the current-disk sha immediately
before writing (TOCTOU guard) and write raw bytes, mark `writeComplete`, verify the
after-sha, mark `verified`. A multi-file edit is a transaction: if any write fails
mid-way, files already written are rolled back from backup. High-risk paths require
`confirmHighRisk=true`. Returns `editId`, `manifestPath`, `backupDirectory`,
`touchedFiles` (with before/after sha), `buildRecommended`, `restartRecommended`.

### `code_rollback_change` (high)
Rolls back an applied edit by `editId` or explicit `manifestPath`. `dryRun` defaults
true (returns the rollback diff + drift report). Before restoring, it checks each
target's current sha against the manifest's recorded after-sha; a mismatch is drift
and is rejected unless `force=true`. A `create_file` edit rolls back by deleting the
created file (only if its sha still matches). A real rollback writes a new rollback
manifest.

## Path policy

Classification runs on the **resolved canonical path** — collapse `..`, resolve
symlinks to their target, casefold on macOS APFS/HFS+, and Unicode-NFC normalize —
*then* classify. A symlink inside a writable root that points at a forbidden target
is forbidden by its target. The fall-through is **deny**.

- **Default-writable**: `Tools/UnrealMcpPyToolSamples/**`, `Tools/UnrealMcpPyTools/**`
- **High-risk (needs `confirmHighRisk`)**: `Source/**`, `Plugins/*/Source/**`,
  `*.uplugin`, `*.uproject`, `Config/*.ini`
- **Forbidden**: `Plugins/UnrealMcp/**` (the UEAtelier plugin itself),
  `Binaries/` `Intermediate/` `DerivedDataCache/` `Saved/` `Content/`, the Engine
  install directory, and `*.generated.h` / `*.gen.cpp` / `*.uasset` / `*.umap`.

`.uproject` edits that touch only `EngineAssociation` are redirected to
`unreal.project_version_migration`. `Saved/UnrealMcp/CodeChanges/` is the tools' own
manifest/backup output and is rejected as a user-code target.

## Manifests and backups

Independent of the self-extension manifest (`Saved/UnrealMcp/LastExtensionApply.json`).
Code Tools write to their own area:

```
Saved/UnrealMcp/CodeChanges/LastCodeChange.json
Saved/UnrealMcp/CodeChanges/Backups/<editId>/
Saved/UnrealMcp/CodeChanges/Previews/<previewId>.json
```

Manifests are written atomically (`<target>.tmp.<guid>` + rename). The `applyState`
field (`started` -> `backupComplete` -> `writeComplete` -> `verified`, or
`rolledBack`) makes a crash recoverable: a manifest stuck before `verified` is
restorable from its verified backups.

## Locking and approval

Real apply/rollback take the same extension session lock as `mcp_apply_scaffold`,
`mcp_build_editor`, `mcp_rollback_*`, `tools.import_package`, and
`skill_promote_draft`, so code edits never race those. The lock applies to all
callers (including external MCP clients) for concurrency safety; it does not span
into a build step. The approval gate is assistant-scoped: an autonomous-AI
`dryRun=false` write requires approval (high-risk paths give a stronger reason);
read-only tools, preview, and any dry run are allowed.

## Writable file types (first version)

Read+write: `.h .hpp .cpp .inl .ipp .Build.cs .Target.cs .uplugin .uproject .py
.json .ini`. Read-only (write deferred): `.md .cs .sh .ps1 .usf .ush .hlsl .yml
.yaml .toml`. Always forbidden: `*.generated.h`, `*.gen.cpp`, `*.uasset`, `*.umap`.

## Relation to self-extension

Code Tools are a general code-file editor. They do **not** replace the
self-extension scaffold pipeline (which creates Python user tools) and do **not**
edit core UEAtelier plugin source (`Plugins/UnrealMcp/**` is forbidden — core C++
tool development is a deliberate PM-directed hand-edit, see
`Tools/codex-prompt-header.md`). Their default target is the project's Python
user-tool tree and isolated user plugins; host `Source/**` is reachable only as a
confirmed high-risk write.
