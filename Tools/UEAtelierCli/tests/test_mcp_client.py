from __future__ import annotations

from unittest.mock import patch

import pytest
import requests

from ueatelier_cli.mcp_client import (
    EndpointUnreachableError,
    HttpStatusError,
    JsonRpcProtocolError,
    McpClient,
    resolve_endpoint,
)


class FakeResponse:
    def __init__(self, payload, status_code=200, text="OK"):
        self._payload = payload
        self.status_code = status_code
        self.text = text

    def json(self):
        if isinstance(self._payload, Exception):
            raise self._payload
        return self._payload


def test_list_tools_sends_json_rpc_envelope_and_timeout():
    response = FakeResponse({"jsonrpc": "2.0", "id": 1, "result": {"tools": [{"name": "unreal.editor_status"}]}})
    with patch("requests.Session.post", return_value=response) as post:
        client = McpClient(endpoint="http://example.test/mcp", timeout=7)
        tools = client.list_tools()

    assert tools == [{"name": "unreal.editor_status"}]
    post.assert_called_once_with(
        "http://example.test/mcp",
        json={"jsonrpc": "2.0", "id": 1, "method": "tools/list", "params": {}},
        timeout=7,
    )


def test_call_tool_sends_tools_call_with_arguments():
    response = FakeResponse({"jsonrpc": "2.0", "id": 1, "result": {"structuredContent": {"ok": True}}})
    with patch("requests.Session.post", return_value=response) as post:
        client = McpClient(endpoint="http://example.test/mcp", timeout=12)
        result = client.call_tool("unreal.editor_status", {"verbose": True})

    assert result == {"structuredContent": {"ok": True}}
    post.assert_called_once_with(
        "http://example.test/mcp",
        json={
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {"name": "unreal.editor_status", "arguments": {"verbose": True}},
        },
        timeout=12,
    )


def test_connection_errors_are_endpoint_unreachable():
    with patch("requests.Session.post", side_effect=requests.exceptions.ConnectionError("refused")):
        client = McpClient(endpoint="http://127.0.0.1:8765/mcp")
        with pytest.raises(EndpointUnreachableError):
            client.list_tools()


def test_http_error_raises_http_status_error_with_body_excerpt():
    response = FakeResponse({"ignored": True}, status_code=500, text="server exploded")
    with patch("requests.Session.post", return_value=response):
        client = McpClient(endpoint="http://127.0.0.1:8765/mcp")
        with pytest.raises(HttpStatusError) as exc:
            client.list_tools()

    assert exc.value.status_code == 500
    assert exc.value.body == "server exploded"


def test_json_rpc_error_raises_protocol_error():
    response = FakeResponse({"jsonrpc": "2.0", "id": 1, "error": {"code": -32601, "message": "missing"}})
    with patch("requests.Session.post", return_value=response):
        client = McpClient(endpoint="http://127.0.0.1:8765/mcp")
        with pytest.raises(JsonRpcProtocolError) as exc:
            client.list_tools()

    assert exc.value.payload == {"code": -32601, "message": "missing"}


def test_env_endpoint_precedence(monkeypatch):
    monkeypatch.setenv("UEATELIER_MCP_ENDPOINT", "http://env.test/mcp")
    assert resolve_endpoint(None) == "http://env.test/mcp"
    assert resolve_endpoint("http://flag.test/mcp") == "http://flag.test/mcp"
