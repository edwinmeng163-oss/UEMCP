#!/usr/bin/env python3
"""Populate ToolRegistry inputSchema fields from C++ and a live tools/list dump."""

from __future__ import annotations

import argparse
import copy
import difflib
import json
import re
import shutil
import sys
from collections import OrderedDict
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
CANONICAL_REGISTRY = ROOT / "Tools/UnrealMcpToolRegistry/tools.json"
MIRROR_REGISTRY = ROOT / "Plugins/UnrealMcp/Resources/ToolRegistry/tools.json"
LIVE_DUMP = Path("/tmp/v0.24-tools-list-baseline.json")
STATIC_SOURCES = [
    ROOT / "Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpToolDefinitions.cpp",
    ROOT / "Plugins/UnrealMcp/Source/UnrealMcp/Private/UnrealMcpToolRegistrar.cpp",
]

BANNED_KEYWORDS = {"oneOf", "anyOf", "$ref", "allOf", "not"}
ALLOWED_TYPES = {"string", "integer", "number", "boolean", "array", "object", "null"}
EMPTY_SCHEMA = OrderedDict(
    [
        ("type", "object"),
        ("properties", OrderedDict()),
        ("required", []),
        ("additionalProperties", False),
    ]
)


@dataclass(frozen=True)
class SchemaError:
    tool_name: str
    path: str
    reason: str

    def format(self) -> str:
        return f'ERROR  tool={self.tool_name}  path={self.path}  reason="{self.reason}"'


@dataclass
class StaticRecord:
    schema: dict[str, Any]
    source_file: str
    line: int

    @property
    def provenance(self) -> str:
        return f"from-{self.source_file}:L{self.line}"


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle, object_pairs_hook=OrderedDict)


def dump_json_bytes(data: Any) -> bytes:
    return (json.dumps(data, indent=2, ensure_ascii=True) + "\n").encode("utf-8")


def extract_live_tools(path: Path) -> dict[str, dict[str, Any]]:
    if not path.exists():
        raise SystemExit(f"missing live tools/list dump: {path}")

    payload = load_json(path)
    tools = None
    if isinstance(payload, dict):
        if isinstance(payload.get("result"), dict):
            tools = payload["result"].get("tools")
        elif "tools" in payload:
            tools = payload.get("tools")
    elif isinstance(payload, list):
        tools = payload

    if not isinstance(tools, list):
        raise SystemExit(f"live tools/list dump has no tools array: {path}")

    result: dict[str, dict[str, Any]] = {}
    for tool in tools:
        if isinstance(tool, dict) and isinstance(tool.get("name"), str):
            result[tool["name"]] = tool
    return result


def cpp_text_value(expr: str) -> Any:
    expr = expr.strip()
    text_match = re.search(r'TEXT\("((?:\\.|[^"\\])*)"\)', expr)
    if text_match:
        return bytes(text_match.group(1), "utf-8").decode("unicode_escape")
    if expr in {"true", "false"}:
        return expr == "true"
    if expr in {"FString()", "FString"}:
        return None
    if expr == "GetHostBuildPlatformName()":
        return None
    number_match = re.fullmatch(r"-?\d+(?:\.\d+)?", expr)
    if number_match:
        return float(expr) if "." in expr else int(expr)
    return None


def split_cpp_args(arg_text: str) -> list[str]:
    args: list[str] = []
    start = 0
    depth = 0
    in_string = False
    escape = False
    for index, char in enumerate(arg_text):
        if in_string:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char in "({[":
            depth += 1
        elif char in ")}]":
            depth -= 1
        elif char == "," and depth == 0:
            args.append(arg_text[start:index].strip())
            start = index + 1
    tail = arg_text[start:].strip()
    if tail:
        args.append(tail)
    return args


