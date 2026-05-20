# unreal.mcp_lock_extension_session

**Category**: self-extension
**Title**: Lock Extension Session
**Risk level**: low

Acquires or releases the local self-extension lock that gates high-risk scaffold apply, package import, and build actions.

## Capabilities

- Requires write: false
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: false
- Preflight support: true
- Postcheck support: true
- Test coverage: missing

## Input schema

```json
{
  "type": "object",
  "properties": {
    "mode": {
      "type": "string",
      "description": "Lock mode: status, acquire, release, or refresh.",
      "default": "status"
    },
    "sessionId": {
      "type": "string",
      "description": "Session id for release/refresh."
    },
    "owner": {
      "type": "string",
      "description": "Human-readable lock owner.",
      "default": "Unreal MCP Chat"
    },
    "reason": {
      "type": "string",
      "description": "Why this extension session is locked."
    },
    "ttlSeconds": {
      "type": "number",
      "description": "Lock TTL in seconds, clamped to 30..86400.",
      "default": 900
    },
    "force": {
      "type": "boolean",
      "description": "Override a stale or foreign lock. Use carefully.",
      "default": false
    }
  },
  "required": [],
  "additionalProperties": false
}
```

## Usage example

_Provenance: schema-minimal_

```json
{}
```

## Provenance
- Source docs: README.md#tool-coverage
- Reason: Explicit registry: self-extension tool with controlled write or workflow side effects.
