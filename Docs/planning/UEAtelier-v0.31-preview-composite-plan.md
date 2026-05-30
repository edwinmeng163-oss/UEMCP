# UEAtelier v0.31 阶段2 — Preview Composite Generator (R1-locked plan)

**Status**: R1 schema-locked 2026-05-31. Reframed from "composite replay" to
**reviewable workflow draft / preview composite generator** (PM + Hermes R0/R1
+ internal 2nd-opinion all concur). NOT a one-click replay.

## Why reframe (the core finding)

Captured tool arguments cannot be faithfully "replayed". A composite re-runs
each step through v0.30 `call_tool`, which is fail-closed: high-risk / write
tools are force-`dryRun=true` or denied (`UnrealMcpCallToolLibrary.cpp:149-163`,
`UnrealMcpCallToolPolicy.cpp:46-63`). If captured args carried `dryRun=false`
(a real write), replay silently becomes a dry-run — calling it "replay" misleads
users precisely on the write tools they care about most.

So the deliverable is: turn historical activity into a **reviewable, previewable
(read-only/dryRun), documentable composite draft**, with honest per-step
`policyDecision` labels.

## Two P0 risks driving the design

1. **Privacy leak** — full args (abs paths, code, prompts, asset names) must NOT
   land in the general ActivityLog JSONL, which is indexed into RAG
   (`UnrealMcpKnowledgeTools.cpp` `AddActivityLogCards`).
2. **Misleading replay** — users must never think a dry-run/denied step was a
   real write.

## Locked schema (R1)

### Capture metadata (public, in ActivityLog payload)
ActivityLog record gains writer-time `eventId` (ULID/GUID + sessionId; NOT
ts/line-based). tool_call payload stores ONLY public metadata:
`argumentKeys`, `captureStatus`, `captureRef?`, `captureSha256?`,
`captureSchemaVersion`, `redactionSummaryPublic`. (Existing `argumentKeys` kept.)

### Captured args (private store, NOT in ActivityLog, NOT in RAG)
`Saved/UnrealMcp/CapturedToolArgs/<sessionId>/<eventId>.json`:
`{eventId, sessionId, tool, timestampUtc, sanitizedArguments, redactionSummary,
originalSize, storedSize, toolPolicyAtCapture}`.

### Redaction (applied before storing)
- recursive field denylist: `(token|api[_-]?key|password|passwd|secret|credential|authorization|cookie)`
- home/project absolute paths → placeholder
- per-tool + arg-path denylist (e.g. execute_python code body, code_apply_change file content) → `captureStatus=skipped`
- per-value and total size cap; oversize value → marker (not truncated-and-stored)
- `captureSha256` for integrity (detect tampering)

### Task Atlas schema v2
Ordered `stepRefs[]` (each: `ordinal, eventId, sessionId, tool, ts, isError,
captureStatus, captureRef?, policyClassAtCapture`). `criticalPath` kept as
summary only. New: `stepRefTotal, stepRefsTruncated, replayEligibility,
replayUnavailableReason`. (criticalPath is `AddUnique`-deduped
`UnrealMcpTaskAtlasTools.cpp:443` → cannot be the replay sequence.)

### Generated composite
`compositeKind=preview|skeleton`,
`replayStatus=preview_only|skeleton_pre_capture|partial|blocked`.
main.py uses captured args ONLY as JSON data constants / private refs — NEVER
spliced into source/comments (injection guard). Each step returns
`policyDecision / effectiveArgsDiff / isError`.

## Design constraints (R1 caught these)

- consent/retention: capture default on/off? retention period? cleanup with ActivityLog.
- integrity: `captureSha256` prevents forged-history replay args.
- schema drift: validate old args against current tool schema before generating; incompatible → partial/blocked.
- prompt/code injection: captured strings are JSON data only.
- RAG canary: capture args with a fake secret, refresh Knowledge, assert search/cards never surface it.
- concurrent ordinal: eventId/ordinal generated atomically at write time, not by timestamp sort.
- new task ≠ auto-replayable: needs full stepRefs + capture not missing/truncated + schema-compatible + policy non-deny; else partial preview.
- old task: skeleton (or keys-only assisted skeleton). NO full backfill. Make Tool UI must say skeleton/preview/partial explicitly, not vague "replay".

## R2 Wave split

- **Wave A**: ActivityLog `eventId` + capture metadata helper + redaction pure-function + tests. Does NOT touch Task Atlas yet.
- **Wave B**: private captured-args store + sha/integrity + retention + RAG allowlist/canary test.
- **Wave C**: Task Atlas schema v2 (stepRefs/eligibility/UI labels); old-task downgrade.
- **Wave D**: preview composite generator + per-step policy/effective-diff output + smoke tests.
- **Wave E**: docs/AGENTS/README + TaskAtlas docs freshness.

## Risks ranked (R1)
1. P0 privacy leak (args into ActivityLog/RAG/support bundle).
2. P0 misleading replay (user thinks real write, actually dryRun/deny).
3. P1 stableId drift (rollover/rescan/duplicate-ts → stepRefs point wrong).
4. P1 truncated-but-generated (long task drops steps, UI says full).
5. P2 multi-point capture drift (AssistantRun vs Protocol behave differently).

## Verified facts (PM ground-truth, R0+R1)
- args values are NOT in any ActivityLog record point (all 3 store argumentKeys only): `UnrealMcpAssistantRun.cpp:444`, `UnrealMcpProtocol.cpp:343-360`, `UnrealMcpTaskAtlasTools.cpp:379-391`.
- criticalPath = `AddUnique(ToolName)` `UnrealMcpTaskAtlasTools.cpp:443`.
- eventRefs cap 250, fields ts/tool/isError only: `:25`, `:385-388`.
- ActivityLog indexed into RAG: `UnrealMcpKnowledgeTools.cpp` AddActivityLogCards.
- call_tool force-dryRun: `UnrealMcpCallToolLibrary.cpp:160-163`.
