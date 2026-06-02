# Chat Sync

Chat sync tools let external MCP clients inspect the editor Chat Panel state and, with approval, inject a user prompt into the live panel. The tools are intentionally narrow:

- `unreal.chat_inject_user_input` writes to the active Chat Panel and can trigger the normal assistant flow. AssistantRun approval is required unless `dryRun=true`.
- `unreal.chat_history_tail` reads persisted chat history and omits private details fields.
- `unreal.chat_tool_log_tail` reads ActivityLog `tool_call` events and returns argument keys only, never argument values.

These tools share the existing Chat Panel command path and ActivityLog storage; they do not change MCP transport capabilities.
