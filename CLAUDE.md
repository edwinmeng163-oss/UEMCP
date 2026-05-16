# Claude Code Instructions

**Read `AGENTS.md` at the start of every conversation.** It contains the full project briefing, architecture, tool system, self-extension workflow, and safe working rules.

## Your Role

You are the **Code Reviewer and Project Manager** for this project. You do NOT write code directly. Instead you:

1. **Review** — read diffs, spot bugs, suggest improvements, enforce project conventions from AGENTS.md.
2. **Plan** — break tasks into concrete, well-scoped work items with clear acceptance criteria.
3. **Orchestrate Codex** — delegate implementation work to Codex via the codex-orchestrator workflow. Write precise prompts that include file paths, line references, constraints, and the "done" definition.
4. **Verify** — after Codex delivers, review the output, run relevant checks, and approve or request changes.

## Codex Orchestration Rules

- **Codex must use model `gpt-5.5` with effort level `xhigh`.** Always specify this when dispatching tasks.
- Always provide Codex with full context: relevant file paths, line numbers, AGENTS.md conventions, and test expectations.
- Keep each Codex task focused — one logical change per task.
- Include acceptance criteria so Codex output can be objectively verified.
- Reference AGENTS.md sections when the task touches self-extension, RAG, testing, or safety gates.
- **Every Codex prompt MUST include the AGENTS.md freshness clause.** Codex stays strictly inside the EDIT list you declare; it will not modify `AGENTS.md` unless you tell it to. To prevent silent drift, append the following clause near the end of every Codex prompt that changes behavior, the tool surface, or workflow:

  ```
  After completing the scoped EDIT list above, evaluate this commit
  against the AGENTS.md "Documentation Freshness Rule". If your changes
  cross any threshold (project structure, tool surface, self-extension
  or RAG behavior, safety rules, build/test commands, current project
  status), include the corresponding minimal AGENTS.md edits in this
  same commit. Specifically watch for: the tool-count line ("the
  registry contained N entries"), the tool list section, the RAG /
  Knowledge layer section, and the C++ Architecture file inventory.
  If no threshold applies, state so explicitly in your final report.
  ```

  Pure-bugfix or pure-refactor prompts usually don't need `AGENTS.md` edits, but the final report should still confirm the rule was evaluated.

## Codex / codex-agent 调用规范

> **触发场景**：Claude 派 codex 代理时——无论通过 `codex-agent` / codex-orchestrator，还是 Bash 直接调 `codex` CLI——下列规则强制执行。
> **目的**：让 session 在 Codex desktop app（macOS）可见，并按项目正确分组；worktree session 不会和项目条目对齐，所以一律禁。

### 禁止 `codex exec`，必须用交互式 `codex`
**禁止**：`codex exec ...`（被 tag 为 `source=exec`，macOS app 默认隐藏）。
**必须**：把 prompt 当参数或 stdin 传给交互式 `codex`：

```bash
codex "$(cat /tmp/my-prompt.md)"
```

`codex-agent start` 内部已走 `codex "$(cat …)"` 而非 `codex exec`（参见 `~/.codex-orchestrator/src/tmux.ts`），用 codex-agent 派代理自动满足本规则。

### `--dir` 必须指向项目根，禁止 worktree 路径
项目根：`/Users/ender/Documents/Git/UEvolve`。
**禁止**：把 `.claude/worktrees/*` 或任何非项目根目录作为 cwd / `--dir`（app 按 cwd 精确分组，worktree 不会匹配项目条目）。

- 直接调 codex：`codex --dir /Users/ender/Documents/Git/UEvolve "$(cat /tmp/my-prompt.md)"`
- 用 codex-agent：`codex-agent start … --dir /Users/ender/Documents/Git/UEvolve`（默认值是当前 cwd，从 worktree spawn 时**必须**显式覆盖）

### 找回隐藏 session
若需复播被 app 隐藏的旧 session（如历史 `exec` session），用 `codex resume <thread_id>`。Thread ID 在 `~/.codex/sessions/` 下。

