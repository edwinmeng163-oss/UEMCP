---
name: cli-anything-ueatelier
description: Control a running UEAtelier-equipped Unreal Editor through CLI-Anything by proxying MCP JSON-RPC over HTTP.
---

# cli-anything-ueatelier

`cli-anything-ueatelier` is a command-line wrapper for UEAtelier, the Unreal
Editor MCP self-extension workbench. It connects to a running editor at the
local MCP endpoint, sends JSON-RPC `tools/list` and `tools/call` requests, and
unwraps responses for shell scripts, CLI-Anything, CI jobs, and command-mode
agents that cannot speak MCP directly.

The CLI is a client only. It does not start Unreal Editor, install the plugin,
or add new MCP tools.

## Prerequisites

Unreal Editor must already be running with the UEAtelier plugin loaded.

Default endpoint:

```bash
http://127.0.0.1:8765/mcp
```

Required state:

- Unreal Editor 5.6 or 5.7 is open.
- The `UnrealMcp` plugin is enabled.
- The project has finished loading.
- Localhost HTTP POST calls can reach the MCP endpoint.

Start each session with:

```bash
cli-anything-ueatelier status
```

If the editor is not reachable, the CLI exits `2` and reports
`endpoint_unreachable`.

## Installation

Install from the UEAtelier repository subdirectory:

```bash
pip install git+https://github.com/edwinmeng163-oss/UEAtelier.git#subdirectory=Tools/UEAtelierCli
```

Local editable install from a clone:

```bash
pip install -e Tools/UEAtelierCli
```

Verify:

```bash
cli-anything-ueatelier --version
```

Runtime dependencies are `click` and `requests`.

## Quick Start

Check connectivity:

```bash
cli-anything-ueatelier status
```

List verification tools:

```bash
cli-anything-ueatelier tools list --category verification
```

Describe one tool:

```bash
cli-anything-ueatelier tools describe unreal.pie_smoke
```

Run a generic tool:

```bash
cli-anything-ueatelier run unreal.editor_status --args-json '{}'
```

Read diagnostics as JSON:

```bash
cli-anything-ueatelier --json diagnostics --classes compile,log_error --limit 20
```

## Subcommand Reference

Global form:

```bash
cli-anything-ueatelier [GLOBAL] <command> [ARGS]
```

Global flags:

- `--json`: emit one machine-readable JSON document.
- `--endpoint URL`: override the endpoint for this command.
- `--http-timeout SEC`: HTTP timeout for every request. Default: `120`.
- `--version`: print the package version.

`status`: verify editor and plugin availability.

```bash
cli-anything-ueatelier status
cli-anything-ueatelier --json status
```

Human output includes endpoint, connected state, editor version, and tool count.
JSON output is normalized:

```json
{"endpoint":"http://127.0.0.1:8765/mcp","connected":true,"editorVersion":"5.7.4","toolCount":160}
```

`tools list`: list MCP tools, with optional category, search, and limit.

```bash
cli-anything-ueatelier tools list
cli-anything-ueatelier tools list --category verification
cli-anything-ueatelier tools list --search blueprint
cli-anything-ueatelier tools list --limit 20
cli-anything-ueatelier --json tools list --category task-atlas
```

Common categories: `actors`, `blueprint`, `editor`, `material`, `memory`,
`scaffold`, `self-extension`, `skills`, `task-atlas`, `verification`, `widget`.
JSON mode returns the `result.tools` array directly.

`tools describe NAME`: show one tool object and schema.

```bash
cli-anything-ueatelier tools describe unreal.automation_run
cli-anything-ueatelier tools describe automation_run
cli-anything-ueatelier --json tools describe unreal.editor_diagnostics
```

Short names are accepted. JSON mode returns the single tool object directly.

`run NAME`: call any MCP tool through `tools/call`.

```bash
cli-anything-ueatelier run unreal.editor_status --args-json '{}'
cli-anything-ueatelier run unreal.tail_log --args-file args.json
cli-anything-ueatelier run editor_status --args-json '{}'
cli-anything-ueatelier --json run unreal.project_settings_get --args-json '{"section":"Project"}'
```

Do not pass both `--args-json` and `--args-file`. Invalid JSON exits `2`.
JSON mode returns only `result.structuredContent`.

`diagnostics`: wrap `unreal.editor_diagnostics`.

```bash
cli-anything-ueatelier diagnostics
cli-anything-ueatelier diagnostics --since 2026-05-19T00:00:00Z
cli-anything-ueatelier diagnostics --classes compile,map_check,log_error
cli-anything-ueatelier diagnostics --limit 50
```

Useful classes: `compile`, `map_check`, `content`, `automation`,
`log_warning`, `log_error`.

`automation list`: wrap `unreal.automation_list`.

```bash
cli-anything-ueatelier automation list
cli-anything-ueatelier automation list --filter UnrealMcp
cli-anything-ueatelier automation list --details
cli-anything-ueatelier automation list --limit 25
```

Arguments sent: `filter`, `includeDetails`, `limit`.

`automation run NAME`: wrap `unreal.automation_run`.

```bash
cli-anything-ueatelier automation run Project.Functional Tests.MyTest
cli-anything-ueatelier automation run Project.Functional Tests.MyTest --tool-timeout 180
cli-anything-ueatelier automation run Project.Functional Tests.MyTest --tags cli,smoke
```

The command queues one exact test and returns a `runId`.
`--tool-timeout` maps to `timeoutSeconds` and is separate from
`--http-timeout`.

`automation report ID`: wrap `unreal.automation_report`.

```bash
cli-anything-ueatelier automation report RUN_ID
cli-anything-ueatelier automation report RUN_ID --wait
```