def property_schema_from_expr(expr: str) -> OrderedDict[str, Any] | None:
    prop_type: str | None = None
    if "MakeStringArrayProperty" in expr:
        schema: OrderedDict[str, Any] = OrderedDict([("type", "array")])
        args = split_cpp_args(expr[expr.find("(") + 1 : expr.rfind(")")])
        if args:
            description = cpp_text_value(args[0])
            if description:
                schema["description"] = description
        schema["items"] = OrderedDict([("type", "string")])
        return schema
    if "MakeObjectArrayProperty" in expr:
        schema = OrderedDict([("type", "array")])
        args = split_cpp_args(expr[expr.find("(") + 1 : expr.rfind(")")])
        if args:
            description = cpp_text_value(args[0])
            if description:
                schema["description"] = description
        schema["items"] = OrderedDict([("type", "object"), ("properties", OrderedDict())])
        return schema
    if "MakeFlexibleObjectProperty" in expr:
        schema = OrderedDict([("type", "object")])
        args = split_cpp_args(expr[expr.find("(") + 1 : expr.rfind(")")])
        if args:
            description = cpp_text_value(args[0])
            if description:
                schema["description"] = description
        schema["properties"] = OrderedDict()
        return schema

    if "MakeEnumStringProperty" in expr or "MakeStringProperty" in expr:
        prop_type = "string"
    elif "MakeBoolProperty" in expr:
        prop_type = "boolean"
    elif "MakeNumberProperty" in expr:
        prop_type = "number"

    if not prop_type:
        return None

    open_paren = expr.find("(")
    close_paren = expr.rfind(")")
    args = split_cpp_args(expr[open_paren + 1 : close_paren]) if open_paren >= 0 and close_paren >= 0 else []
    schema = OrderedDict([("type", prop_type)])
    if args:
        description = cpp_text_value(args[0])
        if description:
            schema["description"] = description
    if len(args) >= 2:
        default_value = cpp_text_value(args[1])
        if default_value is not None:
            schema["default"] = default_value
    if "MakeEnumStringProperty" in expr:
        enum_values = re.findall(r'TEXT\("((?:\\.|[^"\\])*)"\)', expr)
        if len(enum_values) > 1:
            schema["enum"] = [bytes(value, "utf-8").decode("unicode_escape") for value in enum_values[1:]]
    return schema


