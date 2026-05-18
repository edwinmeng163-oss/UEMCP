# Build and Packaging Pitfalls

Consolidated traps discovered while shipping the Task Atlas v0.17 → v0.19.1 program. Read before authoring a new chunk, before dispatching Codex, or before producing a release zip. Each item lists the trap, how to detect it, and how to avoid or fix it.

---

## 1. Unity-build symbol collisions (C++)

### Trap
UBT's unity build concatenates multiple `.cpp` files into a single `Module.UnrealMcp.<N>.cpp` translation unit. Two common patterns silently collide once merged:

1. **Duplicate definitions across files.** File A defines `bool IsEditorPlaying()` at namespace `UnrealMcp::` scope. File B duplicates the same definition inside `UnrealMcp::<anonymous>::`. Each works in isolation; under unity build they conflict.
2. **Same-named per-file helpers in anonymous namespaces.** Many tool files declare local helpers like `GetTaskRoot`, `GetEditorAssetSubsystem`, `GetActivityLogRoot`, `MakeErrorObject`, `NormalizePathForJson`. Anonymous namespaces give internal linkage in their own TU, but unity merges multiple `.cpp` files into one TU — the anonymous namespaces collapse into one and the same-named helpers become redefinitions.

### Why this hides for so long
UBT's **adaptive unity build** automatically excludes files from the unity batch when they look risky (recently changed, frequently rebuilt, etc.). UE 5.7 example-host builds happened to exclude the problematic files by coincidence. UE 5.6 dev-host builds against `UEvolve.uproject` did not, and surfaced the issues immediately.

### Detection
- Compile errors of the form `error: redefinition of '<name>'`
- Compile errors of the form `error: call to '<name>' is ambiguous`
- Errors only appearing when building against `UEvolve.uproject` (UE 5.6 dev host) but not `UEvolveExample57.uproject` (UE 5.7 example host), or vice versa
- A list at the top of UBT output like `[Adaptive Build] Excluded from UnrealMcp unity file: ...` that includes the colliding files (indicating they would have collided otherwise)

### Avoidance for new C++

When authoring a new tool file, choose one of these patterns and stick with it for the whole file:

**Pattern A (recommended for cross-file helpers)** — define helpers at `UnrealMcp::` scope in exactly one file (typically `UnrealMcpCoreHelpers.cpp`), forward-declare them in every consumer at the same scope:

```cpp
// UnrealMcpCoreHelpers.cpp — single definition
namespace UnrealMcp
{
    bool IsEditorPlaying() { ... }
}

// UnrealMcpFooTools.cpp — consumer
namespace UnrealMcp
{
    bool IsEditorPlaying();   // forward decl, NOT inside anonymous namespace
    ...
}
```

**Pattern B (per-file helpers with unique names)** — keep helpers in `UnrealMcp::<anonymous>::` but give them names that cannot collide:

```cpp
// UnrealMcpFooTools.cpp
namespace UnrealMcp
{
    namespace
    {
        FString FooTools_GetCacheRoot() { ... }   // file-unique prefix
    }
}
```

**Do NOT** declare a forward-decl in one file's `UnrealMcp::<anonymous>::` and rely on another file's definition — names with internal linkage are not shared across files, only within the same TU. The unity build will make them appear shared, which is the trap.

### Fix when collisions surface
Two options, in order of preference:

1. Surgical: rename the duplicates so they don't collide. Cheap if only one or two cases.
2. Module-wide: add `bUseUnity = false;` to the module's `Build.cs`. One-line fix that eliminates the entire class of risk. Cost: ~30% slower full builds; incremental builds unchanged. This is what `Plugins/UnrealMcp/Source/UnrealMcp/UnrealMcp.Build.cs` does as of v0.19.1.

---

## 2. Verify both UE 5.6 dev host AND UE 5.7 example host

