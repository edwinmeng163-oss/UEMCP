# Unreal MCP Test Fixtures

This directory contains versioned MCP test cases that should be safe to commit and review.

Runtime-generated test scaffolds still belong under `Saved/UnrealMcp/TestScaffolds` and should remain local. These fixtures are stable smoke tests for core tools and self-extension safety rails.

Run the core suite from Editor Chat:

```text
/tool unreal.mcp_run_test_suite {"testsDir":"Tools/UnrealMcpTests/Core","readProjectMemory":false,"writeProjectMemory":false}
```

The suite includes happy path, missing required argument, and wrong type validation cases.

Skill Activity fixtures use numeric prefixes because a few cases intentionally build on
earlier setup steps. Keep lexical ordering when running the suite so recording starts
before distillation, draft save runs before promote dry-run validation, and the final
teardown case stops recording again.

## Category Suites

Additional category fixtures live outside `Core` so they can be run deliberately:

```text
Tools/UnrealMcpTests/Actors
Tools/UnrealMcpTests/Blueprint
Tools/UnrealMcpTests/SelfExtension
Tools/UnrealMcpTests/Widget
```

Run a category suite from Editor Chat:

```text
/tool unreal.mcp_run_test_suite {"testsDir":"Tools/UnrealMcpTests/Blueprint","readProjectMemory":false,"writeProjectMemory":false}
```

Happy-path write fixtures should use disposable sandboxes instead of
`executeTool:false` whenever the tool can be made deterministic. Current actor,
Blueprint, and Widget happy-path suites create/reset `/Game/__UEvolveMcpTest*`
content and `UEvolveMcpTest_*` level actors before writing. If a future fixture
cannot be made safe, mark it manual and explain why in the test description.

Wrapped test cases can include `expectToolCallStructuredFields` to assert scalar
fields inside the executed tool's `structuredContent` using dot-separated paths,
for example `preflight.evaluatedBeforeExecution`. This keeps safety metadata
testable instead of relying on manual inspection.
