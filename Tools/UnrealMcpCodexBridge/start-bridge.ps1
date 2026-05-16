[CmdletBinding()]
param(
  [int]$Port,
  [ValidateSet("ws", "unix", "stdio")]
  [string]$Transport,
  [string]$McpUrl,
  [string]$CodexBin
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($PSBoundParameters.ContainsKey("Port")) {
  $env:UEVOLVE_CODEX_BRIDGE_PORT = [string]$Port
}
if ($PSBoundParameters.ContainsKey("Transport")) {
  $env:UEVOLVE_CODEX_TRANSPORT = $Transport
}
if ($PSBoundParameters.ContainsKey("McpUrl")) {
  $env:UEVOLVE_MCP_URL = $McpUrl
}
if ($PSBoundParameters.ContainsKey("CodexBin")) {
  $env:UEVOLVE_CODEX_BIN = $CodexBin
}

function Get-EnvOrUnset {
  param([Parameter(Mandatory = $true)][string]$Name)
  $Value = [Environment]::GetEnvironmentVariable($Name, "Process")
  if ([string]::IsNullOrWhiteSpace($Value)) {
    return "<unset>"
  }
  return $Value
}

Write-Host "UEVOLVE_CODEX_BIN=$(Get-EnvOrUnset "UEVOLVE_CODEX_BIN")"
Write-Host "UEVOLVE_CODEX_BRIDGE_PORT=$(Get-EnvOrUnset "UEVOLVE_CODEX_BRIDGE_PORT")"
Write-Host "UEVOLVE_CODEX_TRANSPORT=$(Get-EnvOrUnset "UEVOLVE_CODEX_TRANSPORT")"
Write-Host "UEVOLVE_CODEX_APP_SERVER_PORT=$(Get-EnvOrUnset "UEVOLVE_CODEX_APP_SERVER_PORT")"
Write-Host "UEVOLVE_MCP_URL=$(Get-EnvOrUnset "UEVOLVE_MCP_URL")"
Write-Host "UEVOLVE_CODEX_APPROVAL_POLICY=$(Get-EnvOrUnset "UEVOLVE_CODEX_APPROVAL_POLICY")"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$BridgeDir = Join-Path $RepoRoot "Tools\UnrealMcpCodexBridge"
$BundledBun = Join-Path $PSScriptRoot "runtime\bun.exe"
if ([string]::IsNullOrWhiteSpace($env:UEVOLVE_BUN_BIN) -and (Test-Path -LiteralPath $BundledBun)) {
  $env:UEVOLVE_BUN_BIN = $BundledBun
}
$BunBin = $env:UEVOLVE_BUN_BIN
if ([string]::IsNullOrWhiteSpace($BunBin)) {
  $BunBin = "bun"
}
Write-Host "UEVOLVE_BUN_BIN=$(Get-EnvOrUnset "UEVOLVE_BUN_BIN")"
& "$BunBin" run --cwd "$BridgeDir" "src\index.ts"
exit $LASTEXITCODE
