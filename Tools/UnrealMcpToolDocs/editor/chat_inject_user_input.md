# unreal.chat_inject_user_input

**Category**: editor
**Title**: Inject Chat Panel User Input
**Risk level**: medium

Inject a user prompt into the editor Chat Panel as if the user typed Enter, triggering the normal AI assistant flow.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: true
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "additionalProperties": false,
  "required": [
    "text"
  ],
  "properties": {
    "text": {
      "type": "string",
      "minLength": 1,
      "description": "User prompt to inject into the editor Chat Panel as if the user typed it and pressed Enter."
    },
    "sessionId": {
      "type": "string",
      "default": "",
      "description": "Optional ActivityLog sessionId. Default: current process session."
    },
    "dryRun": {
      "type": "boolean",
      "default": false,
      "description": "When true, report whether a panel is available without actually queuing the prompt."
    }
  }
}
```

## Usage example

_Provenance: schema-minimal_

```json
{
  "text": "Summarize the current editor status.",
  "dryRun": true
}
```

## Provenance

- Source docs: Docs/ChatSync.md
- Reason: v0.31 R4 chunk 9 CLI to editor Chat Panel user-input injection.
- Notes: AssistantRun approval is REQUIRED unless dryRun=true. Real injection writes chat state and can trigger an autonomous AI turn.
