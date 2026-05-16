import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import crypto from "node:crypto";
import fs from "node:fs";
import net from "node:net";
import os from "node:os";
import path from "node:path";
import { approvalResponse, type ApprovalMode } from "./approval-policy";

export type Logger = (direction: string, payload: any) => void;
type Pending = { resolve: (value: any) => void; reject: (error: Error) => void };
export type ConfigOverrides = Record<string, string>;
export type CodexTransport = "unix" | "ws" | "stdio";

export type CodexDiscoveryWarning = {
  code: string;
  message: string;
  candidate?: string;
  recommendation?: string;
};

export type McpRegistration = {
  enabled: boolean;
  name: string;
  url: string;
  bearerConfigured: boolean;
};

export type AppServerOptions = {
  configOverrides?: ConfigOverrides;
};

export type StartedAppServer = {
  proc: ChildProcessWithoutNullStreams;
  endpoint: string;
  transport: CodexTransport;
  dir: string;
  codexBin: string;
  shellMode: boolean;
  spawnArgs: string[];
  mcpRegistration: McpRegistration;
  warnings: CodexDiscoveryWarning[];
};

type ConnectTarget =
  | { kind: "unix"; path: string; hostHeader: "localhost" }
  | { kind: "tcp"; host: string; port: number; hostHeader: string };

export class CodexWsClient {
  private socket?: net.Socket;
  private buffer = Buffer.alloc(0);
  private upgraded = false;
  private nextId = 1;
  private pending = new Map<string, Pending>();
  private target: ConnectTarget;

  constructor(
    endpoint: string,
    private log: Logger,
    private mode: ApprovalMode,
    private onNotification: (message: any) => void,
  ) {
    this.target = parseEndpoint(endpoint);
  }

  async connect(): Promise<void> {
    await new Promise<void>((resolve, reject) => {
      const sock = this.target.kind === "tcp" ? net.connect({ host: this.target.host, port: this.target.port }) : net.connect(this.target.path);
      this.socket = sock;
      const key = crypto.randomBytes(16).toString("base64");
      const timer = setTimeout(() => {
        sock.destroy();
        reject(new Error("Timed out upgrading Codex socket"));
      }, 10000);
      sock.once("connect", () => {
        sock.write(
          `GET / HTTP/1.1\r\nHost: ${this.target.hostHeader}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`,
        );
      });
      sock.on("data", (chunk) => {
        this.buffer = Buffer.concat([this.buffer, chunk]);
        if (!this.upgraded) {
          const end = this.buffer.indexOf("\r\n\r\n");
          if (end < 0) return;
          const header = this.buffer.subarray(0, end).toString("utf8");
          if (!header.startsWith("HTTP/1.1 101")) {
            reject(new Error(`Codex WebSocket upgrade failed: ${header}`));
            sock.destroy();
            return;
          }
          this.upgraded = true;
          this.buffer = this.buffer.subarray(end + 4);
          clearTimeout(timer);
          resolve();
        }
        this.readFrames();
      });
      sock.once("error", reject);
      sock.once("close", () => this.failAll(new Error("Codex app-server socket closed")));
    });
  }

  async initialize(): Promise<any> {
    const result = await this.request("initialize", {
      clientInfo: { name: "uevolve-codex-bridge", title: "UEvolve Codex Bridge", version: "0.1.0" },
      capabilities: { experimentalApi: true },
    });
    this.notify("initialized", {});
    return result;
  }

  request(method: string, params: any = {}): Promise<any> {
    const id = String(this.nextId++);
    this.send({ id, method, params });
    return new Promise((resolve, reject) => this.pending.set(id, { resolve, reject }));
  }

  notify(method: string, params: any = {}): void {
    this.send({ method, params });
  }

  close(): void {
    this.socket?.end();
  }

  private send(message: any): void {
    const json = JSON.stringify(message);
    this.log("send", message);
    this.socket?.write(encodeClientTextFrame(json));
  }

