"""Click entry point for cli-anything-ueatelier."""

from __future__ import annotations

import json
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

import click

from . import __version__
from .mcp_client import (
    DEFAULT_ENDPOINT,
    EndpointUnreachableError,
    HttpStatusError,
    JsonRpcProtocolError,
    McpClient,
    is_tool_error,
    structured_content,
)
from .output import emit_json, emit_key_values, emit_pretty_json, format_table, shorten

TERMINAL_STATUSES = {"completed", "failed", "timed_out", "stale"}
POLL_SECONDS = 2.0


@dataclass
class CliState:
    client: McpClient
    json_output: bool
    http_timeout: float


def _state(ctx: click.Context) -> CliState:
    return ctx.ensure_object(CliState)


def _error_payload(kind: str, endpoint: str, hint: str, **extra: Any) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "error": kind,
        "endpoint": endpoint,
        "hint": hint,
    }
    payload.update(extra)
    return payload


def _exit_invalid_args(ctx: click.Context, message: str) -> None:
    state = _state(ctx)
    if state.json_output:
        emit_json(_error_payload("invalid_args", state.client.endpoint, message))
    else:
        click.echo(f"Invalid arguments: {message}")
    ctx.exit(2)


def _exit_wait_timeout(ctx: click.Context, run_id: str, last_report: Any) -> None:
    state = _state(ctx)
    payload = {
        "errorKind": "wait_timeout",
        "message": f"Timed out waiting for run {run_id}.",
        "runId": run_id,
        "lastReport": last_report,
    }
    if state.json_output:
        emit_json(payload)
    else:
        click.echo(payload["message"])
        if last_report is not None:
            emit_pretty_json(last_report)
    ctx.exit(3)


def _handle_client_error(ctx: click.Context, exc: Exception) -> None:
    state = _state(ctx)
    endpoint = getattr(exc, "endpoint", state.client.endpoint)
    if isinstance(exc, EndpointUnreachableError):
        hint = "Launch Unreal Editor with the UEAtelier plugin first."
        if state.json_output:
            emit_json(_error_payload("endpoint_unreachable", endpoint, hint))
        else:
            click.echo("MCP endpoint not reachable. Launch Unreal Editor with the UEAtelier plugin first.")
        ctx.exit(2)

    if isinstance(exc, HttpStatusError):
        hint = "The MCP endpoint returned an HTTP error."
        if state.json_output:
            emit_json(
                _error_payload(
                    "http_error",
                    endpoint,
                    hint,
                    statusCode=exc.status_code,
                    body=exc.body,
                )
            )
        else:
            click.echo(f"MCP endpoint returned HTTP {exc.status_code}: {exc.body}")
        ctx.exit(4)

    if isinstance(exc, JsonRpcProtocolError):
        hint = "The MCP endpoint returned a JSON-RPC error or malformed response."
        payload = getattr(exc, "payload", None)
        if state.json_output:
            emit_json(_error_payload("tool_error", endpoint, hint, details=payload))
        else:
            click.echo(f"MCP protocol error: {exc.message}")
            if payload is not None:
                emit_pretty_json(payload)
        ctx.exit(3)

    raise exc


def _client_call(ctx: click.Context, call: Callable[[], Any]) -> Any:
    try:
        return call()
    except Exception as exc:  # noqa: BLE001 - converted to CLI exits above.
        _handle_client_error(ctx, exc)
        raise AssertionError("unreachable")


def _normalize_tool_name(name: str) -> str:
    if name.startswith("unreal."):
        return name
    return f"unreal.{name}"


def _extract_tool_payload(ctx: click.Context, result: dict[str, Any]) -> Any:
    payload = structured_content(result)
    if is_tool_error(result):
        if payload is None:
            payload = {"errorKind": "tool_error", "message": "Tool returned isError=true."}
        if _state(ctx).json_output:
            emit_json(payload)
        else:
            emit_pretty_json(payload)
        ctx.exit(3)
    return payload if payload is not None else {}


def _call_tool_payload(ctx: click.Context, tool_name: str, arguments: dict[str, Any] | None = None) -> Any:
    state = _state(ctx)
    result = _client_call(ctx, lambda: state.client.call_tool(tool_name, arguments or {}))
    return _extract_tool_payload(ctx, result)


def _emit_payload(ctx: click.Context, payload: Any) -> None:
    if _state(ctx).json_output:
        emit_json(payload)
    else:
        emit_pretty_json(payload)


def _parse_json_object(ctx: click.Context, source: str, value: str) -> dict[str, Any]:
    try:
        payload = json.loads(value)
    except json.JSONDecodeError as exc:
        _exit_invalid_args(ctx, f"{source} must contain valid JSON: {exc.msg}")
    if not isinstance(payload, dict):
        _exit_invalid_args(ctx, f"{source} must decode to a JSON object")
    return payload


