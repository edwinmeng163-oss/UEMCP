from __future__ import annotations

import json
from pathlib import Path

import pytest

from ueatelier_cli import cli as cli_module
from ueatelier_cli.cli import main
from ueatelier_cli.mcp_client import EndpointUnreachableError


class FakeClient:
    def __init__(self, tools=None, responses=None, list_error=None):
        self.tools = tools or []
        self.responses = responses or {}
        self.list_error = list_error
        self.calls = []
        self.endpoint = "http://127.0.0.1:8765/mcp"
        self.timeout = 120

    def list_tools(self):
        if self.list_error:
            raise self.list_error
        return self.tools

    def call_tool(self, name, arguments=None):
        arguments = arguments or {}
        self.calls.append((name, arguments))
        value = self.responses.get(name, {"structuredContent": {"tool": name, "arguments": arguments}})
        if isinstance(value, list):
            if not value:
                raise AssertionError(f"No fake responses left for {name}")
            value = value.pop(0)
        if isinstance(value, Exception):
            raise value
        return value


@pytest.fixture
def install_fake_client(monkeypatch):
    def install(fake):
        def factory(endpoint=None, timeout=120):
            fake.endpoint = endpoint or "http://127.0.0.1:8765/mcp"
            fake.timeout = timeout
            return fake

        monkeypatch.setattr(cli_module, "McpClient", factory)
        return fake

    return install


def decode_json(result):
    assert result.output
    return json.loads(result.output)


def test_status_json_normalizes_payload(runner, install_fake_client):
    fake = FakeClient(
        tools=[{"name": "unreal.editor.engine_version"}, {"name": "unreal.editor_status"}],
        responses={
            "unreal.editor_status": {
                "structuredContent": {
                    "projectName": "MyProject",
                    "engineVersion": "5.7.4",
                    "currentMap": "/Game/Maps/Main",
                    "isPlayInEditor": False,
                    "projectDir": "/abs/path",
                    "isSimulatingInEditor": False,
                    "playRequestPending": False,
                    "selectedAssetCount": 0,
                    "selectedActorCount": 0,
                    "endpoint": "http://127.0.0.1:8765/mcp",
                }
            },
        },
    )
    install_fake_client(fake)

    result = runner.invoke(main, ["--json", "--endpoint", "http://editor.test/mcp", "status"])

    assert result.exit_code == 0
    assert decode_json(result) == {
        "endpoint": "http://editor.test/mcp",
        "connected": True,
        "editorVersion": "5.7.4",
        "toolCount": 2,
        "projectName": "MyProject",
        "currentMap": "/Game/Maps/Main",
        "isPlayInEditor": False,
    }
    assert fake.calls == [("unreal.editor_status", {})]


def test_status_falls_back_to_engine_version_when_editor_status_isError(runner, install_fake_client):
    fake = FakeClient(
        tools=[{"name": "unreal.editor.engine_version"}, {"name": "unreal.editor_status"}],
        responses={
            "unreal.editor_status": {"isError": True, "structuredContent": {"errorKind": "x"}},
            "unreal.editor.engine_version": {
                "structuredContent": {"version_string": "5.7.4", "major": 5, "minor": 7, "patch": 4}
            },
        },
    )
    install_fake_client(fake)

    result = runner.invoke(main, ["--json", "--endpoint", "http://editor.test/mcp", "status"])

    assert result.exit_code == 0
    assert decode_json(result) == {
        "endpoint": "http://editor.test/mcp",
        "connected": True,
        "editorVersion": "5.7.4",
        "toolCount": 2,
    }
    assert fake.calls == [("unreal.editor_status", {}), ("unreal.editor.engine_version", {})]


def test_status_unknown_when_both_tools_iserror(runner, install_fake_client):
    fake = FakeClient(
        tools=[{"name": "unreal.editor.engine_version"}, {"name": "unreal.editor_status"}],
        responses={
            "unreal.editor_status": {"isError": True, "structuredContent": {"errorKind": "status_failed"}},
            "unreal.editor.engine_version": {"isError": True, "structuredContent": {"errorKind": "version_failed"}},
        },
    )
    install_fake_client(fake)

    result = runner.invoke(main, ["--json", "--endpoint", "http://editor.test/mcp", "status"])

    assert result.exit_code == 0
    assert decode_json(result) == {
        "endpoint": "http://editor.test/mcp",
        "connected": True,
        "editorVersion": "unknown",
        "toolCount": 2,
    }
    assert fake.calls == [("unreal.editor_status", {}), ("unreal.editor.engine_version", {})]