### Trap
Most release builds run against `Examples/UEvolveExample57/UEvolveExample57.uproject` (UE 5.7 example host). The local `UEvolve.uproject` (UE 5.6 dev host) is rarely tested in CI. Adaptive unity exclusion and engine-version differences can hide bugs from one but not the other.

### Why it matters
Plugin users will eventually run both UE 5.6 and UE 5.7. A "passing release" that only built against one engine is half-tested.

### Required pre-release check

```bash
# UE 5.7 example host (canonical target name is MyProjectEditor, NOT UEvolveExample57Editor)
rm -rf Plugins/UnrealMcp/Binaries Plugins/UnrealMcp/Intermediate
"/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" \
  MyProjectEditor Mac Development \
  -project="$(pwd)/Examples/UEvolveExample57/UEvolveExample57.uproject" \
  -WaitMutex

# UE 5.6 dev host (target name is UEvolveEditor — different from above!)
rm -rf Plugins/UnrealMcp/Binaries Plugins/UnrealMcp/Intermediate Binaries Intermediate
"/Users/Shared/Epic Games/UE_5.6/Engine/Build/BatchFiles/Mac/Build.sh" \
  UEvolveEditor Mac Development \
  -project="$(pwd)/UEvolve.uproject" \
  -WaitMutex
```

If either fails, the release is not ready.

---

## 3. Build target name traps

### Trap
The example project folder is `UEvolveExample57` but the internal UE module + target is `MyProject*`. A natural-sounding `UEvolveExample57Editor` does not exist.

### Why it matters
If Codex (or a human) types a non-existent target name, UBT will **author a stub `.Target.cs` to satisfy the command** rather than report the mismatch. The build "succeeds" but you now have a bogus file to clean up.

### Avoidance
Before embedding any build command in a Codex prompt or doc, verify the target exists:

```bash
ls Examples/UEvolveExample57/Source/*.Target.cs
ls Examples/UEvolveExample/Source/*.Target.cs
ls Source/*.Target.cs
```

Canonical targets as of v0.19.1:
- `Examples/UEvolveExample57/Source/MyProjectEditor.Target.cs` (UE 5.7 example host)
- `Examples/UEvolveExample/Source/MyProjectEditor.Target.cs` (UE 5.6 example host)
- `Source/UEvolveEditor.Target.cs` (UE 5.6 dev host)

---

## 4. Stale plugin dylib shadow

### Trap
When `Plugins/UnrealMcp/Binaries/` has a dylib from a previous dev-host build AND you then rebuild against an example host, UBT writes the fresh dylib to `Examples/UEvolveExampleNN/Binaries/Mac/` — but the editor mount path for the externally-loaded plugin keeps loading the OLD plugin-level binary. Smoke responses look exactly like pre-fix behavior; freshly added fields are absent.

### Avoidance
Before any example-host smoke run after a code change to the plugin:

```bash
rm -rf Plugins/UnrealMcp/Binaries Plugins/UnrealMcp/Intermediate
```

Then rebuild. This is in every build verify command in this document for that reason.

---

## 5. Cross-platform zip path separator handling

### Trap
PowerShell's `Compress-Archive` on Windows produces zip entries with `\` separators. The Python `zipfile` module on macOS/Linux treats these as literal characters in filenames, not directory separators — so a Win-produced zip looks empty to Mac tools and to `Tools/verify_package_integrity.py` running on Mac.

### Why this trap is dangerous
PM's instinct may be to "fix" the zip by repackaging it with forward slashes. **Do not.** The Win user already tested the original zip on Windows. The replacement was never verified on Win — you are shipping an untested artifact.

