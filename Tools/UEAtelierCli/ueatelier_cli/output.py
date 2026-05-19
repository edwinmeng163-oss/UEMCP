"""Output helpers for human and JSON CLI modes."""

from __future__ import annotations

import json
from typing import Any, Iterable, Sequence

import click


def emit_json(payload: Any) -> None:
    click.echo(json.dumps(payload, ensure_ascii=False, separators=(",", ":")))


def emit_pretty_json(payload: Any) -> None:
    click.echo(json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True))


def emit_key_values(rows: Sequence[tuple[str, Any]]) -> None:
    width = max((len(label) for label, _value in rows), default=0)
    for label, value in rows:
        click.echo(f"{label + ':':<{width + 1}} {value}")


def format_table(headers: Sequence[str], rows: Iterable[Sequence[Any]]) -> str:
    string_rows = [[str(cell) for cell in row] for row in rows]
    if not string_rows:
        return "(none)"

    widths = [len(header) for header in headers]
    for row in string_rows:
        for index, cell in enumerate(row):
            widths[index] = max(widths[index], len(cell))

    lines = []
    lines.append("  ".join(header.ljust(widths[index]) for index, header in enumerate(headers)))
    lines.append("  ".join("-" * width for width in widths))
    for row in string_rows:
        lines.append("  ".join(cell.ljust(widths[index]) for index, cell in enumerate(row)))
    return "\n".join(lines)


def shorten(text: Any, limit: int = 80) -> str:
    value = "" if text is None else str(text)
    if len(value) <= limit:
        return value
    return value[: max(0, limit - 3)] + "..."
