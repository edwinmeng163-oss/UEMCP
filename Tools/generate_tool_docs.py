#!/usr/bin/env python3
"""Generate per-tool markdown reference docs from the Unreal MCP registry."""

from __future__ import annotations

import argparse
import copy
import json
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
REGISTRY = ROOT / "Tools" / "UnrealMcpToolRegistry" / "tools.json"
JSON_FIXTURES = ROOT / "Tools" / "UnrealMcpTests"
CPP_FIXTURES = ROOT / "Plugins" / "UnrealMcp" / "Source" / "UnrealMcp" / "Private" / "Tests"
OUT = ROOT / "Tools" / "UnrealMcpToolDocs"

CATEGORIES = [
    "actors",
    "blueprint",
    "editor",
    "material",
    "memory",
    "scaffold",
    "self-extension",
    "skills",
    "task-atlas",
    "verification",
    "widget",
]
SCHEMA_ORDER = ["type", "properties", "required", "additionalProperties"]

CPP_TOOL_RE = re.compile(r'TEXT\("(?P<tool>unreal\.[A-Za-z0-9_.]+)"\)')
CPP_ARG_BLOCK_RE = re.compile(
    r"TSharedPtr<FJsonObject>\s+(?P<var>[A-Za-z0-9_]+)\s*=\s*MakeShared<FJsonObject>\(\);(?P<body>.*?)"
    r"TryExecute[A-Za-z0-9_:]*\(\s*TEXT\(\"(?P<tool>unreal\.[A-Za-z0-9_.]+)\"\)\s*,\s*\*(?P=var)",
    re.DOTALL,
)
CPP_SET_FIELD_RE = re.compile(
    r"(?P<var>[A-Za-z0-9_]+)->Set(?P<kind>String|Number|Bool)Field"
    r"\(\s*TEXT\(\"(?P<field>[^\"]+)\"\)\s*,\s*(?P<value>.*?)\s*\);",
    re.DOTALL,
)