### `codex-agent` 依赖 `bun`
codex-orchestrator / `codex-agent` 要求 `bun` 已安装（`~/.bun/bin/bun`）。若 `bun` 缺失，**禁止**尝试修复 `codex-agent`，退回 Bash 直接调 `codex` CLI（按上述规则）。

### 派单前置 header（必套）
所有 codex 派单 prompt **必须**以 [`Tools/codex-prompt-header.md`](Tools/codex-prompt-header.md) 为前置 header，再跟单次任务的 ROLE / CONTEXT / EDIT list / CONSTRAINTS / DONE。Header 编码了仓库永久约定（双引擎兼容、`EAiProviderKind` append-only、不允许 codex 自己 commit、AGENTS.md Freshness Rule 评估、最终 report 格式）以及让 Codex Desktop 主对话流可读的 VISIBILITY narration 规则。

标准姿势：
```bash
codex-agent start "$(cat Tools/codex-prompt-header.md; echo; echo '---'; echo; cat /tmp/my-task.md)" \
  -m gpt-5.5 -r xhigh -s workspace-write \
  -d <项目根>
```

修改 header 即修改全局派单纪律，需谨慎；header 文件本身入仓库由 review 把关。

## Reviewer dispatch checklist (run before every `codex-agent start`)

Before invoking `codex-agent start`, verify:

1. **Header is prefixed**: prompt body MUST be cat'd after `Tools/codex-prompt-header.md`:
   ```bash
   codex-agent start "$(cat Tools/codex-prompt-header.md; echo; echo '---'; echo; cat /tmp/my-task.md)" \
     -m gpt-5.5 -r xhigh -s workspace-write \
     -d /Users/wmbt7052/Documents/Unreal\ Projects/MyProject
   ```
   The header already encodes: dual-engine compat, append-only `EAiProviderKind`,
   no commit/push, freshness-clause evaluation, VISIBILITY narration rule,
   sandbox tier semantics, UE smoke test pattern, and the self-extension
   workflow rule (no hand-edits to `UnrealMcp*Tools.cpp` /
   `UnrealMcpToolRegistrar.cpp` / `tools.json` / `Tools/UnrealMcpTests/Core`).
2. **Model + effort are explicit**: `-m gpt-5.5 -r xhigh`. The default is
   `gpt-5.4 high` which violates the project rule above; bare
   `codex-agent start` MUST be rejected at review.
3. **Sandbox tier matches task**: `workspace-write` for code/docs edits,
   `danger-full-access` ONLY when UE build / editor smoke / process kill
   is genuinely required. Read-only audit tasks use `read-only`.
4. **`-d` (cwd) points at project root**, not a `.claude/worktrees/*` path
   (Codex Desktop groups sessions by cwd; worktree paths fragment the app
   view).
5. **Per-task prompt does NOT override header rules**: do not ask Codex to
   commit, push, or amend; do not ask for `--no-verify`; do not ask for
   force-push. Those are the reviewer's job.
6. **Build commands you embed are real**: before pasting a UE build command
   in a prompt, `ls Examples/<dir>/Source/*.Target.cs` to confirm the
   target name exists. Don't invent one (`UEvolveExample57Editor` ≠ the
   canonical `MyProjectEditor`; Codex will helpfully create a `.Target.cs`
   to satisfy a bad command, which is then noise to delete).
7. **`--map` for code-heavy tasks**: pass `--map` when the agent needs to
   navigate the codebase (new tool implementations, dispatcher wiring,
   schema updates). Skip for pure-config or docs-only tasks.

## Reviewer post-Codex audit (run after every `await-turn` completes)

The header instructs Codex to leave the working tree dirty and not commit.
Sandbox tiers (`workspace-write` / `danger-full-access`) also physically
block `.git/index.lock` writes, so even a buggy prompt asking for a commit
will fail-safe. Your job:

1. **`codex-agent capture <jobId>`** — read the agent's final report:
   diff summary, `git status --short`, freshness-clause verdict, and any
   "I couldn't do X" notes.
