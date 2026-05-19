# cli-anything-ueatelier

`cli-anything-ueatelier` is a small Python CLI wrapper for a running
UEAtelier-equipped Unreal Editor. It sends MCP JSON-RPC requests to the local
editor endpoint and prints either human-readable text or agent-friendly JSON.

Install from this repository subdirectory:

```bash
pip install git+https://github.com/edwinmeng163-oss/UEAtelier.git#subdirectory=Tools/UEAtelierCli
```

Minimal use:

```bash
cli-anything-ueatelier status
cli-anything-ueatelier tools list --category verification
cli-anything-ueatelier --json run unreal.editor_status --args-json '{}'
```

The default endpoint is `http://127.0.0.1:8765/mcp`. Override it with
`--endpoint URL` or `UEATELIER_MCP_ENDPOINT`.

Manual smoke with a running editor:

```bash
cli-anything-ueatelier status
cli-anything-ueatelier automation list --filter UnrealMcp --limit 10
cli-anything-ueatelier diagnostics --classes compile,log_error --limit 20
```

See [SKILL.md](SKILL.md) for the full CLI-Anything skill definition and command
reference.
