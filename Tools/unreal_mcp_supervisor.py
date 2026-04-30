#!/usr/bin/env python3
"""External supervisor for Unreal MCP editor restart handoff.

This script intentionally lives outside Unreal Editor so it can close/reopen the
editor and then continue MCP extension tests after the plugin DLL/dylib reloads.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
import urllib.error
import urllib.request


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_UPROJECT = PROJECT_ROOT / "MyProject.uproject"
DEFAULT_URL = os.environ.get("UNREAL_MCP_URL", "http://127.0.0.1:8765/mcp")
DEFAULT_PROTOCOL = os.environ.get("UNREAL_MCP_PROTOCOL_VERSION", "2025-06-18")
AUTH_TOKEN = os.environ.get("UNREAL_MCP_AUTH_TOKEN", "")


def log(message: str) -> None:
    print(message, file=sys.stderr, flush=True)


def headers() -> dict[str, str]:
    result = {
        "Content-Type": "application/json",
        "Accept": "application/json, text/event-stream",
        "MCP-Protocol-Version": DEFAULT_PROTOCOL,
    }
    if AUTH_TOKEN:
        result["Authorization"] = f"Bearer {AUTH_TOKEN}"
    return result


def rpc(url: str, method: str, params: dict | None = None, timeout: float = 30.0) -> dict:
    payload = {
        "jsonrpc": "2.0",
        "id": int(time.time() * 1000) % 1_000_000,
        "method": method,
    }
    if params is not None:
        payload["params"] = params

    request = urllib.request.Request(
        url,
        data=json.dumps(payload, separators=(",", ":")).encode("utf-8"),
        headers=headers(),
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8", errors="replace"))


def call_tool(url: str, name: str, arguments: dict, timeout: float = 120.0) -> dict:
    return rpc(url, "tools/call", {"name": name, "arguments": arguments}, timeout=timeout)


def wait_endpoint(url: str, timeout: float) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            response = rpc(url, "tools/list", {}, timeout=2.0)
            if "result" in response:
                return True
        except Exception:
            time.sleep(1.0)
    return False


def find_editor_pids(uproject: Path) -> list[int]:
    if sys.platform == "win32":
        return []

    pattern = f"UnrealEditor.*{uproject}"
    try:
        completed = subprocess.run(
            ["pgrep", "-f", pattern],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
    except FileNotFoundError:
        return []

    pids: list[int] = []
    for line in completed.stdout.splitlines():
        try:
            pids.append(int(line.strip()))
        except ValueError:
            pass
    return pids


def start_editor(uproject: Path, editor_cmd: str | None) -> None:
    if editor_cmd:
        subprocess.Popen([editor_cmd, str(uproject)])
        return

    if sys.platform == "darwin":
        subprocess.Popen(["open", str(uproject)])
    elif sys.platform == "win32":
        os.startfile(str(uproject))  # type: ignore[attr-defined]
    else:
        subprocess.Popen(["UnrealEditor", str(uproject)])


def stop_editor(uproject: Path, timeout: float) -> None:
    if sys.platform == "darwin":
        subprocess.run(
            ["osascript", "-e", 'tell application "UnrealEditor" to quit'],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    deadline = time.time() + timeout
    while time.time() < deadline:
        pids = find_editor_pids(uproject)
        if not pids:
            return
        time.sleep(1.0)

    for pid in find_editor_pids(uproject):
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass


def print_json(data: dict) -> None:
    print(json.dumps(data, indent=2, ensure_ascii=False))


def command_wait(args: argparse.Namespace) -> int:
    if wait_endpoint(args.url, args.timeout):
        log("Unreal MCP endpoint is ready.")
        return 0
    log("Timed out waiting for Unreal MCP endpoint.")
    return 1


def command_restart(args: argparse.Namespace) -> int:
    uproject = Path(args.uproject).expanduser().resolve()
    log(f"Stopping Unreal Editor for {uproject}")
    stop_editor(uproject, args.stop_timeout)
    log(f"Starting Unreal Editor for {uproject}")
    start_editor(uproject, args.editor_cmd)
    return command_wait(args)


def command_resume_test(args: argparse.Namespace) -> int:
    if not wait_endpoint(args.url, args.timeout):
        log("Endpoint is not ready; cannot resume test.")
        return 1

    response = call_tool(
        args.url,
        "unreal.mcp_extension_pipeline" if args.pipeline else "unreal.mcp_run_tool_test",
        {
            "mode": "resume_test",
            "memoryKey": args.memory_key,
            "readProjectMemory": True,
            "writeProjectMemory": True,
        },
        timeout=args.call_timeout,
    )
    print_json(response)
    return 1 if response.get("result", {}).get("isError") else 0


def command_pipeline(args: argparse.Namespace) -> int:
    if not wait_endpoint(args.url, args.timeout):
        log("Endpoint is not ready; cannot run pipeline.")
        return 1

    try:
        pipeline_args = json.loads(args.args_json)
    except json.JSONDecodeError as exc:
        log(f"--args-json is not valid JSON: {exc}")
        return 1

    response = call_tool(args.url, "unreal.mcp_extension_pipeline", pipeline_args, timeout=args.call_timeout)
    print_json(response)

    structured = response.get("result", {}).get("structuredContent", {})
    if not args.auto_restart or not structured.get("requiresRestart"):
        return 1 if response.get("result", {}).get("isError") else 0

    log("Pipeline requires restart; restarting editor and resuming test.")
    restart_args = argparse.Namespace(**vars(args))
    command_restart(restart_args)

    memory_key = pipeline_args.get("memoryKey") or structured.get("memoryKey") or "mcp.extension.pipeline"
    resume_args = argparse.Namespace(**vars(args))
    resume_args.memory_key = memory_key
    resume_args.pipeline = True
    return command_resume_test(resume_args)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default=DEFAULT_URL, help="Unreal MCP endpoint URL.")
    parser.add_argument("--uproject", default=str(DEFAULT_UPROJECT), help="Path to the .uproject file.")
    parser.add_argument("--editor-cmd", default="", help="Optional UnrealEditor executable path.")
    parser.add_argument("--timeout", type=float, default=120.0, help="Endpoint wait timeout in seconds.")
    parser.add_argument("--call-timeout", type=float, default=180.0, help="MCP tool call timeout in seconds.")
    parser.add_argument("--stop-timeout", type=float, default=30.0, help="Graceful editor stop timeout in seconds.")

    subparsers = parser.add_subparsers(dest="command", required=True)

    wait_parser = subparsers.add_parser("wait", help="Wait until the MCP endpoint is ready.")
    wait_parser.set_defaults(func=command_wait)

    restart_parser = subparsers.add_parser("restart", help="Restart Unreal Editor and wait for MCP.")
    restart_parser.set_defaults(func=command_restart)

    resume_parser = subparsers.add_parser("resume-test", help="Resume post-restart MCP tool testing from project memory.")
    resume_parser.add_argument("--memory-key", default="mcp.extension.pipeline")
    resume_parser.add_argument("--pipeline", action="store_true", help="Use mcp_extension_pipeline mode=resume_test instead of mcp_run_tool_test.")
    resume_parser.set_defaults(func=command_resume_test)

    pipeline_parser = subparsers.add_parser("pipeline", help="Run mcp_extension_pipeline, optionally restart and resume.")
    pipeline_parser.add_argument("--args-json", required=True, help="JSON object passed to unreal.mcp_extension_pipeline.")
    pipeline_parser.add_argument("--auto-restart", action="store_true", help="Restart Unreal Editor if the pipeline requires it, then resume test.")
    pipeline_parser.set_defaults(func=command_pipeline)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