  private readFrames(): void {
    while (this.buffer.length >= 2) {
      const b1 = this.buffer[0];
      const b2 = this.buffer[1];
      let len = b2 & 0x7f;
      let offset = 2;
      if (len === 126) {
        if (this.buffer.length < 4) return;
        len = this.buffer.readUInt16BE(2);
        offset = 4;
      } else if (len === 127) {
        if (this.buffer.length < 10) return;
        len = Number(this.buffer.readBigUInt64BE(2));
        offset = 10;
      }
      const masked = (b2 & 0x80) !== 0;
      const maskOffset = offset;
      if (masked) offset += 4;
      if (this.buffer.length < offset + len) return;
      let payload = this.buffer.subarray(offset, offset + len);
      if (masked) {
        const mask = this.buffer.subarray(maskOffset, maskOffset + 4);
        payload = Buffer.from(payload.map((value, index) => value ^ mask[index % 4]));
      }
      this.buffer = this.buffer.subarray(offset + len);
      const opcode = b1 & 0x0f;
      if (opcode === 1) this.handleJson(JSON.parse(payload.toString("utf8")));
      if (opcode === 8) this.socket?.end();
      if (opcode === 9) this.socket?.write(Buffer.from([0x8a, 0x00]));
    }
  }

  private handleJson(message: any): void {
    this.log("recv", message);
    if (message.id != null && message.method) {
      this.send({ id: message.id, result: approvalResponse(message.method, message.params, this.mode) });
      return;
    }
    if (message.id != null) {
      const waiter = this.pending.get(String(message.id));
      if (!waiter) return;
      this.pending.delete(String(message.id));
      message.error ? waiter.reject(new Error(message.error.message ?? JSON.stringify(message.error))) : waiter.resolve(message.result);
      return;
    }
    if (message.method) this.onNotification(message);
  }

  private failAll(error: Error): void {
    for (const waiter of this.pending.values()) waiter.reject(error);
    this.pending.clear();
  }
}

export class CodexStdioClient {
  private buffer = "";
  private nextId = 1;
  private pending = new Map<string, Pending>();
  private messageHandlers = new Set<(msg: any) => void>();
  private errorHandlers = new Set<(err: Error) => void>();
  private closeHandlers = new Set<() => void>();

  constructor(
    private proc: ChildProcessWithoutNullStreams,
    private log: Logger = () => {},
    private mode: ApprovalMode = "reject",
    private onNotification: (message: any) => void = () => {},
  ) {
    this.proc.stdout.on("data", (chunk) => this.handleStdout(chunk));
    this.proc.stdout.on("error", (error) => this.emitError(error));
    this.proc.stdin.on("error", (error) => this.emitError(error));
    this.proc.once("error", (error) => this.emitError(error));
    this.proc.once("close", () => {
      this.failAll(new Error("Codex app-server stdio closed"));
      for (const handler of this.closeHandlers) handler();
    });
  }

  async connect(): Promise<void> {
    if (!this.proc.stdin.writable) throw new Error("Codex app-server stdin is not writable");
    if (!this.proc.stdout.readable) throw new Error("Codex app-server stdout is not readable");
  }

  async initialize(): Promise<any> {
    const result = await this.request("initialize", {
      clientInfo: { name: "uevolve-codex-bridge", title: "UEvolve Codex Bridge", version: "0.1.0" },
      capabilities: { experimentalApi: true },
    });
    this.notify("initialized", {});
    return result;
  }

  request(method: string, params: any = {}): Promise<any> {
    const id = String(this.nextId++);
    this.send({ id, method, params });
    return new Promise((resolve, reject) => this.pending.set(id, { resolve, reject }));
  }

  notify(method: string, params: any = {}): void {
    this.send({ method, params });
  }

  send(message: object): void {
    this.log("send", message);
    this.proc.stdin.write(`${JSON.stringify(message)}\n`);
  }

  onMessage(handler: (msg: any) => void): void {
    this.messageHandlers.add(handler);
  }

  onError(handler: (err: Error) => void): void {
    this.errorHandlers.add(handler);
  }

  onClose(handler: () => void): void {
    this.closeHandlers.add(handler);
  }

  async close(): Promise<void> {
    this.proc.stdin.end();
  }

