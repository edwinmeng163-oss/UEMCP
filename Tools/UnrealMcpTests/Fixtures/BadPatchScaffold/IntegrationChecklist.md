# Bad Patch Scaffold Checklist

- `unreal.mcp_inspect_scaffold` must report `readyForApply=false`.
- `unreal.mcp_apply_scaffold` dry-run must fail before source writes.
