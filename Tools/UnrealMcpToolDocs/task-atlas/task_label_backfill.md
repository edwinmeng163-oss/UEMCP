# unreal.task_label_backfill

**Category**: task-atlas
**Title**: Backfill Task Atlas Labels
**Risk level**: medium

Uses the configured Anthropic Messages provider to infer short labels for unpinned Task Atlas tasks that still have Session timestamp placeholders.

## Capabilities

- Requires write: true
- Requires build: false
- Requires external process: false
- Requires restart: false
- Requires lock: false
- Dry-run support: true
- Preflight support: true
- Postcheck support: true
- Test coverage: category

## Input schema

```json
{
  "type": "object",
  "properties": {
    "sessionId": {
      "type": "string",
      "description": "Optional Task Atlas sessionId filter."
    },
    "limit": {
      "type": "number",
      "description": "Maximum placeholder-label tasks to process. Defaults to 25.",
      "default": 25
    },
    "force": {
      "type": "boolean",
      "description": "Broaden candidate discovery while preserving pinned and user-edited safeguards.",
      "default": false
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview eligible tasks without calling the provider or writing task JSON.",
      "default": false
    }
  },
  "required": [],
  "additionalProperties": false
}
```

## Usage example

_Provenance: fixture-derived_

```json
{
  "sessionId": "fixture-session",
  "limit": 1,
  "force": true,
  "dryRun": true
}
```

## Provenance
- Source docs: Docs/TaskAtlas.md
- Reason: v0.19 Task Atlas retrospective labeling for placeholder workflow labels with pinned/user-edited safeguards.
- Notes: Calls Anthropic through configured provider settings, preferring claude-haiku-4-5 and falling back to claude-sonnet-4-6. Missing provider config returns success with no_provider_configured skips. The tool never overwrites pinned tasks or labels outside the exact Session YYYY-MM-DD HH:MM placeholder with empty userIntentText.