def _parse_csv(value: str | None) -> list[str]:
    if not value:
        return []
    return [item.strip() for item in value.split(",") if item.strip()]


def _tool_display_rows(tools: list[dict[str, Any]]) -> list[tuple[str, str, str]]:
    rows = []
    for tool in tools:
        rows.append(
            (
                str(tool.get("name", "")),
                str(tool.get("category", "")),
                shorten(tool.get("title") or tool.get("description"), 72),
            )
        )
    return rows


def _filter_tools(
    tools: list[dict[str, Any]],
    category: str | None,
    search: str | None,
    limit: int | None,
) -> list[dict[str, Any]]:
    filtered = tools
    if category:
        category_lower = category.lower()
        filtered = [tool for tool in filtered if str(tool.get("category", "")).lower() == category_lower]
    if search:
        needle = search.lower()
        filtered = [
            tool
            for tool in filtered
            if needle
            in " ".join(
                [
                    str(tool.get("name", "")),
                    str(tool.get("title", "")),
                    str(tool.get("description", "")),
                    str(tool.get("category", "")),
                ]
            ).lower()
        ]
    if limit is not None:
        filtered = filtered[:limit]
    return filtered


def _find_tool(tools: list[dict[str, Any]], name: str) -> dict[str, Any] | None:
    candidates = {name, _normalize_tool_name(name)}
    for tool in tools:
        if tool.get("name") in candidates:
            return tool
    return None


def _extract_editor_version(payload: Any) -> str:
    if not isinstance(payload, dict):
        return "unknown"
    aliases = {"unrealversion", "editorversion", "engineversion", "version", "version_string"}
    for key, value in payload.items():
        if key.lower() in aliases and value not in (None, ""):
            return str(value)
    return "unknown"


def _report_status(payload: Any) -> str:
    if isinstance(payload, dict):
        return str(payload.get("status", "")).lower()
    return ""


def _wait_for_report(ctx: click.Context, run_id: str, deadline: float, wait: bool) -> Any:
    last_report = None
    while True:
        last_report = _call_tool_payload(ctx, "unreal.automation_report", {"runId": run_id})
        if not wait or _report_status(last_report) in TERMINAL_STATUSES:
            return last_report

        remaining = deadline - time.monotonic()
        if remaining <= 0:
            _exit_wait_timeout(ctx, run_id, last_report)
        time.sleep(min(POLL_SECONDS, remaining))


@click.group(context_settings={"help_option_names": ["-h", "--help"]})
@click.option("--json", "json_output", is_flag=True, help="Machine-readable JSON output.")
@click.option("--endpoint", help=f"Override MCP endpoint. Defaults to env UEATELIER_MCP_ENDPOINT or {DEFAULT_ENDPOINT}.")
@click.option("--http-timeout", type=float, default=120.0, show_default=True, help="HTTP request timeout in seconds.")
@click.version_option(__version__, prog_name="cli-anything-ueatelier")
@click.pass_context
def main(ctx: click.Context, json_output: bool, endpoint: str | None, http_timeout: float) -> None:
    """CLI-Anything wrapper for UEAtelier MCP."""

    ctx.obj = CliState(client=McpClient(endpoint=endpoint, timeout=http_timeout), json_output=json_output, http_timeout=http_timeout)


@main.command()
@click.pass_context
def status(ctx: click.Context) -> None:
    """Verify editor and plugin availability."""

    state = _state(ctx)
    tools = _client_call(ctx, state.client.list_tools)
    tool_count = len(tools)
    tool_names = {str(tool.get("name", "")) for tool in tools}
    editor_version = "unknown"
    project_name = None
    current_map = None
    is_play_in_editor = None
    editor_status_succeeded = False

    if "unreal.editor_status" in tool_names:
        status_result = _client_call(ctx, lambda: state.client.call_tool("unreal.editor_status", {}))
        if not is_tool_error(status_result):
            editor_status_succeeded = True
            status_payload = structured_content(status_result)
            editor_version = _extract_editor_version(status_payload)
            if isinstance(status_payload, dict):
                if status_payload.get("projectName") not in (None, ""):
                    project_name = status_payload.get("projectName")
                if status_payload.get("currentMap") not in (None, ""):
                    current_map = status_payload.get("currentMap")
                if isinstance(status_payload.get("isPlayInEditor"), bool):
                    is_play_in_editor = status_payload.get("isPlayInEditor")
    if not editor_status_succeeded and "unreal.editor.engine_version" in tool_names:
        engine_result = _client_call(ctx, lambda: state.client.call_tool("unreal.editor.engine_version", {}))
        if not is_tool_error(engine_result):
            editor_version = _extract_editor_version(structured_content(engine_result))

    payload = {
        "endpoint": state.client.endpoint,
        "connected": True,
        "editorVersion": editor_version,
        "toolCount": tool_count,
    }
    if project_name is not None:
        payload["projectName"] = project_name
    if current_map is not None:
        payload["currentMap"] = current_map
    if is_play_in_editor is not None:
        payload["isPlayInEditor"] = is_play_in_editor
    if state.json_output:
        emit_json(payload)
    else:
        rows = [
            ("UEAtelier MCP server", state.client.endpoint),
            ("Status", "connected"),
            ("Editor version", editor_version),
        ]
        if project_name is not None:
            rows.append(("Project name", project_name))
        if current_map is not None:
            rows.append(("Current map", current_map))
        if is_play_in_editor is not None:
            rows.append(("PIE", "true" if is_play_in_editor else "false"))
        rows.append(("Tool count", tool_count))
        emit_key_values(
            rows
        )


