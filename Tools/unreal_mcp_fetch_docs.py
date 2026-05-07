#!/usr/bin/env python3
"""Fetch curated official documentation pages into a local UEvolve RAG cache.

The fetched page content is for local indexing only. By default this script
writes under Saved/UnrealMcp so official docs are not redistributed through Git.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from datetime import datetime, timezone
from html.parser import HTMLParser
from pathlib import Path
from typing import Iterable


DEFAULT_SEED_FILE = Path("Tools/UnrealMcpKnowledge/Sources/unreal_engine_official_docs_5_7.json")
DEFAULT_TIMEOUT_SECONDS = 30
DEFAULT_DELAY_SECONDS = 0.4
DEFAULT_USER_AGENT = "UEvolve-RAG-Fetcher/0.1 (+https://github.com/edwinmeng163-oss/UEvolve)"
ALLOWED_HOSTS = {"dev.epicgames.com"}
ALLOWED_PATH_PREFIXES = ("/documentation/en-us/unreal-engine",)
DOCUMENTATION_API_URL = "https://dev.epicgames.com/community/api/documentation/document.json"


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def slugify(value: str) -> str:
    value = value.strip().lower()
    value = re.sub(r"[^a-z0-9._-]+", "-", value)
    value = re.sub(r"-{2,}", "-", value).strip("-")
    return value or "document"


def repo_root_from(start: Path) -> Path:
    for candidate in (start, *start.parents):
        if (candidate / "Plugins/UnrealMcp/UnrealMcp.uplugin").exists():
            return candidate
    return start


class TextExtractor(HTMLParser):
    SKIP_TAGS = {"script", "style", "noscript", "svg"}
    BLOCK_TAGS = {
        "address",
        "article",
        "aside",
        "blockquote",
        "br",
        "div",
        "dl",
        "fieldset",
        "figcaption",
        "figure",
        "footer",
        "form",
        "h1",
        "h2",
        "h3",
        "h4",
        "h5",
        "h6",
        "header",
        "hr",
        "li",
        "main",
        "nav",
        "ol",
        "p",
        "pre",
        "section",
        "table",
        "tr",
        "ul",
    }

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.parts: list[str] = []
        self.links: list[str] = []
        self.title_parts: list[str] = []
        self._skip_depth = 0
        self._in_title = False

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        tag = tag.lower()
        if tag in self.SKIP_TAGS:
            self._skip_depth += 1
            return
        if tag == "title":
            self._in_title = True
        if tag in self.BLOCK_TAGS:
            self.parts.append("\n")
        if tag == "a":
            attrs_dict = dict(attrs)
            href = attrs_dict.get("href")
            if href:
                self.links.append(href)
        if tag.startswith("block-"):
            attrs_dict = {key: value for key, value in attrs if value}
            for key in ("page-name", "title", "description", "href"):
                value = attrs_dict.get(key)
                if value:
                    self.parts.append(f" {value} ")

    def handle_endtag(self, tag: str) -> None:
        tag = tag.lower()
        if tag in self.SKIP_TAGS and self._skip_depth > 0:
            self._skip_depth -= 1
            return
        if tag == "title":
            self._in_title = False
        if tag in self.BLOCK_TAGS:
            self.parts.append("\n")

    def handle_data(self, data: str) -> None:
        if self._skip_depth:
            return
        if self._in_title:
            self.title_parts.append(data)
        self.parts.append(data)

    def text(self) -> str:
        joined = html.unescape(" ".join(self.parts))
        joined = re.sub(r"[ \t\r\f\v]+", " ", joined)
        joined = re.sub(r"\n[ \t]+", "\n", joined)
        joined = re.sub(r"\n{3,}", "\n\n", joined)
        return joined.strip()

    def title(self) -> str:
        joined = html.unescape(" ".join(self.title_parts))
        return re.sub(r"\s+", " ", joined).strip()


@dataclass
class Source:
    id: str
    title: str
    url: str
    category: str = "unreal-docs"
    tags: list[str] = field(default_factory=list)
    parent_id: str | None = None


def load_seed_file(path: Path) -> tuple[dict, list[Source]]:
    if not path.exists():
        fail(f"Seed file does not exist: {path}")
    data = json.loads(path.read_text(encoding="utf-8"))
    sources = []
    for item in data.get("sources", []):
        sources.append(
            Source(
                id=str(item["id"]),
                title=str(item.get("title") or item["id"]),
                url=str(item["url"]),
                category=str(item.get("category") or "unreal-docs"),
                tags=[str(tag) for tag in item.get("tags", [])],
            )
        )
    return data, sources


def canonicalize_url(url: str, base_url: str | None = None, application_version: str | None = None) -> str | None:
    absolute = urllib.parse.urljoin(base_url or "", url)
    parsed = urllib.parse.urlparse(absolute)
    if parsed.scheme not in {"http", "https"}:
        return None
    if parsed.netloc not in ALLOWED_HOSTS:
        return None
    if not any(parsed.path.startswith(prefix) for prefix in ALLOWED_PATH_PREFIXES):
        return None

    query = dict(urllib.parse.parse_qsl(parsed.query, keep_blank_values=False))
    if application_version and "application_version" not in query:
        query["application_version"] = application_version
    normalized_query = urllib.parse.urlencode(sorted(query.items()))
    normalized = parsed._replace(fragment="", query=normalized_query)
    return urllib.parse.urlunparse(normalized)


def fetch_url(url: str, timeout_seconds: int, user_agent: str) -> tuple[int, str, bytes]:
    request = urllib.request.Request(url, headers={"User-Agent": user_agent, "Accept": "text/html,application/xhtml+xml"})
    with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
        status = int(getattr(response, "status", 200))
        final_url = response.geturl()
        body = response.read()
    return status, final_url, body


def documentation_api_url_for(url: str, application_version: str, lang: str = "en-US") -> str | None:
    parsed = urllib.parse.urlparse(url)
    if parsed.netloc not in ALLOWED_HOSTS:
        return None
    if "/python-api" in parsed.path:
        # The Python API docs are static Sphinx pages and already contain text.
        return None
    if not any(parsed.path.startswith(prefix) for prefix in ALLOWED_PATH_PREFIXES):
        return None

    doc_path = parsed.path
    doc_path = doc_path.replace("/documentation/en-us/", "/documentation/", 1)
    query = dict(urllib.parse.parse_qsl(parsed.query, keep_blank_values=False))
    app_version = query.get("application_version") or application_version
    api_query = {
        "path": doc_path,
        "application_version": app_version,
        "lang": lang,
    }
    return DOCUMENTATION_API_URL + "?" + urllib.parse.urlencode(api_query)


def content_html_from_document_json(data: dict) -> str:
    parts: list[str] = []
    for block in data.get("blocks", []):
        if isinstance(block, dict):
            html_value = block.get("content_html")
            if isinstance(html_value, str):
                parts.append(html_value)
    return "\n\n".join(parts)


def fetch_document(
    url: str,
    timeout_seconds: int,
    user_agent: str,
    application_version: str,
) -> tuple[str, int, str, bytes, str, str, list[str]]:
    api_url = documentation_api_url_for(url, application_version=application_version)
    if api_url:
        try:
            status, final_url, body = fetch_url(api_url, timeout_seconds, user_agent)
            data = json.loads(body.decode("utf-8", errors="replace"))
            html_body = content_html_from_document_json(data)
            if html_body:
                extractor = TextExtractor()
                extractor.feed(html_body)
                title = str(data.get("title") or extractor.title() or "")
                return "documentation_api", status, final_url, body, html_body, title, extractor.links
        except (urllib.error.URLError, TimeoutError, OSError, UnicodeError, json.JSONDecodeError):
            # Fall through to raw HTML. The manifest records the final fetch mode.
            pass

    status, final_url, body = fetch_url(url, timeout_seconds, user_agent)
    decoded = body.decode("utf-8", errors="replace")
    extractor = TextExtractor()
    extractor.feed(decoded)
    return "html", status, final_url, body, decoded, extractor.title(), extractor.links


def write_json(path: Path, data: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def append_jsonl(path: Path, rows: Iterable[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        for row in rows:
            handle.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")


def source_from_link(link_url: str, parent: Source, index: int) -> Source:
    parsed = urllib.parse.urlparse(link_url)
    stem = Path(parsed.path).name or "index"
    return Source(
        id=f"{parent.id}-link-{index:03d}-{slugify(stem)}",
        title=stem.replace("-", " ").replace("_", " ").strip().title() or link_url,
        url=link_url,
        category=parent.category,
        tags=list(parent.tags),
        parent_id=parent.id,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch curated official Unreal Engine docs into a local RAG cache.")
    parser.add_argument("--seed-file", default=str(DEFAULT_SEED_FILE), help="JSON seed file with official docs URLs.")
    parser.add_argument("--output-dir", default=None, help="Output directory. Defaults to seed defaultOutputDir.")
    parser.add_argument("--max-pages", type=int, default=12, help="Maximum pages to fetch.")
    parser.add_argument("--include-linked", action="store_true", help="Also enqueue in-scope links discovered on fetched pages.")
    parser.add_argument("--application-version", default="5.7", help="application_version query parameter to add to discovered links.")
    parser.add_argument("--delay-seconds", type=float, default=DEFAULT_DELAY_SECONDS, help="Delay between requests.")
    parser.add_argument("--timeout-seconds", type=int, default=DEFAULT_TIMEOUT_SECONDS, help="HTTP timeout.")
    parser.add_argument("--user-agent", default=DEFAULT_USER_AGENT, help="HTTP User-Agent.")
    parser.add_argument("--min-text-chars", type=int, default=500, help="Warn when extracted text is shorter than this.")
    parser.add_argument("--force", action="store_true", help="Refetch pages even if source.html already exists.")
    parser.add_argument("--dry-run", action="store_true", help="Print planned URLs without downloading.")
    args = parser.parse_args()

    repo_root = repo_root_from(Path.cwd())
    seed_path = Path(args.seed_file)
    if not seed_path.is_absolute():
        seed_path = repo_root / seed_path
    seed, initial_sources = load_seed_file(seed_path)

    output_dir = Path(args.output_dir or seed.get("defaultOutputDir") or "Saved/UnrealMcp/KnowledgeSources/UnrealEngineOfficialDocs/5.7")
    if not output_dir.is_absolute():
        output_dir = repo_root / output_dir

    queue = list(initial_sources)
    seen_urls: set[str] = set()
    fetched_rows: list[dict] = []
    manifest_entries: list[dict] = []

    print(f"Seed: {seed_path}")
    print(f"Output: {output_dir}")
    print(f"Max pages: {args.max_pages}")

    if args.dry_run:
        for source in queue[: args.max_pages]:
            print(f"DRY RUN: {source.id}: {source.url}")
        return 0

    while queue and len(fetched_rows) < args.max_pages:
        source = queue.pop(0)
        canonical_url = canonicalize_url(source.url, application_version=args.application_version)
        if not canonical_url or canonical_url in seen_urls:
            continue
        seen_urls.add(canonical_url)

        doc_slug = slugify(source.id)
        doc_dir = output_dir / "pages" / doc_slug
        html_path = doc_dir / "source.html"
        text_path = doc_dir / "content.txt"
        meta_path = doc_dir / "metadata.json"

        if html_path.exists() and text_path.exists() and not args.force:
            text = text_path.read_text(encoding="utf-8", errors="replace")
            metadata = json.loads(meta_path.read_text(encoding="utf-8")) if meta_path.exists() else {}
            row = {
                "id": source.id,
                "title": metadata.get("title") or source.title,
                "url": canonical_url,
                "category": source.category,
                "tags": source.tags,
                "textPath": str(text_path.relative_to(output_dir)),
                "metadataPath": str(meta_path.relative_to(output_dir)),
                "charCount": len(text),
                "cached": True,
            }
            fetched_rows.append(row)
            manifest_entries.append(row)
            print(f"CACHED {source.id}: {canonical_url}")
            continue

        try:
            fetch_mode, status, final_url, body, decoded, fetched_title, discovered_links = fetch_document(
                canonical_url,
                args.timeout_seconds,
                args.user_agent,
                args.application_version,
            )
            extractor = TextExtractor()
            extractor.feed(decoded)
            text = extractor.text()
            title = fetched_title or extractor.title() or source.title
            sha256 = hashlib.sha256(body).hexdigest()
            b_low_content = len(text) < args.min_text_chars

            doc_dir.mkdir(parents=True, exist_ok=True)
            if fetch_mode == "documentation_api":
                (doc_dir / "source_api.json").write_bytes(body)
                html_path.write_text(decoded, encoding="utf-8")
            else:
                html_path.write_bytes(body)
            text_path.write_text(text + "\n", encoding="utf-8")

            metadata = {
                "id": source.id,
                "title": title,
                "seedTitle": source.title,
                "url": canonical_url,
                "finalUrl": final_url,
                "category": source.category,
                "tags": source.tags,
                "parentId": source.parent_id,
                "status": status,
                "bytes": len(body),
                "sha256": sha256,
                "charCount": len(text),
                "fetchMode": fetch_mode,
                "lowContentWarning": b_low_content,
                "minTextChars": args.min_text_chars,
                "fetchedAt": utc_now(),
                "sourceSeed": str(seed_path),
                "licenseNotice": seed.get("licenseNotice", ""),
            }
            write_json(meta_path, metadata)

            row = {
                "id": source.id,
                "title": title,
                "url": canonical_url,
                "category": source.category,
                "tags": source.tags,
                "textPath": str(text_path.relative_to(output_dir)),
                "metadataPath": str(meta_path.relative_to(output_dir)),
                "charCount": len(text),
                "bytes": len(body),
                "sha256": sha256,
                "fetchMode": fetch_mode,
                "lowContentWarning": b_low_content,
                "cached": False,
            }
            fetched_rows.append(row)
            manifest_entries.append(row)
            warning_suffix = " LOW_CONTENT" if b_low_content else ""
            print(f"FETCHED {source.id}: {status} {fetch_mode} {len(body)} bytes {len(text)} chars{warning_suffix}")

            if args.include_linked:
                link_count = 0
                for raw_link in [*extractor.links, *discovered_links]:
                    linked_url = canonicalize_url(raw_link, base_url=final_url, application_version=args.application_version)
                    if not linked_url or linked_url in seen_urls:
                        continue
                    link_count += 1
                    queue.append(source_from_link(linked_url, source, link_count))
                    if len(queue) + len(fetched_rows) >= args.max_pages:
                        break
        except (urllib.error.URLError, TimeoutError, OSError, UnicodeError) as exc:
            error_row = {
                "id": source.id,
                "title": source.title,
                "url": canonical_url,
                "category": source.category,
                "tags": source.tags,
                "error": str(exc),
                "fetchedAt": utc_now(),
            }
            manifest_entries.append(error_row)
            print(f"FAILED {source.id}: {exc}", file=sys.stderr)

        if args.delay_seconds > 0 and queue and len(fetched_rows) < args.max_pages:
            time.sleep(args.delay_seconds)

    manifest = {
        "name": seed.get("name", "unreal_docs"),
        "seedFile": str(seed_path),
        "outputDir": str(output_dir),
        "createdAt": utc_now(),
        "maxPages": args.max_pages,
        "includeLinked": args.include_linked,
        "applicationVersion": args.application_version,
        "fetchedCount": len(fetched_rows),
        "entryCount": len(manifest_entries),
        "licenseNotice": seed.get("licenseNotice", ""),
        "entries": manifest_entries,
    }
    write_json(output_dir / "manifest.json", manifest)
    append_jsonl(output_dir / "documents.jsonl", fetched_rows)
    print(f"Wrote {output_dir / 'manifest.json'}")
    print(f"Wrote {output_dir / 'documents.jsonl'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