With `--wait`, the CLI polls every 2 seconds until `completed`, `failed`,
`timed_out`, or `stale`. The wait loop stops at the global `--http-timeout`
wall-clock budget.

`pie-smoke`: wrap `unreal.pie_smoke`, then poll `unreal.automation_report`.

```bash
cli-anything-ueatelier pie-smoke
cli-anything-ueatelier pie-smoke --map /Game/Maps/L_Test
cli-anything-ueatelier pie-smoke --tool-timeout 90
cli-anything-ueatelier pie-smoke --alive-window 5
```

The final output includes `pieReport` when the editor produces it.

`tasks list`: wrap `unreal.task_list`.

```bash
cli-anything-ueatelier tasks list
cli-anything-ueatelier tasks list --filter pinned
cli-anything-ueatelier tasks list --limit 10
```

Allowed filters: `all`, `success`, `failed`, `unrated`, `pinned`.

`tasks describe ID`: wrap `unreal.task_describe`.

```bash
cli-anything-ueatelier tasks describe TASK_ID
```

Use before rating or pinning a task.

`tasks rate ID RATING`: wrap `unreal.task_rate`.

```bash
cli-anything-ueatelier tasks rate TASK_ID success
```

Allowed ratings: `success`, `failed`, `unrated`.

`tasks pin ID`: wrap `unreal.task_pin`.

```bash
cli-anything-ueatelier tasks pin TASK_ID
cli-anything-ueatelier tasks pin TASK_ID --unpin
```

Pinned tasks are useful source material for workflow review.

## JSON Output Mode

Use `--json` when an agent or script will parse stdout.

Rules:

- The CLI prints one JSON document to stdout.
- It prints no banners or progress lines in JSON mode.
- It never prints raw JSON-RPC envelopes.
- It unwraps MCP responses for agent use.

Unwrapping:

- `status` returns a CLI-normalized object.
- `tools list` returns `result.tools`.
- `tools describe` returns one tool object.
- `run`, `diagnostics`, `automation`, `pie-smoke`, and `tasks` return
  `result.structuredContent`.

If a tool returns `isError: true`, the CLI prints the structured error body and
exits `3`.

Endpoint error:

```json
{"error":"endpoint_unreachable","endpoint":"http://127.0.0.1:8765/mcp","hint":"Launch Unreal Editor with the UEAtelier plugin first."}
```

Invalid arguments:

```json
{"error":"invalid_args","endpoint":"http://127.0.0.1:8765/mcp","hint":"Use either --args-json or --args-file, not both"}
```

Exit codes:

- `0`: success
- `2`: endpoint unreachable or invalid arguments
- `3`: tool error or wait timeout
- `4`: HTTP error

## Environment Variables

`UEATELIER_MCP_ENDPOINT` sets the default endpoint.

```bash
export UEATELIER_MCP_ENDPOINT=http://127.0.0.1:8765/mcp
cli-anything-ueatelier status
```

Endpoint precedence:

1. `--endpoint URL`
2. `UEATELIER_MCP_ENDPOINT`
3. `http://127.0.0.1:8765/mcp`

## Limitations

The CLI requires a running editor.

It does not launch Unreal Editor, install UEAtelier, add MCP tools, modify the
ToolRegistry, manage rollback manifests, or replace UEAtelier's native planning
and verification loop.

Not all 160 MCP tools have dedicated subcommands. Use `run` for the rest.

For write-capable tools, inspect the schema first:

```bash
cli-anything-ueatelier tools describe unreal.spawn_actor_basic
```

Then call with explicit JSON:

```bash
cli-anything-ueatelier run unreal.spawn_actor_basic --args-json '{"label":"CLI_Test","location":{"x":0,"y":0,"z":100}}'
```

For project-risky work, prefer UEAtelier preview, snapshot, diff, read-back,
and verification tools.

## Agent Guidance

Start every session:

```bash
cli-anything-ueatelier --json status
```

If unreachable, ask the user to launch Unreal Editor and enable the UEAtelier
plugin if needed. Do not retry in a tight loop.

Discover tools:

```bash
cli-anything-ueatelier --json tools list --search widget
cli-anything-ueatelier --json tools describe unreal.widget_dump_tree
```

Read-only call:

```bash
cli-anything-ueatelier --json run unreal.editor_status --args-json '{}'
```

Diagnostics:

```bash
cli-anything-ueatelier --json diagnostics --classes compile,log_error --limit 50
```

Automation:

```bash
cli-anything-ueatelier --json automation list --filter UnrealMcp --limit 20
cli-anything-ueatelier --json automation run Exact.Test.Name --tool-timeout 120 --tags cli
cli-anything-ueatelier --json automation report RUN_ID --wait
```

PIE smoke:

```bash
cli-anything-ueatelier --json pie-smoke --tool-timeout 90 --alive-window 5
```

Task Atlas:

```bash
cli-anything-ueatelier --json tasks list --filter pinned --limit 20
cli-anything-ueatelier --json tasks describe TASK_ID
cli-anything-ueatelier --json tasks rate TASK_ID success
cli-anything-ueatelier --json tasks pin TASK_ID
```

Generic workflow:

1. Run `status`.
2. Run `tools describe NAME`.
3. Build a JSON argument object.
4. Run `run NAME --args-json`.
5. Check exit code and structured output.

For high-risk edits, use the native UEAtelier MCP planning loop:
`unreal.preview_change_plan`, `unreal.capture_project_snapshot`, target write
tools, read-back inspection tools, `unreal.diff_project_snapshot`, and
`unreal.verify_task_outcome`.