def test_status_endpoint_unreachable_during_editor_status_exits_2(runner, install_fake_client):
    fake = FakeClient(
        tools=[{"name": "unreal.editor.engine_version"}, {"name": "unreal.editor_status"}],
        responses={
            "unreal.editor_status": EndpointUnreachableError("refused", endpoint="http://editor.test/mcp"),
            "unreal.editor.engine_version": {"structuredContent": {"version_string": "5.7.4"}},
        },
    )
    install_fake_client(fake)

    result = runner.invoke(main, ["--json", "--endpoint", "http://editor.test/mcp", "status"])

    assert result.exit_code == 2
    assert decode_json(result)["error"] == "endpoint_unreachable"
    assert fake.calls == [("unreal.editor_status", {})]


def test_status_omits_currentmap_when_empty(runner, install_fake_client):
    fake = FakeClient(
        tools=[{"name": "unreal.editor_status"}],
        responses={
            "unreal.editor_status": {
                "structuredContent": {
                    "projectName": "MyProject",
                    "engineVersion": "5.7.4",
                    "currentMap": "",
                    "isPlayInEditor": False,
                }
            },
        },
    )
    install_fake_client(fake)

    result = runner.invoke(main, ["--json", "--endpoint", "http://editor.test/mcp", "status"])

    assert result.exit_code == 0
    payload = decode_json(result)
    assert payload["projectName"] == "MyProject"
    assert payload["editorVersion"] == "5.7.4"
    assert payload["isPlayInEditor"] is False
    assert "currentMap" not in payload


def test_status_unreachable_json_exits_2(runner, install_fake_client):
    fake = FakeClient(list_error=EndpointUnreachableError("refused", endpoint="http://dead.test/mcp"))
    install_fake_client(fake)

    result = runner.invoke(main, ["--json", "--endpoint", "http://dead.test/mcp", "status"])

    assert result.exit_code == 2
    assert decode_json(result)["error"] == "endpoint_unreachable"
    assert "Traceback" not in result.output


def test_tools_list_filters_and_outputs_json(runner, install_fake_client):
    fake = FakeClient(
        tools=[
            {"name": "unreal.automation_list", "category": "verification", "title": "List Automation Tests"},
            {"name": "unreal.task_list", "category": "task-atlas", "title": "List Tasks"},
        ]
    )
    install_fake_client(fake)

    result = runner.invoke(main, ["--json", "tools", "list", "--category", "verification", "--search", "automation", "--limit", "1"])

    assert result.exit_code == 0
    assert decode_json(result) == [{"name": "unreal.automation_list", "category": "verification", "title": "List Automation Tests"}]


def test_tools_describe_accepts_short_name(runner, install_fake_client):
    fake = FakeClient(tools=[{"name": "unreal.editor_status", "inputSchema": {"type": "object"}}])
    install_fake_client(fake)

    result = runner.invoke(main, ["--json", "tools", "describe", "editor_status"])

    assert result.exit_code == 0
    assert decode_json(result) == {"name": "unreal.editor_status", "inputSchema": {"type": "object"}}


def test_run_maps_json_args_and_normalizes_tool_name(runner, install_fake_client):
    fake = FakeClient()
    install_fake_client(fake)

    result = runner.invoke(main, ["--json", "run", "editor_status", "--args-json", '{"verbose": true}'])

    assert result.exit_code == 0
    assert fake.calls == [("unreal.editor_status", {"verbose": True})]
    assert decode_json(result)["arguments"] == {"verbose": True}


def test_run_rejects_args_json_and_file_without_traceback(runner, install_fake_client):
    fake = FakeClient()
    install_fake_client(fake)
    with runner.isolated_filesystem():
        Path("args.json").write_text("{}", encoding="utf-8")
        result = runner.invoke(main, ["--json", "run", "editor_status", "--args-json", "{}", "--args-file", "args.json"])

    assert result.exit_code == 2
    assert decode_json(result)["error"] == "invalid_args"
    assert "Traceback" not in result.output


def test_run_rejects_invalid_json_without_traceback(runner, install_fake_client):
    fake = FakeClient()
    install_fake_client(fake)

    result = runner.invoke(main, ["--json", "run", "editor_status", "--args-json", "{bad"])

    assert result.exit_code == 2
    assert decode_json(result)["error"] == "invalid_args"
    assert "Traceback" not in result.output