  private handleStdout(chunk: Buffer | string): void {
    this.buffer += chunk.toString();
    for (;;) {
      const newline = this.buffer.indexOf("\n");
      if (newline < 0) return;
      let line = this.buffer.slice(0, newline);
      this.buffer = this.buffer.slice(newline + 1);
      if (line.endsWith("\r")) line = line.slice(0, -1);
      if (!line.trim()) continue;
      try {
        this.handleJson(JSON.parse(line));
      } catch (error) {
        this.emitError(new Error(`Failed to parse Codex stdio JSON: ${error instanceof Error ? error.message : String(error)}`));
      }
    }
  }

  private handleJson(message: any): void {
    this.log("recv", message);
    for (const handler of this.messageHandlers) handler(message);
    if (message.id != null && message.method) {
      this.send({ id: message.id, result: approvalResponse(message.method, message.params, this.mode) });
      return;
    }
    if (message.id != null) {
      const waiter = this.pending.get(String(message.id));
      if (!waiter) return;
      this.pending.delete(String(message.id));
      message.error ? waiter.reject(new Error(message.error.message ?? JSON.stringify(message.error))) : waiter.resolve(message.result);
      return;
    }
    if (message.method) this.onNotification(message);
  }

  private emitError(error: Error): void {
    for (const handler of this.errorHandlers) handler(error);
  }

  private failAll(error: Error): void {
    for (const waiter of this.pending.values()) waiter.reject(error);
    this.pending.clear();
  }
}

export async function startCodexAppServer(
  onExit: (reason: string) => void,
  options: AppServerOptions = {},
): Promise<StartedAppServer> {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "uevolve-codex-bridge-"));
  const transport = selectedTransport();
  const endpoint = transport === "stdio" ? "stdio://" : transport === "ws" ? `ws://127.0.0.1:${await selectedPort()}` : path.join(dir, "codex.sock");
  const listenEndpoint = transport === "stdio" ? "stdio://" : transport === "ws" ? endpoint : `unix://${endpoint}`;
  const spawnArgs = ["app-server", "--listen", listenEndpoint];
  const mcpRegistration = mcpRegistrationFromEnv();
  const configOverrides: ConfigOverrides = {};
  if (mcpRegistration.enabled) {
    configOverrides[`mcp_servers.${mcpRegistration.name}.url`] = tomlString(mcpRegistration.url);
    configOverrides[`mcp_servers.${mcpRegistration.name}.transport`] = tomlString("streamable-http");
    const bearer = process.env.UEVOLVE_MCP_BEARER;
    if (bearer) configOverrides[`mcp_servers.${mcpRegistration.name}.bearer_token`] = tomlString(bearer);
  }
  Object.assign(configOverrides, options.configOverrides ?? {});
  for (const [key, value] of Object.entries(configOverrides)) spawnArgs.push("-c", `${key}=${value}`);
  const codexResolution = await resolveCodexBinary();
  const codexBin = codexResolution.path;
  const shellMode = shouldUseShellForCodex(codexBin);
  let proc: ChildProcessWithoutNullStreams;
  try {
    proc = spawn(codexBin, spawnArgs, { shell: shellMode, stdio: ["pipe", "pipe", "pipe"], windowsHide: true });
  } catch (error) {
    throw new Error(failedCodexLaunchMessage(error));
  }
  proc.once("error", (error) => onExit(failedCodexLaunchMessage(error)));
  proc.once("exit", (code, signal) => onExit(`codex app-server exited code=${code} signal=${signal}`));
  return { proc, endpoint, transport, dir, codexBin, shellMode, spawnArgs, mcpRegistration, warnings: codexResolution.warnings };
}

export async function waitForEndpoint(endpoint: string, transport: CodexTransport): Promise<void> {
  if (transport === "stdio") return;
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    if (transport === "unix" && fs.existsSync(endpoint)) return;
    if (transport === "ws" && (await canConnectTcp(endpoint))) return;
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`Timed out waiting for ${endpoint}`);
}

function encodeClientTextFrame(text: string): Buffer {
  const payload = Buffer.from(text);
  const header =
    payload.length < 126
      ? Buffer.from([0x81, 0x80 | payload.length])
      : payload.length < 65536
        ? Buffer.from([0x81, 0x80 | 126, payload.length >> 8, payload.length & 255])
        : Buffer.concat([Buffer.from([0x81, 0x80 | 127]), u64(payload.length)]);
  const mask = crypto.randomBytes(4);
  const masked = Buffer.alloc(payload.length);
  for (let i = 0; i < payload.length; i++) masked[i] = payload[i] ^ mask[i % 4];
  return Buffer.concat([header, mask, masked]);
}

