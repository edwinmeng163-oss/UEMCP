"""Thin HTTP JSON-RPC client for UEAtelier MCP."""

from __future__ import annotations

import itertools
import os
from typing import Any

import requests

DEFAULT_ENDPOINT = "http://127.0.0.1:8765/mcp"
ENDPOINT_ENV_VAR = "UEATELIER_MCP_ENDPOINT"


class McpClientError(Exception):
    """Base class for client-facing MCP errors."""

    def __init__(self, message: str, *, endpoint: str) -> None:
        super().__init__(message)
        self.message = message
        self.endpoint = endpoint


class EndpointUnreachableError(McpClientError):
    """Raised when the MCP endpoint cannot be reached."""


class HttpStatusError(McpClientError):
    """Raised when the endpoint returns an HTTP 4xx or 5xx response."""

    def __init__(self, message: str, *, endpoint: str, status_code: int, body: str) -> None:
        super().__init__(message, endpoint=endpoint)
        self.status_code = status_code
        self.body = body


class JsonRpcProtocolError(McpClientError):
    """Raised when the endpoint returns a JSON-RPC error or malformed response."""

    def __init__(self, message: str, *, endpoint: str, payload: Any | None = None) -> None:
        super().__init__(message, endpoint=endpoint)
        self.payload = payload


def resolve_endpoint(endpoint: str | None = None) -> str:
    """Resolve endpoint using CLI flag, environment, then built-in default."""

    if endpoint:
        return endpoint
    return os.environ.get(ENDPOINT_ENV_VAR, DEFAULT_ENDPOINT)


def _body_excerpt(text: str, limit: int = 500) -> str:
    if len(text) <= limit:
        return text
    return text[:limit] + "..."


class McpClient:
    """Minimal JSON-RPC-over-HTTP client for UEAtelier MCP."""

    def __init__(
        self,
        endpoint: str | None = None,
        timeout: float = 120,
        session: requests.Session | None = None,
    ) -> None:
        self.endpoint = resolve_endpoint(endpoint)
        self.timeout = timeout
        self.session = session or requests.Session()
        self._ids = itertools.count(1)

    def request(self, method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        request_id = next(self._ids)
        envelope = {
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
            "params": params or {},
        }

        try:
            response = self.session.post(self.endpoint, json=envelope, timeout=self.timeout)
        except (requests.exceptions.ConnectionError, requests.exceptions.Timeout) as exc:
            raise EndpointUnreachableError(str(exc), endpoint=self.endpoint) from exc
        except requests.exceptions.RequestException as exc:
            raise EndpointUnreachableError(str(exc), endpoint=self.endpoint) from exc

        body = getattr(response, "text", "")
        status_code = int(getattr(response, "status_code", 0))
        if status_code >= 400:
            excerpt = _body_excerpt(body)
            raise HttpStatusError(
                f"HTTP {status_code} from MCP endpoint",
                endpoint=self.endpoint,
                status_code=status_code,
                body=excerpt,
            )

        try:
            payload = response.json()
        except ValueError as exc:
            raise HttpStatusError(
                "MCP endpoint returned non-JSON response",
                endpoint=self.endpoint,
                status_code=status_code,
                body=_body_excerpt(body),
            ) from exc

        if not isinstance(payload, dict):
            raise JsonRpcProtocolError(
                "MCP endpoint returned a non-object JSON-RPC payload",
                endpoint=self.endpoint,
                payload=payload,
            )

        if "error" in payload:
            raise JsonRpcProtocolError(
                "MCP endpoint returned JSON-RPC error",
                endpoint=self.endpoint,
                payload=payload.get("error"),
            )

        if "result" not in payload:
            raise JsonRpcProtocolError(
                "MCP endpoint response did not contain result",
                endpoint=self.endpoint,
                payload=payload,
            )

        return payload

    def list_tools(self) -> list[dict[str, Any]]:
        payload = self.request("tools/list", {})
        result = payload.get("result")
        if not isinstance(result, dict):
            raise JsonRpcProtocolError(
                "tools/list result was not an object",
                endpoint=self.endpoint,
                payload=result,
            )
        tools = result.get("tools")
        if not isinstance(tools, list):
            raise JsonRpcProtocolError(
                "tools/list result did not contain a tools array",
                endpoint=self.endpoint,
                payload=result,
            )
        return tools

    def call_tool(self, name: str, arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        payload = self.request(
            "tools/call",
            {
                "name": name,
                "arguments": arguments or {},
            },
        )
        result = payload.get("result")
        if not isinstance(result, dict):
            raise JsonRpcProtocolError(
                "tools/call result was not an object",
                endpoint=self.endpoint,
                payload=result,
            )
        return result


def is_tool_error(result: dict[str, Any]) -> bool:
    return bool(result.get("isError"))


def structured_content(result: dict[str, Any]) -> Any:
    """Return the MCP structuredContent payload, if present."""

    return result.get("structuredContent")
