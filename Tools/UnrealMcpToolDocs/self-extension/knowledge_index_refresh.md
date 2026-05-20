# unreal.knowledge_index_refresh

**Category**: self-extension
**Title**: Refresh Knowledge Index
**Risk level**: low

Rebuilds the local Saved/UnrealMcp/KnowledgeIndex/ JSONL index from fetched docs plus visible tool metadata for RAG retrieval; call after upstream docs change or after a registry-changing chunk.

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
    "sourceRoot": {
      "type": "string",
      "description": "Optional root containing fetched knowledge sources. Defaults to Saved/UnrealMcp/KnowledgeSources."
    },
    "indexRoot": {
      "type": "string",
      "description": "Optional output directory for the generated KnowledgeCard index. Defaults to Saved/UnrealMcp/KnowledgeIndex."
    },
    "includeOfficialDocs": {
      "type": "boolean",
      "description": "Include fetched official documentation documents.jsonl files.",
      "default": true
    },
    "includeVersionedDocs": {
      "type": "boolean",
      "description": "Include versioned README/Docs markdown files.",
      "default": true
    },
    "includeToolRegistry": {
      "type": "boolean",
      "description": "Include visible ToolRegistry entries as searchable tool cards.",
      "default": true
    },
    "skipLowContent": {
      "type": "boolean",
      "description": "Skip source rows flagged as low-content by the docs fetcher.",
      "default": true
    },
    "maxCards": {
      "type": "number",
      "description": "Maximum KnowledgeCards to write.",
      "default": 2000
    },
    "maxChunkChars": {
      "type": "number",
      "description": "Maximum text characters per card chunk.",
      "default": 1800
    },
    "chunkOverlapChars": {
      "type": "number",
      "description": "Overlapping characters between adjacent chunks.",
      "default": 160
    },
    "dryRun": {
      "type": "boolean",
      "description": "Preview source/card counts without writing index files.",
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
  "includeOfficialDocs": false,
  "includeVersionedDocs": true,
  "includeToolRegistry": true,
  "maxCards": 60,
  "maxChunkChars": 1200,
  "chunkOverlapChars": 80
}
```

## Provenance
- Source docs: Docs/KnowledgeRag.md
- Reason: Explicit registry: writes local Saved/UnrealMcp KnowledgeCard indexes for RAG search over docs and tool metadata.
- Notes: Downloaded official docs remain under ignored Saved/UnrealMcp; this tool writes only the local index.