def test_tool_error_exits_3_and_emits_structured_payload(runner, install_fake_client):
    fake = FakeClient(
        responses={
            "unreal.editor_status": {
                "isError": True,
                "structuredContent": {"errorKind": "editor_state", "message": "not ready"},
            }
        }
    )
    install_fake_client(fake)

    result = runner.invoke(main, ["--json", "run", "unreal.editor_status"])

    assert result.exit_code == 3
    assert decode_json(result) == {"errorKind": "editor_state", "message": "not ready"}


def test_diagnostics_maps_arguments(runner, install_fake_client):
    fake = FakeClient()
    install_fake_client(fake)

    result = runner.invoke(main, ["--json", "diagnostics", "--since", "2026-05-19T00:00:00Z", "--classes", "compile,log_error", "--limit", "5"])

    assert result.exit_code == 0
    assert fake.calls == [
        (
            "unreal.editor_diagnostics",
            {"limit": 5, "since": "2026-05-19T00:00:00Z", "classes": ["compile", "log_error"]},
        )
    ]


def test_automation_commands_map_arguments(runner, install_fake_client, monkeypatch):
    fake = FakeClient(
        responses={
            "unreal.automation_report": [
                {"structuredContent": {"runId": "r1", "status": "running"}},
                {"structuredContent": {"runId": "r1", "status": "completed"}},
            ]
        }
    )
    install_fake_client(fake)
    monkeypatch.setattr(cli_module.time, "sleep", lambda _seconds: None)

    list_result = runner.invoke(main, ["--json", "automation", "list", "--filter", "Mcp", "--details", "--limit", "3"])
    run_result = runner.invoke(main, ["--json", "automation", "run", "Project.Tests.One", "--tool-timeout", "9", "--tags", "cli,smoke"])
    report_result = runner.invoke(main, ["--json", "--http-timeout", "30", "automation", "report", "r1", "--wait"])

    assert list_result.exit_code == 0
    assert run_result.exit_code == 0
    assert report_result.exit_code == 0
    assert ("unreal.automation_list", {"includeDetails": True, "limit": 3, "filter": "Mcp"}) in fake.calls
    assert ("unreal.automation_run", {"fullName": "Project.Tests.One", "timeoutSeconds": 9.0, "tags": ["cli", "smoke"]}) in fake.calls
    assert fake.calls.count(("unreal.automation_report", {"runId": "r1"})) == 2
    assert decode_json(report_result) == {"runId": "r1", "status": "completed"}


def test_pie_smoke_polls_report_and_outputs_final_report(runner, install_fake_client):
    fake = FakeClient(
        responses={
            "unreal.pie_smoke": {"structuredContent": {"runId": "pie-1", "initialStatus": "queued"}},
            "unreal.automation_report": {"structuredContent": {"runId": "pie-1", "status": "completed", "pieReport": {"ok": True}}},
        }
    )
    install_fake_client(fake)

    result = runner.invoke(main, ["--json", "pie-smoke", "--map", "/Game/Test", "--tool-timeout", "20", "--alive-window", "2"])

    assert result.exit_code == 0
    assert fake.calls == [
        ("unreal.pie_smoke", {"timeoutSeconds": 20.0, "aliveWindowSeconds": 2.0, "mapPath": "/Game/Test"}),
        ("unreal.automation_report", {"runId": "pie-1"}),
    ]
    assert decode_json(result)["pieReport"] == {"ok": True}


def test_task_commands_map_arguments(runner, install_fake_client):
    fake = FakeClient()
    install_fake_client(fake)

    commands = [
        ["--json", "tasks", "list", "--filter", "pinned", "--limit", "4"],
        ["--json", "tasks", "describe", "task-1"],
        ["--json", "tasks", "rate", "task-1", "success"],
        ["--json", "tasks", "pin", "task-1", "--unpin"],
    ]
    for command in commands:
        result = runner.invoke(main, command)
        assert result.exit_code == 0

    assert ("unreal.task_list", {"filter": "pinned", "limit": 4}) in fake.calls
    assert ("unreal.task_describe", {"taskId": "task-1"}) in fake.calls
    assert ("unreal.task_rate", {"taskId": "task-1", "rating": "success"}) in fake.calls
    assert ("unreal.task_pin", {"taskId": "task-1", "pinned": False}) in fake.calls
