# Call Tool Agent Guide

Use `call_tool` inside Python user tools when a `user.*` tool needs to compose
safe, visible core `unreal.*` tools. Full reference: `Docs/CallTool.md`.

## Authoring Pattern

User tools still define only:

```python
def execute(args):
    ...
    return {"isError": False, "text": "...", "structuredContent": {...}}
```

Do not print result sentinels. The Python bridge wrapper imports `main`, calls
`main.execute(args)`, and serializes the returned dict.

Inside `execute`, call the injected builtins directly:

```python
def execute(args):
    status = call_tool("unreal.editor_status")
    return {
        "isError": False,
        "text": "status read",
        "structuredContent": {
            "policy": status["meta"]["policyDecision"],
            "engineVersion": status["structuredContent"].get("engineVersion"),
        },
    }
```

`call_tool` raises `RuntimeError` on MCP/tool errors. Let that raise when the
user tool cannot continue; the wrapper will return a structured error. Use
`call_tool_raw` only when the user tool can intentionally recover from a denied
or failed inner call.

## Safety Boundary

Only call visible core `unreal.*` tools. `user.*` targets are denied, nested
`call_tool` re-entry is denied, hidden tools are denied, and high-risk dynamic
tools such as `unreal.execute_python` are denied.

Dry-run-capable write tools may be forced to `dryRun=true`. Check
`result["meta"]["forcedDryRun"]` if your user-facing result needs to explain that
no real mutation occurred.

The approval gate is not interactive from Python. If the policy cannot safely
run the target directly or as a forced dry run, the call fails closed.

## Composition Example

```python
def execute(args):
    status = call_tool("unreal.editor_status")
    actors = call_tool("unreal.list_level_actors")
    return {
        "isError": False,
        "text": "editor snapshot collected",
        "structuredContent": {
            "editorStatusPolicy": status["meta"]["policyDecision"],
            "listActorsPolicy": actors["meta"]["policyDecision"],
            "actorCount": actors["structuredContent"].get("count"),
        },
    }
```

For a recoverable optional step:

```python
def execute(args):
    preview = call_tool_raw("unreal.mcp_user_registry_reload")
    return {
        "isError": False,
        "text": "reload preview checked",
        "structuredContent": {
            "reloadPolicy": preview["meta"]["policyDecision"],
            "reloadWasError": preview["isError"],
        },
    }
```