function u64(value: number): Buffer {
  const buf = Buffer.alloc(8);
  buf.writeBigUInt64BE(BigInt(value));
  return buf;
}

function mcpRegistrationFromEnv(): McpRegistration {
  const name = process.env.UEVOLVE_MCP_NAME ?? "unrealmcp";
  if (!/^[A-Za-z0-9_-]+$/.test(name)) throw new Error(`Invalid UEVOLVE_MCP_NAME: ${name}`);
  return {
    enabled: process.env.UEVOLVE_DISABLE_AUTO_MCP_REGISTER !== "1",
    name,
    url: process.env.UEVOLVE_MCP_URL ?? "http://127.0.0.1:8765/mcp",
    bearerConfigured: Boolean(process.env.UEVOLVE_MCP_BEARER),
  };
}

function selectedTransport(): CodexTransport {
  const requested = process.env.UEVOLVE_CODEX_TRANSPORT?.trim().toLowerCase();
  if (requested === "ws" || requested === "unix" || requested === "stdio") return requested;
  if (requested) throw new Error(`Invalid UEVOLVE_CODEX_TRANSPORT: ${requested}`);
  return process.platform === "win32" ? "stdio" : "unix";
}

async function resolveCodexBinary(): Promise<{ path: string; warnings: CodexDiscoveryWarning[] }> {
  const warnings: CodexDiscoveryWarning[] = [];
  const tried: string[] = [];
  const override = process.env.UEVOLVE_CODEX_BIN?.trim();
  if (override) {
    tried.push(`UEVOLVE_CODEX_BIN=${override}`);
    if (!path.isAbsolute(override)) throw new Error(`UEVOLVE_CODEX_BIN must be an absolute path. Got: ${override}`);
    if (!fs.existsSync(override)) throw new Error(`UEVOLVE_CODEX_BIN does not exist: ${override}`);
    rejectWindowsAppsCodex(override);
    return { path: override, warnings };
  }
  tried.push("UEVOLVE_CODEX_BIN=<unset>");

  if (process.platform === "win32") {
    const localCandidates = windowsUserCodexCandidates();
    tried.push(localCandidates.pattern);
    const localCandidate = firstUsableCodexCandidate(localCandidates.paths, warnings);
    if (localCandidate) return { path: localCandidate, warnings };
  }

  const resolver = process.platform === "win32" ? "where" : "which";
  tried.push(`${resolver} codex`);
  const resolved = await resolvePaths(resolver, "codex");
  const resolvedCandidate = firstUsableCodexCandidate(resolved, warnings);
  if (resolvedCandidate) return { path: resolvedCandidate, warnings };

  const warningText = warnings.length ? ` Warnings: ${warnings.map((warning) => warning.message).join(" ")}` : "";
  throw new Error(
    `Unable to find a usable codex binary. Tried: ${tried.join("; ")}.${warningText} Set UEVOLVE_CODEX_BIN to the full path of a user-mode codex.exe or codex.cmd.`,
  );
}

function windowsUserCodexCandidates(): { pattern: string; paths: string[] } {
  const base = process.env.LOCALAPPDATA ? path.join(process.env.LOCALAPPDATA, "OpenAI", "Codex", "bin") : "";
  const pattern = "%LOCALAPPDATA%\\OpenAI\\Codex\\bin\\*\\codex.exe";
  if (!base || !fs.existsSync(base)) return { pattern, paths: [] };
  const paths = fs
    .readdirSync(base, { withFileTypes: true })
    .filter((entry) => entry.isDirectory())
    .map((entry) => path.join(base, entry.name, "codex.exe"))
    .filter((candidate) => fs.existsSync(candidate))
    .sort((a, b) => a.localeCompare(b));
  return { pattern, paths };
}

function firstUsableCodexCandidate(candidates: string[], warnings: CodexDiscoveryWarning[]): string {
  for (const candidate of candidates) {
    if (isWindowsAppsCodex(candidate)) {
      warnings.push({
        code: "windowsapps-codex-skipped",
        message: "WindowsApps Codex binary skipped; install user-mode Codex or set UEVOLVE_CODEX_BIN.",
        candidate,
        recommendation: "Install the Codex user-mode binary under %LOCALAPPDATA%\\OpenAI\\Codex\\bin or set UEVOLVE_CODEX_BIN.",
      });
      continue;
    }
    return candidate;
  }
  return "";
}

