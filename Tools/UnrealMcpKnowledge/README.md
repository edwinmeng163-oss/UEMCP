# Unreal MCP Knowledge Sources

This folder stores versioned source manifests for UEvolve's local knowledge/RAG
bootstrap. It should contain source lists, schemas, and small metadata files, not
downloaded third-party documentation payloads.

## Official Unreal Engine Docs

The first curated seed list is:

```text
Tools/UnrealMcpKnowledge/Sources/unreal_engine_official_docs_5_7.json
```

Fetch the seed pages into a local ignored cache:

```bash
python3 Tools/unreal_mcp_fetch_docs.py --max-pages 12
```

Output defaults to:

```text
Saved/UnrealMcp/KnowledgeSources/UnrealEngineOfficialDocs/5.7
```

The downloader prefers Epic's structured documentation JSON endpoint for normal
documentation pages and falls back to static HTML for pages such as the Unreal
Python API. Low extracted text counts are flagged in the generated manifest so
the indexer can skip or deprioritize weak pages.

Do not commit fetched official documentation content unless the upstream license
explicitly allows redistribution. Commit only source manifests and downloader
code.