### Right fix
The verifier accepts both separators as of commit `8dd9086`. `safe_extract_zip` normalizes `\` to `/` per zip entry during extraction. Same-version Win and Mac zips now verify cleanly cross-platform without any zip rewrite.

### Rule
**The Win-tested artifact is the canonical Win release.** A verifier failure on PM's machine for a different OS is not a sufficient reason to swap it. Fix the verifier or fix the verification context, not the artifact.

---

## 6. Packaging script scaffold path

### Trap
Both `Tools/package_plugin.sh` and `Tools/package_plugin.ps1` originally hardcoded `Tools/UnrealMcpToolScaffolds/{fps_bootstrap,verify_input_drives_pawn}` as the staging source. That directory is the developer-local working copy and is **gitignored**. Clean checkouts (CI machines, first-time Win collaborators) never have it. The packager dies with "Missing directory" on its first run.

### Fix
As of commit `1fb5c4a`, both scripts use a `resolve_scaffold_source` / `Resolve-ScaffoldSource` helper that prefers the working copy if present, otherwise falls back to the canonical committed `Tools/UnrealMcpToolScaffoldStarters/<id>`.

### Avoidance for new scaffolds
When adding a new starter scaffold pair, commit it under `Tools/UnrealMcpToolScaffoldStarters/<id>/`. Never refer to `Tools/UnrealMcpToolScaffolds/<id>/` in scripts or docs as a required input — that directory is always optional.

---

## 7. Codex dispatch CLI hygiene

### Trap A — wrong subcommand
`codex-agent list` is not a valid subcommand. The CLI interprets unknown subcommands as the prompt text and spawns a new codex job with the literal string `list` as the entire prompt — using **default model gpt-5.4 and default effort high**, both of which violate the project rule (`gpt-5.5 xhigh`).

### Avoidance
- Correct subcommand to list jobs: `codex-agent jobs`
- All other subcommands listed in `codex-agent --help`
- Always include `-m gpt-5.5 -r xhigh` explicitly on `codex-agent start` even though the project rule says so — the default is wrong

### Trap B — `codex exec` is invisible in Codex Desktop
`codex exec ...` tags sessions with `source=exec`, which the macOS Codex Desktop app hides by default. Always use interactive `codex` with the prompt as argument or stdin:

```bash
codex --dir /Users/wmbt7052/Documents/Unreal\ Projects/MyProject "$(cat /tmp/my-prompt.md)"
```

`codex-agent start` already does this internally.

### Trap C — `--dir` must point at project root, not a worktree

Codex Desktop groups sessions by `cwd`. If `cwd` is a `.claude/worktrees/*` path, the session won't show under the project's entry. Always:

```bash
codex-agent start "..." -d "/Users/wmbt7052/Documents/Unreal Projects/MyProject"
```

---

## 8. Hermes coordinator hygiene

### Trap A — max-turns 30 is a hard ceiling
Hermes hit the 30-turn limit repeatedly during the Task Atlas program when given multi-part briefs. Symptom: Hermes runs prep work but never gets to the actual `codex-agent start` call.

### Avoidance
For multi-part programs, split into separate Hermes invocations — one per part — instead of one long brief. Each `hermes chat --resume <session_id>` gets its own 30-turn budget. v0.19's three deliverables (Make Tool / RAG indexing / LLM backfill) needed three separate Hermes calls.

### Trap B — context compaction loses the brief
When Hermes context approaches the model limit, the runtime compacts older messages. The initial PM brief can be partially truncated. Hermes had to recover the v0.17 frozen schema from disk-saved `~/.hermes/sessions/session_*.json` mid-flow.

### Avoidance
Always persist the per-part prompt to `/tmp/<descriptive>.md` and reference it by path in the Hermes brief. Even if the brief gets compacted, Hermes can `cat /tmp/<file>.md` to recover full context. Used successfully for `/tmp/v0.19_B_knowledge_ingestion.md`, etc.

### Trap C — codex-agent tmux jobs linger after completion
`codex-agent jobs` shows a codex job as `RUNNING` for ~5-10 minutes after the codex turn finishes (the tmux session lingers before the wrapper kills it). Do not block on `await-turn` waiting for status to flip — the underlying job is idle. Use `codex-agent capture <jobId>` to read the final report directly.

Run `codex-agent clean` periodically to drop orphaned sessions.

---

## 9. End-to-end verification at integration boundaries

### Trap
A write operation succeeds — file lands on disk, structured response says `success: true` — but the consumer downstream never sees it. v0.18's `[→ RAG]` button wrote `Saved/UnrealMcp/KnowledgeSources/TaskAtlas/<taskId>.md` and called `knowledge_index_refresh`, both reported success. But `knowledge_index_refresh` only scans `documents.jsonl` manifests, not `*.md` — the file was written but never indexed.

### Detection
Hermes caught this during v0.18 pre-dispatch review by reading the consumer side (`UnrealMcpKnowledgeTools.cpp` line ~1785) and seeing that the ingestion pattern only handled `documents.jsonl`.

### Avoidance
When wiring a new producer-consumer chain, **read the consumer's ingestion code**, not just its docs. Verify the producer's output shape and location match what the consumer expects. Smoke-test the end-to-end flow with a query that exercises the consumer after the producer writes — not just "did the write API return success".

---

## 10. Codex spec deviations — when to accept

### Trap
Codex may implement a request differently than the prompt specifies. v0.19 Part C's prompt said "use existing AnthropicMessagesProvider abstraction"; Codex instead used direct HTTP. PM accepted the deviation.

### Decision rule
Accept the deviation when:
- The pre-existing abstraction is ill-fitting for the new use case (provider abstraction is designed for streaming chat with tool loops; one-shot JSON classification doesn't need that machinery)
- The deviation's failure surface is smaller, not larger
- Codex's solution is well-scoped (one new file, clear boundaries)

Reject the deviation when:
- It silently changes the contract surface that other callers depend on
- It introduces a new dependency or pattern with no precedent
- It conflicts with a frozen schema or cross-version invariant

When in doubt, send Codex a targeted correction via `codex-agent send <jobId> "..."` rather than letting the deviation land.

---

## 11. Distinguish editor-load warnings from UBT errors

### Trap
Errors printed in the UE editor's Output Log during project load look like "build errors" but are content-layer issues — package version mismatches, missing external actor references, stale Blueprint references. These come from the asset system loading content, not from UBT compiling code.

### Detection rules
- If the error appears in UBT output between `[N/M] Compile` lines or in the final `Result: ...` summary → it's a real build error
- If the error appears only after UBT exits with `Result: Succeeded` and the editor launches → it's a content-layer warning, not a build error
- If the error contains `/Game/...` paths → it's a content-layer issue (Plugin source paths look like `Plugins/UnrealMcp/Source/...`)

### Rule
A successful UBT `Result: Succeeded` means the plugin built. Content warnings on top of that may need attention but are NOT build regressions. Pre-existing stock content warnings (Mannequin custom version, orphan external actor references in `Examples/UEvolveExample/Content/`) have shipped in every release v0.12 → v0.19.1 without affecting end users.

---

## Summary checklist (use at every release)

- [ ] `MyProjectEditor` target name confirmed for example hosts, `UEvolveEditor` for dev host
- [ ] `rm -rf Plugins/UnrealMcp/Binaries Plugins/UnrealMcp/Intermediate` before every smoke
- [ ] UE 5.6 dev host build PASS
- [ ] UE 5.7 example host build PASS
- [ ] All 4 validators PASS (`validate_tool_registry`, `check_ue56_compat`, mirror cmp, `verify_package_integrity`)
- [ ] ASCII scan on touched `.cpp` / `.h` files PASS
- [ ] Tool count matches in `AGENTS.md`, all `README.md` language sections, `Plugins/UnrealMcp/README.md`
- [ ] Codex prompts include `-m gpt-5.5 -r xhigh`; `-d` points at project root not worktree
- [ ] Hermes brief saved to `/tmp/*.md` if multi-step
- [ ] Don't repackage a Win-tested zip on Mac for "verifier passes" reasons
- [ ] Each PM commit's Co-Authored-By trailer names who actually wrote the code (Codex when generated, Claude when reviewer-authored)
