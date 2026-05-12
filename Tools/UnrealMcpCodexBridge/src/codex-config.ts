import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";

type EnsureResult = {
  wroteFile: boolean;
  path: string;
};

export async function ensureUnrealMcpServerRegistered(opts: { url: string; configPath?: string }): Promise<EnsureResult> {
  const configPath = opts.configPath ?? path.join(os.homedir(), ".codex", "config.toml");
  try {
    await fs.mkdir(path.dirname(configPath), { recursive: true });
    await fs.writeFile(configPath, "", { flag: "a" });

    const existing = await fs.readFile(configPath, "utf8");
    const canonical = canonicalUnrealMcpBlock(opts.url);
    const updated = mergeUnrealMcpBlock(existing, canonical);
    if (updated === existing) return { wroteFile: false, path: configPath };

    await fs.writeFile(configPath, updated, "utf8");
    return { wroteFile: true, path: configPath };
  } catch (error) {
    console.error(`Failed to update Codex config at ${configPath}: ${error instanceof Error ? error.message : String(error)}`);
    return { wroteFile: false, path: configPath };
  }
}

function mergeUnrealMcpBlock(existing: string, canonical: string): string {
  const ranges = findUnrealMcpTableRanges(existing);
  if (ranges.length === 0) return `${existing}${appendSeparator(existing)}${canonical}\n`;

  const currentBlock = existing.slice(ranges[0].start, ranges[0].end);
  if (ranges.length === 1 && (currentBlock === canonical || currentBlock === `${canonical}\n`)) return existing;

  let updated = `${existing.slice(0, ranges[0].start)}${canonical}\n`;
  let cursor = ranges[0].end;
  for (const range of ranges.slice(1)) {
    updated += existing.slice(cursor, range.start);
    cursor = range.end;
  }
  return `${updated}${existing.slice(cursor)}`;
}

function findUnrealMcpTableRanges(text: string): Array<{ start: number; end: number }> {
  const ranges: Array<{ start: number; end: number }> = [];
  const headerPattern = /^\s*\[mcp_servers\.unrealmcp\]\s*$/gm;
  for (const match of text.matchAll(headerPattern)) {
    ranges.push({
      start: match.index,
      end: findTableBlockEnd(text, nextLineStart(text, match.index)),
    });
  }
  return ranges;
}

function findTableBlockEnd(text: string, cursor: number): number {
  while (cursor < text.length) {
    if (text[cursor] === "[") return cursor;
    cursor = nextLineStart(text, cursor);
  }
  return text.length;
}

function nextLineStart(text: string, index: number): number {
  const newline = text.indexOf("\n", index);
  return newline === -1 ? text.length : newline + 1;
}

function appendSeparator(text: string): string {
  if (!text) return "";
  return text.endsWith("\n") ? "\n" : "\n\n";
}

function canonicalUnrealMcpBlock(url: string): string {
  return `[mcp_servers.unrealmcp]
url = "${tomlStringContent(url)}"
transport = "streamable-http"
enabled = true
# Auto-managed by Tools/UnrealMcpCodexBridge/src/codex-config.ts.
# If you edit fields manually, the bridge will refresh this block on next start.`;
}

function tomlStringContent(value: string): string {
  return value.replace(/\\/g, "\\\\").replace(/"/g, '\\"');
}
