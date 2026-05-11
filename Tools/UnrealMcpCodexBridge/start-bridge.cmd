@echo off
rem UEvolve Codex Bridge launcher for cmd.exe.
rem
rem Optional environment variables:
rem   UEVOLVE_CODEX_BIN=C:\full\path\to\codex.exe or codex.cmd
rem   UEVOLVE_CODEX_BRIDGE_PORT=8766
rem   UEVOLVE_CODEX_TRANSPORT=ws
rem   UEVOLVE_CODEX_APP_SERVER_PORT=<fixed app-server port>
rem   UEVOLVE_MCP_URL=http://127.0.0.1:8765/mcp
rem   UEVOLVE_CODEX_APPROVAL_POLICY=reject

setlocal
pushd "%~dp0..\.." >nul
bun run --cwd Tools\UnrealMcpCodexBridge src\index.ts
set "UEVOLVE_BRIDGE_EXIT=%ERRORLEVEL%"
popd >nul
echo.
pause
exit /b %UEVOLVE_BRIDGE_EXIT%