@main.group()
def tools() -> None:
    """Inspect available MCP tools."""


@tools.command("list")
@click.option("--category", help="Filter by tool category.")
@click.option("--search", help="Case-insensitive search over name, title, description, and category.")
@click.option("--limit", type=int, help="Maximum tools to return.")
@click.pass_context
def tools_list(ctx: click.Context, category: str | None, search: str | None, limit: int | None) -> None:
    """List MCP tools."""

    if limit is not None and limit < 1:
        _exit_invalid_args(ctx, "--limit must be greater than zero")
    all_tools = _client_call(ctx, _state(ctx).client.list_tools)
    filtered = _filter_tools(all_tools, category, search, limit)
    if _state(ctx).json_output:
        emit_json(filtered)
    else:
        click.echo(format_table(("Name", "Category", "Title"), _tool_display_rows(filtered)))


@tools.command("describe")
@click.argument("name")
@click.pass_context
def tools_describe(ctx: click.Context, name: str) -> None:
    """Show a full MCP tool schema and description."""

    all_tools = _client_call(ctx, _state(ctx).client.list_tools)
    tool = _find_tool(all_tools, name)
    if tool is None:
        _exit_invalid_args(ctx, f"Tool not found: {name}")
    _emit_payload(ctx, tool)


@main.command()
@click.argument("name")
@click.option("--args-json", help="JSON object to pass as tool arguments.")
@click.option("--args-file", type=click.Path(exists=True, dir_okay=False, readable=True), help="Path to a JSON arguments file.")
@click.pass_context
def run(ctx: click.Context, name: str, args_json: str | None, args_file: str | None) -> None:
    """Run any MCP tool."""

    if args_json and args_file:
        _exit_invalid_args(ctx, "Use either --args-json or --args-file, not both")
    if args_file:
        try:
            value = Path(args_file).read_text(encoding="utf-8")
        except OSError as exc:
            _exit_invalid_args(ctx, f"Could not read --args-file: {exc}")
        arguments = _parse_json_object(ctx, "--args-file", value)
    elif args_json:
        arguments = _parse_json_object(ctx, "--args-json", args_json)
    else:
        arguments = {}

    payload = _call_tool_payload(ctx, _normalize_tool_name(name), arguments)
    _emit_payload(ctx, payload)


@main.command()
@click.option("--since", help="ISO-8601 UTC lower bound.")
@click.option("--classes", "classes_text", help="Comma-separated diagnostic classes.")
@click.option("--limit", type=int, default=200, show_default=True, help="Maximum diagnostics entries.")
@click.pass_context
def diagnostics(ctx: click.Context, since: str | None, classes_text: str | None, limit: int) -> None:
    """Read editor diagnostics."""

    if limit < 1:
        _exit_invalid_args(ctx, "--limit must be greater than zero")
    arguments: dict[str, Any] = {"limit": limit}
    if since:
        arguments["since"] = since
    classes = _parse_csv(classes_text)
    if classes:
        arguments["classes"] = classes
    payload = _call_tool_payload(ctx, "unreal.editor_diagnostics", arguments)
    _emit_payload(ctx, payload)


@main.group()
def automation() -> None:
    """Work with Unreal Automation Framework tests."""


@automation.command("list")
@click.option("--filter", "filter_text", help="Substring filter for fullName or displayName.")
@click.option("--details", is_flag=True, help="Include category and requirement hints.")
@click.option("--limit", type=int, default=200, show_default=True, help="Maximum tests to return.")
@click.pass_context
def automation_list(ctx: click.Context, filter_text: str | None, details: bool, limit: int) -> None:
    """List automation tests."""

    if limit < 1:
        _exit_invalid_args(ctx, "--limit must be greater than zero")
    arguments: dict[str, Any] = {"includeDetails": details, "limit": limit}
    if filter_text:
        arguments["filter"] = filter_text
    payload = _call_tool_payload(ctx, "unreal.automation_list", arguments)
    _emit_payload(ctx, payload)


