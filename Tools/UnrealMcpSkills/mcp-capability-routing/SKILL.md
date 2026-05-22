# MCP Capability Routing

Read this skill FIRST when a user asks you to do something in Unreal, save a completed workflow, or "turn this into a tool". It routes the goal to existing MCP tools (find → compose → run), and when the work must be productized it takes the SAFE path: honor a direct tool request via the project-local Python user-extension track (scaffold → reload → smoke), offer a lighter skill when the user only said "save this", and never hand-merge a handler into core. See `mcp-self-extension` for build/lifecycle mechanics once this skill says "build".

## Why this skill exists

Most Unreal goals are already achievable by **composing the 162 existing tools** (actors / blueprint / editor / material / widget / verification / …). Two distinct moments trigger this skill — keep them separate:

1. **"Do X."** → Find and compose existing tools. Building is gated by `tool_gap_analyze`, not by intuition.
2. **"Make a tool / save this."** → Productize, but always via the SAFE build path. A direct request for a tool is legitimate — honor it; just never build it the dangerous way.

## Moment 1 — accomplish the goal efficiently (find before you build)

Do not guess tool names or reach for `execute_python`. Use the live discovery tools — they reflect the current registry, a static list in your head does not:

1. `unreal.preview_change_plan` — read-only; sizes the mutation and fixes a risk class. Anchors everything after.
2. `unreal.knowledge_search` — pull task-domain cards. If it reports a missing index, `unreal.knowledge_index_refresh` and retry.
3. `unreal.tool_recommend` — ask which existing tools fit the goal.
4. `unreal.tool_gap_analyze` — returns the **verdict**, treat it as the decision:
   - `use_existing_tool` → call it. Done. Do not build anything.
   - `compose_existing_tools` → `unreal.workflow_recommend` for the shape, then `unreal.workflow_run` (probe with `dryRun=true` AND `writeMemory=false`). Done. Do not build anything.
   - `scaffold_new_tool` → a primitive is genuinely missing. Go to Moment 2.

Most goals end at `use_existing_tool` or `compose_existing_tools`.

## Moment 2 — the user asks to make a tool, or to save the work

Honor what the user actually asked for, and always take the SAFE build path. The v0.26 incident (below) was **not** a wrong artifact choice — the user directly requested a tool, which was legitimate. The damage came entirely from the wrong *build path*. Route by what the user said:

1. **User asked for a tool** ("make this a tool", "I want a callable tool"), or `tool_gap_analyze` returned `scaffold_new_tool`. **Build the tool — via the Python user-extension track, project-local, never merged into core:**
   `unreal.scaffold_mcp_tool` (Python track) → `unreal.mcp_user_registry_reload` → `unreal.mcp_user_tool_smoke`; callable only when `lifecycle.callableNow=true`. Hand off to **`mcp-self-extension`** for the exact gate. This produces the same callable, registry-listed tool the user wanted — with no core edit, no UBT build, no editor restart, and none of the runtime-crash risk of an embedded handler.
   - If the tool would be a pure orchestration of existing tools, you MAY mention a skill is a lighter alternative (path 2) and offer it — but if the user wants a tool, give them a tool, safely.
2. **User only said "save this" / "make it reusable"** (did not specifically ask for a tool) **and the work was pure orchestration of existing tools.** Prefer a **skill** — instantly reusable, reviewable, reversible, no build:
   `unreal.skill_recording_start` → (re-)run the workflow → `unreal.skill_recording_stop` → `unreal.skill_distill_from_activity` → review the draft → `unreal.skill_promote_draft` with `dryRun=true` to preview, then `dryRun=false` to publish at `Tools/UnrealMcpSkills/<skillName>/SKILL.md` (`overwrite=true` only after reviewing conflicts).
3. **Core C++ tool (developer mode, requires approval).** Only when the user or reviewer explicitly asks for a built-in plugin tool. Heavy gate (dry-run → apply → build → restart → audit). Never the AI's default, even for an explicit tool request.

### Decision in one line

> User asked for a tool / a primitive is missing? → build it the SAFE way: **Python user-tool** track (scaffold → reload → smoke), never core-merge. User only said "save this" + pure orchestration? → a **skill** is lighter; offer it. Explicit built-in request + approval? → **core C++**.

## The v0.26 incident, framed correctly

The character was built successfully **using existing core tools** — composition worked. The user then **directly asked to turn it into a tool**, which was a legitimate, explicit instruction; complying was correct. The damage was entirely in the *build path*: the AI hand-authored a 291-line embedded-Python C++ handler and merged it into core, which crashed the editor on shutdown (`Py_FinalizeEx`) and silently mutated the core tool count. The safe path produces the **same callable tool**: the Python user-extension track (`scaffold_mcp_tool` Python → reload → smoke), project-local, no core edit, no build, no restart, no crash. The lesson is not "should have refused to make a tool" — it is **"make the tool the safe way: Python user-track, never a hand-written handler merged into core."**

## Anti-patterns

- Skipping `tool_gap_analyze` and going straight to `scaffold_mcp_tool` / `execute_python`. The gap verdict is the gate for Moment 1.
- Building a tool by hand-writing a handler and pasting it into `UnrealMcp*Tools.cpp` / `UnrealMcpToolRegistrar.cpp` / `tools.json` — this is the incident. The Python user-track gives the same tool safely (see `mcp-self-extension` § Anti-patterns).
- Refusing a direct, legitimate "make a tool" request because "a skill is lighter". Offer the skill, but honor the request via the safe track.
- Claiming a freshly built tool is callable before `lifecycle.callableNow=true` + smoke. See `mcp-self-extension` § Lifecycle state interpretation.

## Hand-off

- Build mechanics, lifecycle states, cross-engine rules, cross-developer transfer → **`mcp-self-extension`**.
- Reviewer/agent dispatch rules → `Tools/codex-prompt-header.md` § Self-extension workflow.
- Finish any task (compose or build) with `unreal.verify_task_outcome`; persist restart/next-step notes via `unreal.project_memory_write`.