def line_number_for_offset(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def brace_span(text: str, offset: int) -> tuple[int, int]:
    start = text.rfind("{", 0, offset)
    if start < 0:
        return max(0, text.rfind("\n", 0, offset)), min(len(text), text.find("\n", offset) + 1)

    depth = 0
    in_string = False
    escape = False
    for index in range(start, len(text)):
        char = text[index]
        if in_string:
            if escape:
                escape = False
            elif char == "\\":
                escape = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return start, index + 1
    return start, len(text)


def extract_static_schema_from_block(block: str) -> OrderedDict[str, Any]:
    properties: OrderedDict[str, Any] = OrderedDict()
    for match in re.finditer(r'->SetObjectField\(\s*TEXT\("([^"]+)"\)\s*,\s*([^;]+)\);', block, re.S):
        name, expr = match.group(1), match.group(2)
        if name == "properties":
            continue
        prop = property_schema_from_expr(expr)
        if prop is not None:
            properties[name] = prop

    required: list[str] = []
    for match in re.finditer(r'Required(?:Fields)?\.Add\([^;]*TEXT\("([^"]+)"\)[^;]*\);', block):
        if match.group(1) not in required:
            required.append(match.group(1))
    required_expr = re.search(r'MakeSchemaWithRequired\([^;]+TArray<FString>\s*\{([^}]+)\}', block, re.S)
    if required_expr:
        for field in re.findall(r'TEXT\("([^"]+)"\)', required_expr.group(1)):
            if field not in required:
                required.append(field)

    schema: OrderedDict[str, Any] = OrderedDict([("type", "object"), ("additionalProperties", False)])
    if properties:
        schema["properties"] = properties
    if required:
        schema["required"] = required
    return schema


def extract_static_records() -> dict[str, StaticRecord]:
    records: dict[str, StaticRecord] = {}
    for path in STATIC_SOURCES:
        text = path.read_text(encoding="utf-8")
        source_file = path.name
        for match in re.finditer(r'TEXT\("(unreal\.[^"]+)"\)', text):
            tool_name = match.group(1)
            start, end = brace_span(text, match.start())
            block = text[start:end]
            if "AddToolDefinition" not in block and "Registrar.Add" not in block:
                continue
            line = line_number_for_offset(text, match.start())
            schema = extract_static_schema_from_block(block)
            previous = records.get(tool_name)
            if previous is None or source_file == "UnrealMcpToolRegistrar.cpp":
                records[tool_name] = StaticRecord(schema=schema, source_file=source_file, line=line)
    return records


def normalize_schema(schema: dict[str, Any]) -> OrderedDict[str, Any]:
    normalized = copy.deepcopy(schema)
    if not isinstance(normalized, dict):
        normalized = {}
    normalized["type"] = "object"
    normalized.setdefault("properties", OrderedDict())
    normalized.setdefault("required", [])
    normalized["additionalProperties"] = False

    ordered = OrderedDict()
    for key in ("type", "additionalProperties", "properties", "required"):
        if key in normalized:
            ordered[key] = normalized[key]
    for key, value in normalized.items():
        if key not in ordered:
            ordered[key] = value
    return ordered


def canonical_compare(schema: dict[str, Any]) -> str:
    return json.dumps(schema, sort_keys=True, separators=(",", ":"))


def validate_inputSchema(schema: dict[str, Any], tool_name: str, exposure: str) -> list[SchemaError]:
    errors: list[SchemaError] = []

    def add(path: str, reason: str) -> None:
        errors.append(SchemaError(tool_name, path, reason))

    if not isinstance(schema, dict):
        return [SchemaError(tool_name, "/inputSchema", "schema must be an object")]

    if schema.get("type") != "object":
        add("/inputSchema/type", "root type must be object")
    if "properties" not in schema:
        add("/inputSchema", "missing required properties block")
    elif not isinstance(schema.get("properties"), dict):
        add("/inputSchema/properties", "properties must be an object")
    if "required" not in schema:
        add("/inputSchema", "missing required array")
    elif not isinstance(schema.get("required"), list):
        add("/inputSchema/required", "required must be an array")
    if exposure == "visible" and schema.get("additionalProperties") is not False:
        add("/inputSchema", "visible tool but additionalProperties not false")

    def walk(value: Any, path: str) -> None:
        if isinstance(value, dict):
            for key, child in value.items():
                child_path = f"{path}/{key}"
                if key in BANNED_KEYWORDS:
                    add(child_path, f"banned keyword {key}")
                if key == "type":
                    if isinstance(child, str):
                        if child not in ALLOWED_TYPES:
                            add(child_path, f"unsupported type {child}")
                    else:
                        add(child_path, "type must be a string")
                walk(child, child_path)
        elif isinstance(value, list):
            for index, child in enumerate(value):
                walk(child, f"{path}/{index}")

    walk(schema, "/inputSchema")
    return errors


def order_entry(entry: OrderedDict[str, Any], schema: OrderedDict[str, Any], legacy: bool) -> OrderedDict[str, Any]:
    result: OrderedDict[str, Any] = OrderedDict()
    inserted = False

    def insert_schema_fields() -> None:
        nonlocal inserted
        if inserted:
            return
        if legacy:
            result["schemaPolicy"] = "legacy"
        result["inputSchema"] = schema
        inserted = True

    for key, value in entry.items():
        if key in {"inputSchema", "schemaPolicy"}:
            continue
        if key in {"outputSchema", "notes"}:
            insert_schema_fields()
        result[key] = value
    insert_schema_fields()
    return result


def build_expected_registry(registry: OrderedDict[str, Any]) -> tuple[OrderedDict[str, Any], dict[str, str], dict[str, Any]]:
    live_tools = extract_live_tools(LIVE_DUMP)
    static_records = extract_static_records()
    expected = copy.deepcopy(registry)
    unresolved_visible: list[str] = []
    validation_errors: list[SchemaError] = []
    provenance: dict[str, str] = OrderedDict()
    mismatch_count = 0
    static_match_count = 0
    dump_count = 0
    legacy_count = 0

    for index, entry in enumerate(registry.get("tools", [])):
        tool_name = entry.get("name")
        exposure = entry.get("exposure", "")
        if not isinstance(tool_name, str):
            raise SystemExit(f"registry tool at index {index} has no string name")

        live_schema = None
        if tool_name in live_tools:
            live_schema = live_tools[tool_name].get("inputSchema")
        static_record = static_records.get(tool_name)

        if exposure == "visible":
            if not isinstance(live_schema, dict):
                unresolved_visible.append(tool_name)
                schema = normalize_schema(static_record.schema if static_record else {})
                source = static_record.provenance if static_record else "unresolved"
            else:
                schema = normalize_schema(live_schema)
                source = "from-tools-list-dump"
                dump_count += 1
                if static_record:
                    static_schema = normalize_schema(static_record.schema)
                    if canonical_compare(static_schema) == canonical_compare(schema):
                        source = static_record.provenance
                        static_match_count += 1
                        dump_count -= 1
                    else:
                        mismatch_count += 1
        else:
            schema = copy.deepcopy(EMPTY_SCHEMA)
            source = "empty-legacy-schema"
            legacy_count += 1

        validation_errors.extend(validate_inputSchema(schema, tool_name, exposure))
        provenance[tool_name] = source
        expected["tools"][index] = order_entry(entry, schema, exposure == "legacy_hidden")

    if unresolved_visible:
        for name in unresolved_visible:
            print(f"ERROR  visible tool has no resolved schema: {name}", file=sys.stderr)
        raise SystemExit(1)
    if validation_errors:
        for error in validation_errors:
            print(error.format(), file=sys.stderr)
        raise SystemExit(1)

    stats = {
        "tool_count": len(expected.get("tools", [])),
        "live_count": len(live_tools),
        "static_record_count": len(static_records),
        "static_match_count": static_match_count,
        "fixture_dump_count": dump_count,
        "legacy_count": legacy_count,
        "mismatch_count": mismatch_count,
        "validator_count": len(expected.get("tools", [])),
    }
    return expected, provenance, stats


def print_provenance(provenance: dict[str, str]) -> None:
    print("| tool | schema provenance |")
    print("|---|---|")
    for name, source in provenance.items():
        print(f"| {name} | {source} |")


def check_bytes(path: Path, expected_bytes: bytes) -> bool:
    if not path.exists():
        print(f"ERROR  missing file: {path}", file=sys.stderr)
        return False
    actual = path.read_bytes()
    if actual == expected_bytes:
        return True
    diff = difflib.unified_diff(
        actual.decode("utf-8").splitlines(),
        expected_bytes.decode("utf-8").splitlines(),
        fromfile=str(path),
        tofile=f"{path} (expected)",
        lineterm="",
    )
    print("\n".join(list(diff)[:200]), file=sys.stderr)
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify generated output matches both registry files")
    parser.add_argument("--provenance", action="store_true", help="print per-tool schema provenance")
    args = parser.parse_args()

    registry = load_json(CANONICAL_REGISTRY)
    expected, provenance, stats = build_expected_registry(registry)
    expected_bytes = dump_json_bytes(expected)

    if args.provenance:
        print_provenance(provenance)

    if args.check:
        ok = check_bytes(CANONICAL_REGISTRY, expected_bytes)
        ok = check_bytes(MIRROR_REGISTRY, expected_bytes) and ok
        if not ok:
            return 1
    else:
        CANONICAL_REGISTRY.write_bytes(expected_bytes)
        shutil.copyfile(CANONICAL_REGISTRY, MIRROR_REGISTRY)

    print(
        "schema extraction complete: "
        f"tools={stats['tool_count']} "
        f"live={stats['live_count']} "
        f"staticRecords={stats['static_record_count']} "
        f"staticMatches={stats['static_match_count']} "
        f"fixtureDerived={stats['fixture_dump_count']} "
        f"legacy={stats['legacy_count']} "
        f"s5S3Mismatches={stats['mismatch_count']} "
        f"validated={stats['validator_count']}"
    )
    if stats["mismatch_count"]:
        print(
            "WARNING  S5/S3 schema mismatches found; live tools/list dump was used for mismatched schemas: "
            f"{stats['mismatch_count']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