@automation.command("run")
@click.argument("name")
@click.option("--tool-timeout", type=float, default=120.0, show_default=True, help="Per-tool timeoutSeconds argument.")
@click.option("--tags", "tags_text", help="Comma-separated caller metadata tags.")
@click.pass_context
def automation_run(ctx: click.Context, name: str, tool_timeout: float, tags_text: str | None) -> None:
    """Queue one automation test."""

    arguments: dict[str, Any] = {"fullName": name, "timeoutSeconds": tool_timeout}
    tags = _parse_csv(tags_text)
    if tags:
        arguments["tags"] = tags
    payload = _call_tool_payload(ctx, "unreal.automation_run", arguments)
    _emit_payload(ctx, payload)


@automation.command("report")
@click.argument("run_id")
@click.option("--wait", is_flag=True, help="Poll every 2 seconds until terminal status.")
@click.pass_context
def automation_report(ctx: click.Context, run_id: str, wait: bool) -> None:
    """Read an automation run report."""

    deadline = time.monotonic() + _state(ctx).http_timeout
    payload = _wait_for_report(ctx, run_id, deadline, wait)
    _emit_payload(ctx, payload)


@main.command("pie-smoke")
@click.option("--map", "map_path", help="Optional /Game/... UWorld path to open before PIE.")
@click.option("--tool-timeout", type=float, default=60.0, show_default=True, help="Per-tool timeoutSeconds argument.")
@click.option("--alive-window", type=float, default=5.0, show_default=True, help="PIE aliveWindowSeconds argument.")
@click.pass_context
def pie_smoke(ctx: click.Context, map_path: str | None, tool_timeout: float, alive_window: float) -> None:
    """Run PIE smoke and print the final report."""

    arguments: dict[str, Any] = {
        "timeoutSeconds": tool_timeout,
        "aliveWindowSeconds": alive_window,
    }
    if map_path:
        arguments["mapPath"] = map_path
    queued = _call_tool_payload(ctx, "unreal.pie_smoke", arguments)
    if not isinstance(queued, dict) or not queued.get("runId"):
        _exit_wait_timeout(ctx, "unknown", queued)
    run_id = str(queued["runId"])
    deadline = time.monotonic() + _state(ctx).http_timeout
    payload = _wait_for_report(ctx, run_id, deadline, True)
    _emit_payload(ctx, payload)


@main.group()
def tasks() -> None:
    """Work with Task Atlas records."""


@tasks.command("list")
@click.option("--filter", "filter_text", default="all", show_default=True, help="all, success, failed, unrated, or pinned.")
@click.option("--limit", type=int, default=50, show_default=True, help="Maximum tasks to return.")
@click.pass_context
def tasks_list(ctx: click.Context, filter_text: str, limit: int) -> None:
    """List Task Atlas tasks."""

    if filter_text not in {"all", "success", "failed", "unrated", "pinned"}:
        _exit_invalid_args(ctx, "--filter must be one of all, success, failed, unrated, pinned")
    if limit < 1:
        _exit_invalid_args(ctx, "--limit must be greater than zero")
    payload = _call_tool_payload(ctx, "unreal.task_list", {"filter": filter_text, "limit": limit})
    _emit_payload(ctx, payload)


@tasks.command("describe")
@click.argument("task_id")
@click.pass_context
def tasks_describe(ctx: click.Context, task_id: str) -> None:
    """Describe one Task Atlas task."""

    payload = _call_tool_payload(ctx, "unreal.task_describe", {"taskId": task_id})
    _emit_payload(ctx, payload)


@tasks.command("rate")
@click.argument("task_id")
@click.argument("rating")
@click.pass_context
def tasks_rate(ctx: click.Context, task_id: str, rating: str) -> None:
    """Rate one Task Atlas task."""

    if rating not in {"success", "failed", "unrated"}:
        _exit_invalid_args(ctx, "rating must be one of success, failed, unrated")
    payload = _call_tool_payload(ctx, "unreal.task_rate", {"taskId": task_id, "rating": rating})
    _emit_payload(ctx, payload)


@tasks.command("pin")
@click.argument("task_id")
@click.option("--unpin", is_flag=True, help="Unpin instead of pinning.")
@click.pass_context
def tasks_pin(ctx: click.Context, task_id: str, unpin: bool) -> None:
    """Pin or unpin one Task Atlas task."""

    payload = _call_tool_payload(ctx, "unreal.task_pin", {"taskId": task_id, "pinned": not unpin})
    _emit_payload(ctx, payload)
