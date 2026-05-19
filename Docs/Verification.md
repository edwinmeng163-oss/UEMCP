# Verification Tools

v0.20 C1a adds the first verification category tools for wrapping Unreal
Engine's Automation Test framework from MCP:

- `unreal.automation_list` discovers currently runnable automation tests.
- `unreal.automation_run` queues one exact test name and returns a run ID
  immediately.
- `unreal.automation_report` polls the run state and reads persisted historical
  reports.

These tools live in the `verification` ToolRegistry category. They are separate
from `self-extension` because they are a general editor verification surface,
not a source-apply pipeline helper.

## Async Lifecycle

Use `unreal.automation_list` first to find a canonical `fullName`. The
`fullName` is the runnable `FAutomationTestFramework` test command name; display
fields are included for humans.

Call `unreal.automation_run` with that exact `fullName`. The tool accepts only
one queued or running automation run at a time, writes an initial state file, and
returns:

```json
{
  "runId": "20260519T120000Z-a1b2c3",
  "acceptedAt": "2026-05-19T12:00:00Z",
  "initialStatus": "queued",
  "reportPath": "Saved/UnrealMcp/AutomationRuns/20260519T120000Z-a1b2c3.json"
}
```

The handler does not wait for completion. A ticker starts the UE automation test
on the game thread, advances latent/network automation commands, and writes the
final report once the framework reports completion.

Poll with:

```text
/tool unreal.automation_report {"runId":"20260519T120000Z-a1b2c3"}
```

Polling every 1-2 seconds is expected. Reports expose only the public fields in
the schema: status, test identity, result events, timing, a bounded log excerpt,
report path, and optional caller tags.

## State Files

State files are local runtime data under:

```text
Saved/UnrealMcp/AutomationRuns/<runId>.json
```

The JSON file is the canonical run record. It includes the public report data
plus internal-only fields such as `internalSchemaVersion` and
`timeoutSecondsConfigured`. Do not treat internal fields as part of the public
MCP contract.

`automation_run.tags` are caller metadata only. They are stored in the state file
and echoed by `automation_report`; they are not passed to UE Automation tag or
filter APIs.

## Status Values

Public report status values are:

- `queued`: accepted and waiting for the ticker to start the test.
- `running`: the UE automation framework has started the test.
- `completed`: the test stopped successfully.
- `failed`: the test stopped with assertion or log errors, or could not start.
- `timed_out`: the configured timeout has elapsed while the state file remains
  active.
- `stale`: the run exceeded `timeoutSeconds + 60` seconds and was marked stale
  so a later `automation_run` can accept a new run.

## v0.20 B1 Scope

B1 implements the minimal foundation: discovery, one active async run, polling,
state-file persistence, caller tags, bounded log excerpts, and stale recovery
after `timeoutSeconds + 60` seconds.

B1 intentionally does not implement release gates, Task Atlas dogfood replay,
RAG recommendation quality checks, heartbeats, multi-run scheduling, history
queries, cancellation, or a hardened watchdog. Those are deferred to v0.20 B2
or later verification chunks.
