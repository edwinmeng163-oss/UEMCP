# Path Traversal Scaffold Checklist

- `unreal.mcp_apply_scaffold` dry-run must reject `ScaffoldMetadata.json` path traversal.
- The rejection must happen before reading or planning edits outside `Plugins/UnrealMcp/Source`.