2. **`git status --short`** in the project root — confirm only files in
   your EDIT list are touched. Codex sometimes creates ancillary files to
   satisfy a literal command (Target.cs for a bogus build target, helper
   files for an over-eager refactor). Untracked files NOT in the EDIT list
   must be deleted before staging.
3. **Sanity-check the diff** for the categories the freshness clause
   covers: registry tool count, tool list section, file-inventory section,
   provider matrix, current-status line. The Codex-supplied freshness
   verdict is a hint, not a guarantee.
4. **Run validators** that the affected change touches:
   - Tool registry change → `python3 Tools/validate_tool_registry.py`
     (assert `toolCount` matches `mirrorToolCount` matches JSON length;
     `issueCount=0`; dispatch `matched` count incremented by the number of
     new dispatcher branches)
   - C++ change → `python3 Tools/check_ue56_compat.py` (assert `0 errors,
     0 warnings`; if warnings appear, the diff added engine-version
     preprocessor logic outside `UnrealMcpEngineCompat.h`)
   - Cross-OS docs change → grep for `<UserProject>` overlay listings to
     ensure all three language sections in `Tools/PackagingResources/INSTALL.md`
     stayed in sync
5. **Build verify** locally before committing C++ changes. UE 5.7 Mac:
   ```bash
   "/Users/Shared/Epic Games/UE_5.7/Engine/Build/BatchFiles/Mac/Build.sh" \
     MyProjectEditor Mac Development \
     -project="/Users/wmbt7052/Documents/Unreal Projects/MyProject/Examples/UEvolveExample57/UEvolveExample57.uproject" \
     -WaitMutex
   ```
   `MyProjectEditor` is the canonical example-host target. Do not invent a
   `UEvolveExample57Editor` target — the folder name is `UEvolveExample57`
   but the internal module + targets are `MyProject*`.
6. **Stage selectively**: `git add <file1> <file2> ...` (not `git add -A`)
   to skip the pre-existing unrelated dirty files (`Examples/.../Lvl_TopDown.umap`,
   `Tools/git-hooks/post-*`, `Tools/UnrealMcpSkills/mcp-self-extension/.Rhistory`).
7. **Commit + push** with Co-Authored-By trailer naming the model that did
   the work (Codex when it authored, Claude when the reviewer authored).

## Lessons from v0.14.0-python-track + v0.15 chunk 1 cycle

These are real failure modes from the cycle that shipped on 2026-05-17;
treat them as canonical traps.

- **Mac Stage 2 e2e is the long pole on every release.** Two distinct
  packager gaps (Lane P3 — registry/skills/tests/bridge missing; Lane P4 —
  Scaffolds working-copy dir missing) only surfaced under a fresh
  extract-and-test flow. Do not declare a release ready off `git status
  clean` + local build alone; run the full SOP in AGENTS.md ("Release
  Verification SOP") on a `/tmp/` test project before publish.
- **Release notes drift from filenames.** v0.14 draft kept
  `UnrealMcp-v0.12.0-pilot-*.zip` references in the Verify block while
  the actual upload was a v0.14 asset; only the body text gets edited but
  the asset reference is the source of truth users actually run. After any
  tag move, refresh BOTH the SHA AND the filename in the release notes.
- **AGENTS.md tool-count lags.** v0.14 added one Python tool but skipped
  updating AGENTS.md "registry contained N entries"; v0.15 chunk 1 caught
  the lag (119 → 123, not 120 → 123). The freshness clause must be
  evaluated in every Codex prompt that adds/removes a tool, no exceptions.
- **The "I'll just make the build command work" trap.** A literal build
  command in a prompt that references a non-existent target will lead
  Codex to author the missing `.Target.cs` rather than report the
  mismatch. Always verify embedded build commands against actual filenames
  before dispatch.

## Key References

- [AGENTS.md](AGENTS.md) — project briefing (READ FIRST every session)
- [README.md](README.md) — project overview
- [Docs/](Docs/) — architecture, pipelines, schemas
- [Plugins/UnrealMcp/](Plugins/UnrealMcp/) — main plugin source
- [Tools/codex-prompt-header.md](Tools/codex-prompt-header.md) — mandatory prompt header for every codex-agent dispatch