function rejectWindowsAppsCodex(candidate: string): void {
  if (!isWindowsAppsCodex(candidate)) return;
  throw new Error(
    `UEVOLVE_CODEX_BIN points at the WindowsApps Codex binary, which Bun cannot spawn: ${candidate}. Install user-mode Codex or set UEVOLVE_CODEX_BIN to a user-mode codex.exe/codex.cmd.`,
  );
}

function isWindowsAppsCodex(candidate: string): boolean {
  if (process.platform !== "win32") return false;
  const normalized = path.resolve(candidate).replace(/\//g, "\\").toLowerCase();
  return normalized.startsWith("c:\\program files\\windowsapps\\");
}

async function resolvePaths(command: string, name: string): Promise<string[]> {
  return new Promise((resolve, reject) => {
    const proc = spawn(command, [name], { stdio: ["ignore", "pipe", "pipe"], windowsHide: true });
    let stdout = "";
    let stderr = "";
    proc.stdout.on("data", (chunk) => (stdout += chunk.toString("utf8")));
    proc.stderr.on("data", (chunk) => (stderr += chunk.toString("utf8")));
    proc.once("error", (error) => reject(new Error(`Failed to run '${command} ${name}': ${error.message}`)));
    proc.once("exit", (code) => {
      const paths = stdout.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
      if (code === 0 && paths.length) resolve(paths);
      else reject(new Error(`'${command} ${name}' failed with code ${code}${stderr.trim() ? `: ${stderr.trim()}` : ""}`));
    });
  }).catch(() => []);
}

function shouldUseShellForCodex(codexBin: string): boolean {
  return process.platform === "win32" && [".cmd", ".bat"].includes(path.extname(codexBin).toLowerCase());
}

function failedCodexLaunchMessage(error: unknown): string {
  return `Failed to launch codex. Set UEVOLVE_CODEX_BIN to the full path of codex.exe or codex.cmd. Got error: ${error instanceof Error ? error.message : String(error)}`;
}

async function selectedPort(): Promise<number> {
  const requested = process.env.UEVOLVE_CODEX_APP_SERVER_PORT?.trim();
  if (requested) {
    const port = Number(requested);
    if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error(`Invalid UEVOLVE_CODEX_APP_SERVER_PORT: ${requested}`);
    return port;
  }
  return freeLocalhostPort();
}

async function freeLocalhostPort(): Promise<number> {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once("error", reject);
    server.listen(0, "127.0.0.1", () => {
      const address = server.address();
      if (!address || typeof address === "string") {
        server.close(() => reject(new Error("Failed to discover a free localhost port")));
        return;
      }
      const port = address.port;
      server.close((error) => (error ? reject(error) : resolve(port)));
    });
  });
}

function parseEndpoint(endpoint: string): ConnectTarget {
  if (endpoint.startsWith("ws://") || endpoint.startsWith("wss://")) {
    const url = new URL(endpoint);
    const port = Number(url.port || (url.protocol === "wss:" ? 443 : 80));
    if (!url.hostname || !Number.isInteger(port)) throw new Error(`Invalid Codex endpoint: ${endpoint}`);
    return { kind: "tcp", host: url.hostname, port, hostHeader: `${url.hostname}:${port}` };
  }
  return { kind: "unix", path: endpoint, hostHeader: "localhost" };
}

async function canConnectTcp(endpoint: string): Promise<boolean> {
  const target = parseEndpoint(endpoint);
  if (target.kind !== "tcp") return false;
  return new Promise((resolve) => {
    const sock = net.connect({ host: target.host, port: target.port });
    const timer = setTimeout(() => {
      sock.destroy();
      resolve(false);
    }, 250);
    sock.once("connect", () => {
      clearTimeout(timer);
      sock.end();
      resolve(true);
    });
    sock.once("error", () => {
      clearTimeout(timer);
      resolve(false);
    });
  });
}

function tomlString(value: string): string {
  return JSON.stringify(value);
}
