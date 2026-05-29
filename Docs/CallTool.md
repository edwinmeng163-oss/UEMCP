# Call Tool

`call_tool` is the Python user-tool bridge for composing existing core Unreal MCP
tools. It lets a project-local `user.*` Python tool call a visible `unreal.*`
core tool through the same UFUNCTION and dispatcher path used by the editor UI.

It is not a registry entry and does not change the canonical tool count.

## Runtime Contract

Wave A exposes the UFUNCTION to Python as:

```python
unreal.UnrealMcpCallTool.call_tool(tool_name, arguments_json)
```

It returns a JSON string with this object shape:

```json
{
  "toolName": "unreal.editor_status",
  "text": "...",
  "isError": false,
  "structuredContent": {},
  "meta": {
    "policyDecision": "allow",
    "forcedDryRun": false,
    "truncated": false,
    "reason": ""
  }
}
```

`text` is capped at 20000 characters. If truncation occurs,
`meta.truncated=true`.

## Policy

The policy is fail-closed and decided before the target tool executes.

| Decision | Current count | Meaning |
|---|---:|---|
| `allow` | 61 | visible, read-safe core tools execute directly |
| `force_dry_run` | 26 | visible dry-run-capable write tools execute with `dryRun=true` injected |
| `deny` | 82 | high-risk, dynamic, workflow, hidden, user, or nested targets refuse |

Denied calls return `isError=true` and `meta.reason`. Dangerous dynamic tools such
as `unreal.execute_python` are denied with `dangerous_no_dryrun`.

The nested-call guard allows depth 1 only. A user Python tool may call one core
tool, but the called core tool cannot re-enter `call_tool` again.

`user.*` targets are forbidden. This prevents a Python user tool from recursively
starting another Python user tool and avoids nested wrapper/module conflicts.

The AssistantRun approval seam is policy-only for this path: `call_tool` does not
prompt for approvals. Tools that would require approval from autonomous assistant
execution are either forced to dry run or denied.

## Python Helpers

The Python registered-tool wrapper injects two builtins before `main.execute`:

```python
call_tool(name, args=None)
call_tool_raw(name, args=None)
```

`call_tool` serializes `args or {}`, invokes
`unreal.UnrealMcpCallTool.call_tool`, parses the JSON result, and raises
`RuntimeError` when `isError` is true. The exception message prefers
`meta.reason`, then `text`, then `call_tool failed`.

`call_tool_raw` performs the same call but never raises. It returns the full dict
including `isError`, `structuredContent`, and `meta`.

The wrapper removes both builtins in its `finally` block after each execution.

## Example

```python
def execute(args):
    status = call_tool("unreal.editor_status")
    actors = call_tool("unreal.list_level_actors")
    return {
        "isError": False,
        "text": "call_tool re-entry ok",
        "structuredContent": {
            "engine": status["structuredContent"].get("engineVersion"),
            "actorPolicy": actors["meta"]["policyDecision"],
        },
    }
```

The committed sample lives at
`Tools/UnrealMcpPyToolSamples/call_tool_demo/`. Copy it to a host project's
`Tools/UnrealMcpPyTools/call_tool_demo/`, run
`unreal.mcp_user_registry_reload`, then call `user.call_tool_demo`.
