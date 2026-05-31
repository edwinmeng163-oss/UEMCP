# UEAtelier v0.31 Stage 3 — §10.3 Isolated C++ Extended Tools: R0 verdict = DEFER

**Status**: R0 complete 2026-05-31. Verdict (three-way concur — PM ground-truth
+ Hermes red-team + Anthropic 2nd-opinion): **do NOT build the full
"AI-generated isolated C++ plugin pipeline" now. Defer.**

## What §10.3 was

The "missing middle" between two existing self-extension paths:
1. **Core C++ pipeline** (`mcp_apply_scaffold` + 12 `UnrealMcpSelfExtension*Tools.cpp`):
   edits UnrealMcp's OWN C++ + registry; needs rebuild+restart; walled-off since v0.27.
2. **Python user-tools** (`scaffold_mcp_tool`): safe, hot-reloaded, but Python-only.

§10.3 = a user's REAL C++ tools in an isolated extension plugin, dynamically
registered into the MCP registry, not polluting the core. Plan framing:
"generate a UE5.8 ToolsetRegistry-style isolated C++ extension plugin via Code Tools."

## Why defer (the three killers)

- **Q1 — isolation buys no UX advantage.** UE C++ has no Python-style hot reload.
  An isolated plugin STILL needs UBT build + editor restart, exactly like editing
  core C++ (lifecycle proves it: `AppliedCoreCppBuildRequired → BuiltRestartRequired
  → LoadedCoreCppAfterRestart`, `UnrealMcpSelfExtensionApplyTools.cpp:205,2507,2543`).
  Isolation's only real gain is not-polluting-core / distributable / lower merge
  risk / audit separation — NOT immediacy. The headline "dynamic C++ tools" is
  largely a misnomer.
- **Q5 — demand evidence is weak.** Python user-tool + call_tool + preview
  composite (v0.30/31, shipped) already cover the "compose existing tools" main
  need. Isolated C++ only serves the narrower native-only niche (UE C++ APIs
  Python can't reach, Slate internals, perf/long-tasks, third-party C++ SDKs).
  Without 2-3 concrete C++-only dogfood cases, full cpp_extension_tool is
  "architecturally elegant, ROI-dubious."
- **Q4 — ToolsetRegistry unverified.** `ToolsetRegistry` appears ONLY in our
  planning doc; not in repo, not in local UE 5.7 Engine Source, not in public
  search. Project ships 5.6/5.7. Betting architecture on an unverified UE5.8
  mechanism is premature.

Plus P0 reality: core has NO public registration ABI — ToolRegistry/HandlerRegistry
are Private, `UnrealMcp.Build.cs` public deps are Core-only. Any external plugin
would first require defining a real public SDK surface.

## Reframe (if/when resumed)

**§10.3 = External C++ Tool Registration ABI spike** — NOT cpp_extension_tool,
NOT an auto-generate/compile/restart pipeline.

Gate to start: collect 2 concrete C++-only dogfood cases first. If it's only
"Python is theoretically insufficient," keep deferring until UE5.8 ships an
official MCP/extension mechanism.

Minimal MVP (developer-manual, NO AI auto-apply) if resumed:
```text
Core one-time public API (needs UNREALMCP_API; relevant headers currently Private):
  struct FExternalCppToolDescriptor { name,title,description,inputSchema,policy,namespace,owner }
  using FExternalCppToolHandler = TFunction<FUnrealMcpExecutionResult(const FJsonObject&)>
  RegisterExternalCppTool(ModuleName, Descriptor, Handler) / UnregisterExternalCppTools(ModuleName)
Dispatcher: ExecuteToolInternal -> FindExternalCppHandler -> delegate; else existing path
Sample plugin: Plugins/UnrealMcpUserExt (depends on UnrealMcp; StartupModule registers; ShutdownModule unregisters)
Naming: NOT user.* (call_tool rejects user targets). Use ext.<plugin>.<tool>; default hidden/deny-call_tool until R1 safety model.
```
Open P0s for that R1: public SDK/ABI surface; native-code safety (arbitrary C++,
bigger than Python); module-unload dangling-delegate lifetime; name conflicts;
schema validation; 5.6/5.7×Mac/Win build matrix; external tools must NOT change
the core 181 count.

## Decision
Accept the defer. v0.31 = Stage 1 (SHA consolidation) + Stage 2 (preview composite,
5 Waves), both pushed to origin/main (HEAD 7b7377b). Stage 3 §10.3 deferred with
this documented rationale. Full R0: session in /tmp/v0.31-s3-r0-output.md.