def rel(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def load_tools() -> list[dict[str, Any]]:
    with REGISTRY.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    tools = data.get("tools")
    if not isinstance(tools, list):
        raise ValueError(f"{rel(REGISTRY)} does not contain a tools array")
    return tools


def doc_path(tool: dict[str, Any]) -> Path:
    category = tool.get("category")
    if category not in CATEGORIES:
        raise ValueError(f"{tool.get('name', '<unnamed>')} has unknown category {category!r}")
    name = tool["name"]
    short = name[len("unreal.") :] if name.startswith("unreal.") else name
    return OUT / category / f"{short.replace('.', '_')}.md"


def ordered(value: Any) -> Any:
    if isinstance(value, list):
        return [ordered(item) for item in value]
    if not isinstance(value, dict):
        return value
    result = {key: ordered(value[key]) for key in SCHEMA_ORDER if key in value}
    result.update((key, ordered(item)) for key, item in value.items() if key not in result)
    return result


def json_block(value: Any) -> str:
    return json.dumps(ordered(value), indent=2, ensure_ascii=False)


def remember(best: dict[str, tuple[tuple[int, str], dict[str, Any]]], tool: str, args: dict[str, Any], rank: tuple[int, str]) -> None:
    if tool not in best or rank < best[tool][0]:
        best[tool] = (rank, copy.deepcopy(args))


def json_rank(path: Path, data: dict[str, Any]) -> tuple[int, str]:
    score = 0
    lowered = path.name.lower()
    if data.get("category") != "happy_path":
        score += 20
    if data.get("expectToolCallError") is True:
        score += 50
    if "_valid" not in lowered and "happy" not in lowered and "smoke" not in lowered:
        score += 10
    if any(token in lowered for token in ("missing", "wrong", "invalid", "bad", "reject")):
        score += 40
    return score, rel(path)


def collect_json_examples(tool_names: set[str]) -> dict[str, dict[str, Any]]:
    best: dict[str, tuple[tuple[int, str], dict[str, Any]]] = {}
    for path in sorted(JSON_FIXTURES.rglob("*.json")):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        if not isinstance(data, dict):
            continue
        params = data.get("request", {}).get("params", {})
        name = params.get("name") if isinstance(params, dict) else None
        args = params.get("arguments") if isinstance(params, dict) else None
        if name in tool_names and isinstance(args, dict):
            remember(best, name, args, json_rank(path, data))
    return {name: args for name, (_rank, args) in best.items()}


def cpp_value(kind: str, raw: str) -> Any:
    value = " ".join(raw.strip().split())
    text_match = re.fullmatch(r'TEXT\("([^"]*)"\)', value)
    if kind == "String":
        return text_match.group(1) if text_match else "<string>"
    if kind == "Number":
        return float(value) if re.fullmatch(r"-?\d+\.\d+", value) else int(value) if re.fullmatch(r"-?\d+", value) else 0
    if kind == "Bool":
        return value == "true"
    return None


def collect_cpp_examples(tool_names: set[str]) -> dict[str, dict[str, Any]]:
    best: dict[str, tuple[tuple[int, str], dict[str, Any]]] = {}
    for path in sorted(CPP_FIXTURES.rglob("*.cpp")):
        try:
            text = path.read_text(encoding="utf-8")
        except OSError:
            continue
        if not any(match.group("tool") in tool_names for match in CPP_TOOL_RE.finditer(text)):
            continue
        for block in CPP_ARG_BLOCK_RE.finditer(text):
            name = block.group("tool")
            if name not in tool_names:
                continue
            var_name = block.group("var")
            args: dict[str, Any] = {}
            for field in CPP_SET_FIELD_RE.finditer(block.group("body")):
                if field.group("var") == var_name:
                    args[field.group("field")] = cpp_value(field.group("kind"), field.group("value"))
            remember(best, name, args, (100, rel(path)))
    return {name: args for name, (_rank, args) in best.items()}


def collect_examples(tool_names: set[str]) -> dict[str, dict[str, Any]]:
    examples = collect_cpp_examples(tool_names)
    examples.update(collect_json_examples(tool_names))
    return examples


def schema_type(schema: dict[str, Any]) -> str:
    kind = schema.get("type")
    if isinstance(kind, list):
        return str(next((item for item in kind if item != "null"), kind[0] if kind else "string"))
    if isinstance(kind, str):
        return kind
    if "properties" in schema:
        return "object"
    if "items" in schema:
        return "array"
    return "string"


def string_placeholder(field: str) -> str:
    lowered = field.lower()
    if "path" in lowered or lowered.endswith("dir") or "directory" in lowered:
        return "/Game/Path"
    if "actor" in lowered or "label" in lowered or lowered.endswith("name"):
        return "actor-name"
    return "<string>"


def placeholder(field: str, schema: dict[str, Any]) -> Any:
    enum_values = schema.get("enum")
    if isinstance(enum_values, list) and enum_values:
        return copy.deepcopy(enum_values[0])
    kind = schema_type(schema)
    if kind == "string":
        return string_placeholder(field)
    if kind in {"integer", "number"}:
        return 0
    if kind == "boolean":
        return False
    if kind == "array":
        return []
    if kind == "object":
        return {}
    return None


def schema_example(input_schema: dict[str, Any]) -> dict[str, Any]:
    props = input_schema.get("properties")
    required = input_schema.get("required", [])
    if not isinstance(props, dict) or not isinstance(required, list):
        return {}
    return {
        field: placeholder(field, props.get(field, {}) if isinstance(props.get(field, {}), dict) else {})
        for field in required
        if isinstance(field, str)
    }


def flag(value: Any) -> str:
    return "true" if value is True else "false" if value is False else str(value)


def render_tool(tool: dict[str, Any], examples: dict[str, dict[str, Any]]) -> tuple[str, str]:
    name = tool["name"]
    input_schema = tool.get("inputSchema") if isinstance(tool.get("inputSchema"), dict) else {"type": "object", "properties": {}}
    provenance = "fixture-derived" if name in examples else "schema-minimal"
    example = examples.get(name, schema_example(input_schema))

    lines = [
        f"# {name}",
        "",
        f"**Category**: {tool.get('category', '')}",
        f"**Title**: {tool.get('title') or name}",
        f"**Risk level**: {tool.get('riskLevel', '')}",
    ]
    exposure = tool.get("exposure", "visible")
    if exposure != "visible":
        lines.append(f"**Exposure**: {exposure}")

    lines += [
        "",
        str(tool.get("description") or "_No description provided._"),
        "",
        "## Capabilities",
        "",
        f"- Requires write: {flag(tool.get('requiresWrite'))}",
        f"- Requires build: {flag(tool.get('requiresBuild'))}",
        f"- Requires external process: {flag(tool.get('requiresExternalProcess'))}",
        f"- Requires restart: {flag(tool.get('requiresRestart'))}",
        f"- Requires lock: {flag(tool.get('requiresLock'))}",
        f"- Dry-run support: {flag(tool.get('dryRunSupport'))}",
        f"- Preflight support: {flag(tool.get('preflightSupport'))}",
        f"- Postcheck support: {flag(tool.get('postcheckSupport'))}",
        f"- Test coverage: {tool.get('testCoverage', '')}",
        "",
        "## Input schema",
        "",
        "```json",
        json_block(input_schema),
        "```",
        "",
        "## Usage example",
        "",
        f"_Provenance: {provenance}_",
        "",
        "```json",
        json_block(example),
        "```",
        "",
        "## Provenance",
    ]

    handler = tool.get("handlerName")
    if handler and handler != name:
        lines.append(f"- Handler: {handler}")
    if tool.get("docsPath"):
        lines.append(f"- Source docs: {tool['docsPath']}")
    if tool.get("reason"):
        lines.append(f"- Reason: {tool['reason']}")
    if tool.get("notes"):
        lines.append(f"- Notes: {tool['notes']}")
    lines.append("")
    return "\n".join(lines), provenance


def render_readme(category_counts: Counter[str], tool_count: int) -> str:
    lines = [
        "# Unreal MCP Tool Docs",
        "",
        "DO NOT HAND-EDIT — generated by Tools/generate_tool_docs.py from Tools/UnrealMcpToolRegistry/tools.json.",
        "",
        "These files are per-tool reference pages for CLI agents, offline review, and tool recommendation context.",
        "",
        "## Generator",
        "",
        "- Generate: `python3 Tools/generate_tool_docs.py`",
        "- Check idempotence: `python3 Tools/generate_tool_docs.py --check`",
        "",
        "## Directory Structure",
        "",
    ]
    lines += [f"- `{category}/`: {category_counts.get(category, 0)} tools" for category in CATEGORIES]
    lines += [
        "",
        "## Field Definitions",
        "",
        "- Category: ToolRegistry ownership category used for dispatch, audit, and documentation grouping.",
        "- Title: Human-readable display title from the registry.",
        "- Risk level: ToolRegistry risk classification for planning and execution guard policy.",
        "- Exposure: Visibility override. Visible tools omit the line; legacy-hidden tools include it.",
        "- Capabilities flags: Boolean registry policy fields for writes, builds, external processes, restarts, locks, dry runs, preflight, and postcheck support.",
        "- Test coverage: Registry coverage label for committed fixtures or automation coverage.",
        "- Usage example provenance: `fixture-derived` means the example arguments came from committed fixtures; `schema-minimal` means required fields were filled from the input schema.",
        "- Provenance: Handler aliases, source docs, reasons, and notes copied from the registry entry.",
        "",
        "## Skill Docs vs Tool Docs",
        "",
        "`Tools/UnrealMcpSkills/` contains reusable task instructions discovered by the in-editor skill system.",
        "`Tools/UnrealMcpToolDocs/` contains generated reference pages for MCP tools and is intentionally outside the skill discovery path.",
        "",
        f"Generated tool count: {tool_count}",
        "",
    ]
    return "\n".join(lines)


def expected_files(tools: list[dict[str, Any]]) -> tuple[dict[Path, str], Counter[str], Counter[str]]:
    examples = collect_examples({tool["name"] for tool in tools})
    files: dict[Path, str] = {}
    provenance_counts: Counter[str] = Counter()
    category_counts: Counter[str] = Counter()
    for tool in sorted(tools, key=lambda item: (item["category"], item["name"])):
        content, provenance = render_tool(tool, examples)
        files[doc_path(tool)] = content
        provenance_counts[provenance] += 1
        category_counts[tool["category"]] += 1
    files[OUT / "README.md"] = render_readme(category_counts, len(tools))
    return files, provenance_counts, category_counts


def current_markdown_files() -> set[Path]:
    return {path for path in OUT.rglob("*.md") if path.is_file()} if OUT.exists() else set()


def write(files: dict[Path, str]) -> None:
    for category in CATEGORIES:
        (OUT / category).mkdir(parents=True, exist_ok=True)
    for path, content in files.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")


def check(files: dict[Path, str]) -> list[str]:
    errors: list[str] = []
    for path, content in files.items():
        if not path.exists():
            errors.append(f"missing: {rel(path)}")
        elif path.read_text(encoding="utf-8") != content:
            errors.append(f"stale: {rel(path)}")
    errors += [f"extra: {rel(path)}" for path in sorted(current_markdown_files() - set(files))]
    return errors


def print_summary(provenance_counts: Counter[str], check_mode: bool) -> None:
    total = provenance_counts["fixture-derived"] + provenance_counts["schema-minimal"]
    print(f"{'Checked' if check_mode else 'Generated'} {total} tool docs under {rel(OUT)}")
    print("Provenance distribution:")
    print(f"  fixture-derived: {provenance_counts['fixture-derived']}")
    print(f"  schema-minimal: {provenance_counts['schema-minimal']}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify generated files are up to date")
    args = parser.parse_args(argv)

    tools = load_tools()
    if len(tools) != 160:
        raise ValueError(f"Expected 160 registry tools, found {len(tools)}")
    files, provenance_counts, _category_counts = expected_files(tools)

    if args.check:
        errors = check(files)
        print_summary(provenance_counts, check_mode=True)
        if errors:
            print("--check failed:", file=sys.stderr)
            for error in errors:
                print(f"  {error}", file=sys.stderr)
            return 1
        print("--check passed: generated docs are up to date")
        return 0

    write(files)
    print_summary(provenance_counts, check_mode=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
