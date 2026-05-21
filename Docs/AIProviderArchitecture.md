# AI Provider Architecture (UnrealMcp)

> v0.25.0 - updated for Reform B. Audience: contributors editing
> `Plugins/UnrealMcp/Source/UnrealMcp/Private/Providers/`. Cross-referenced
> from `Docs/Release-2026-05.md` v0.24.4 entry and from inline file-top
> comments in `CodexProvider.cpp`.

## A. Provider kinds (taxonomy)

There are 5 `EAiProviderKind` values today, served by 3 transport patterns.
`AnthropicMessages` is the source enum name; UI labels usually shorten it to
Anthropic.

| Kind | Transport | Streaming | Auth |
| --- | --- | --- | --- |
| OpenAiResponses | HTTP (Responses API) | SSE | Bearer token |
| OpenAiChatCompat | HTTP (Chat Completions) | SSE / NDJSON | Bearer token |
| AnthropicMessages | HTTP (Messages API) | SSE | x-api-key |
| Codex | Subprocess (codex exec, one-shot non-interactive, JSONL stdout) | line-by-line JSONL events; reply text arrives as one `item.completed` event (no token-level deltas in v0.25) | OAuth via codex login |
| CodexAppServer | WebSocket (localhost) | JSON frames | OAuth via Codex Desktop |

`EAiProviderKind` is append-only. Numeric values never change. See
`Plugins/UnrealMcp/Source/UnrealMcp/Public/UnrealMcpSettings.h` for the enum.

Provider implementation files live under
`Plugins/UnrealMcp/Source/UnrealMcp/Private/Providers/`. The provider factory
map in `OpenAiResponsesProvider.cpp` is the central dispatch surface for the
five kinds above.

HTTP providers share the same broad run lifecycle:

- validate config and model fields;
- prepare payload from `UUnrealMcpSettings`, prompt text, tool definitions, and
  prior response context;
- stream bytes into provider-specific parsers;
- emit text/tool events back to `SUnrealMcpChatPanel`;
- call Unreal MCP tools through the shared backend.

Codex providers differ from HTTP providers:

- `Codex` launches a local `codex exec` subprocess through `/bin/bash -c`;
- `CodexAppServer` speaks to a local bridge WebSocket;
- both rely on the user already being authenticated in the local Codex toolchain.

## B. CodexProvider shell-escaping invariants (CRITICAL)

The `Codex` provider is the only provider that builds an `Arguments` string for
`FPlatformProcess::CreateProc`. That path must obey the following invariant
list exactly:

- The full `Arguments` string passed to `FPlatformProcess::CreateProc` has exactly one literal ASCII space: the separator in `-c <command>`.
- The full `Arguments` string contains zero literal ASCII double-quote characters (`0x22`).
- Trusted generated shell fragments MAY contain required shell syntax: assignment `=`, `$HOME`, `$PATH`, `${IFS}`, `&&`, and `$'...'` ANSI-C single-quote delimiters. **NOTE**: the `export` builtin is forbidden for PATH augmentation (see invariant 8 below).
- User-supplied or config-derived values MUST NEVER be concatenated raw into the command.
- User-supplied or config-derived values MUST appear only as output of `QuoteForBashWordNoSpaces(...)`, or be rejected upstream by `ContainsDangerousShellCharacters(...)`.
- Command parts MUST be joined with `${IFS}`, never with literal ASCII space.
- The command MUST NOT use `"..."` double-quote wrapping, `"$(cat ...)"` substitution, or any other construct that introduces literal `"` characters.
- **PATH augmentation MUST use the bare assignment form** `PATH=...:$PATH&&${IFS}<cmd>`, NOT `export${IFS}PATH=...&&${IFS}<cmd>`. Rationale: `export VAR=value` is the export BUILTIN which word-splits its single argument (`VAR=value`) after parameter expansion. When inherited `$PATH` contains directories with literal spaces — common on macOS, e.g. `/Library/Application Support/JetBrains/Toolbox/scripts` — the expanded assignment splits at the space and `export` receives multiple args. The second arg is not a valid identifier and fails: `export: 'Support/JetBrains/...': not a valid identifier`. The bare `PATH=value` form (no `export` keyword) is a current-shell variable assignment whose RHS is POSIX-guaranteed NOT word-split post-expansion. `$PATH` is already exported by the parent UE Editor process, so re-assignment preserves the exported status and the new value propagates to child processes via standard env inheritance. (Discovered during v0.24.4 ship-time verification.)
- ConversationContext for multi-turn chat MUST be passed via the child subprocess's stdin pipe (closed after write to signal EOF), NOT concatenated into the argv prompt. Argv prompt contains only the latest user message. Rationale: avoids ARG_MAX cap; clean `<stdin>` block separation in codex's prompt envelope.
- Cancellation MUST use `FPlatformProcess::TerminateProc(handle, /*killTree=*/true)` directly on the bash subprocess handle, not a separate `kill <jobId>` subprocess command.

These rules are about the full string passed as the `Arguments` parameter to
`CreateProc`, not about C++ comments, docs, or temporary files. Trusted shell
syntax is allowed only because the plugin generates it. User input, provider
config values, extra args, paths, prompt text, and job ids must not be appended
raw.

### Concrete do / don't examples

DON'T:

```cpp
// literal " breaks UE CreateProc tokenizer
CommandParts.Add(FString::Printf(TEXT("\"$(cat${IFS}%s)\""), *Path));

// literal " in PATH wrapper
"export${IFS}PATH=\"...$PATH\"&&${IFS}"

// literal space breaks IFS-join discipline
CommandParts.Add(TEXT("-m gpt-5.5"));
```

DO:

```cpp
// latest user prompt only; ConversationContext goes through stdin
CommandParts.Add(QuoteForBashWordNoSpaces(UserPrompt));

// PATH wrapper with no literal "
"PATH=$HOME/.bun/bin:...:$PATH&&${IFS}"

// separate IFS-joined args
CommandParts.Add(TEXT("-c"));
CommandParts.Add(QuoteForBashWordNoSpaces(TEXT("model=\"gpt-5.5\"")));
```

### Case studies

- **v0.24.3 - PATH-prefix `"..."` regression**: PM added defensive `"..."`
  wrapping for the PATH assignment, intending to protect against word-splitting
  that POSIX bash does not actually perform on assignment RHS. Cost: literal
  `"` chars in the bash `-c` argv, tokenizer mangling, and
  `bash: -c: option requires an argument`. Fixed in v0.24.4 Edit A.
- **v0.24.4 - pre-existing cat-substitution latent bug**:
  `"$(cat ${IFS}...)"` was masked for months by the `bun: not found` failure
  earlier in the pipeline. v0.24.3 PATH fix unmasked it. Fixed in v0.24.4
  Edit B by passing the composed prompt directly as a `$'...'` argv word.
- **v0.25.0 - Reform B exec rewrite**:
  `codex-agent start -w` could not return the real assistant reply because the
  upstream codex CLI stayed interactive inside the orchestrated session. Reform
  B replaced that path with `codex exec --json --ephemeral`, 3 process pipes,
  JSONL stdout parsing, stderr auth detection, stdin ConversationContext, and
  direct process termination.

### Helper ownership

The shell helpers are production code and test surface:

- `QuoteForBashWordNoSpaces` escapes one value and wraps it in `$'...'`.
- `JoinShellArgumentsNoSpaces` joins quoted values with `${IFS}`.
- `BuildCodexExecCommand` builds the `codex exec` command payload.
- `ParseCodexJsonlEvent` parses one stdout JSONL event line.
- `DetectCodexAuthError` rate-limits friendly login guidance per run.
- `LooksLikeLegacyCodexAgentPath` and `ContainsUnsupportedReasoningFlag`
  provide v0.25 migration checks.

The helper declarations live in the private header
`Providers/CodexProviderExecHelpers.h` so integration tests can call the same
code path as runtime. Tests must not reimplement shell construction logic.

## C. Settings UI architecture

`UDeveloperSettings` plus `UPROPERTY(Config, EditAnywhere)` is the canonical
pattern for Project Settings fields. Use it when a provider setting should be
visible to users and serialized in project config.

`PostEditChangeProperty` fires for top-level property edits. Nested struct
edits, such as a `FAiProviderConfig` field inside a `Providers[]` array element,
only fire `PostEditChangeChainProperty`. v0.24.1 lesson: handle both entry
points for preset auto-fill semantics.

`meta=(GetOptions="FunctionName")` provides dropdown values for `FString`
properties without needing a separate enum. This is useful for preset ids,
model choices, or provider rows where the saved value must remain a string.

To hide a deprecated `UPROPERTY` from the UI, remove `EditAnywhere` and
optionally remove `BlueprintReadWrite`. Keep `Config` so existing user `*.ini`
values still load. `meta=(DeprecatedProperty)` alone does not hide the field in
the Property Editor.

`PostInitProperties` direct invocation in tests poisons UObject thread context.
Extract the initialization logic into a helper that tests call directly.
v0.24.2 fixed the preset initialization path using that pattern.

Settings code should preserve user intent:

- applying a preset must not erase an existing API key;
- an explicit provider id should remain stable unless the user changes it;
- saved URLs should survive upgrades unless migration logic intentionally opts
  into a rewrite;
- provider enum values are append-only.

## D. Region awareness (partial)

v0.24.2 set international-first defaults across all OpenAI-compatible presets:

- Kimi uses `https://api.moonshot.ai/v1/chat/completions`;
- GLM uses `https://api.z.ai/api/paas/v4/chat/completions`;
- Qwen uses `https://dashscope-intl.aliyuncs.com/compatible-mode/v1/chat/completions`;
- DeepSeek uses `https://api.deepseek.com/v1/chat/completions`;
- Ollama stays local at `http://127.0.0.1:11434/v1/chat/completions`.

The project memory says the user is in Tokyo. Mainland-CN URLs are explicit
opt-in only and should not be reintroduced as defaults.

Current gap: there is no per-user `Region` setting that auto-routes to CN
endpoints. That routing is out of v0.24.4 scope.

## E. Testing strategy

Pure-function tests cover string content and registry shape:

- provider preset count and ids;
- default URLs and model fields;
- enum range safety;
- chain-event behavior for nested settings edits;
- subprocess PATH prefix content.

Integration tests cover runtime boundaries that string tests cannot prove:

- v0.25 tests spawn `/bin/bash -c` with the production-built `codex exec`
  command;
- the fake codex binary is a shell script that records argv and stdin;
- argv assertions prove `exec`, JSON flags, `-C`, `-c`, and the user prompt
  land in the expected order, with the prompt last;
- stdin assertions prove ConversationContext is not folded into argv;
- stdout assertions cover JSONL events, including an `item.completed` line split
  across two writes.

JSONL parser tests cover:

- `thread.started`;
- `turn.started`;
- `item.completed` with `agent_message` text;
- `turn.completed` usage fields;
- error events and malformed JSON rejection;
- unknown future event types, which are preserved as `Unknown` with raw JSON.

Pure string tests are not enough for CreateProc-bound command strings.
v0.24.3's subprocess PATH prefix test passed because the string contained the
intended directories, but the real exec failed at the tokenizer layer that no
pure-string test exercised.

Rule of thumb: any `FString` that ultimately flows into
`FPlatformProcess::CreateProc` needs at minimum one integration test that
execs the constructed command against a deterministic helper and asserts argv
boundaries.

The integration test intentionally uses a fake codex script rather than
`/bin/echo`. `echo` collapses argv into one space-separated output line, which
cannot distinguish one argument containing an embedded space from two
arguments, and it cannot prove stdin transport.

## F. Known UE gotchas

`UnixPlatformProcess.cpp:ParseCmdLineToken` treats `"` as a quote delimiter and
captures double-double-quoted text into argv. See Section B.

`UObjectGlobals.cpp:PostInitPropertiesCheck` has thread-context expectations.
Direct re-invocation in test fixtures can assert at runtime. Extract a
non-`PostInitProperties` helper instead.

`FPaths::FileExists` does not expand `~` (tilde). The preset default for
`CodexBinaryPath` is now empty, but user-entered `~/...` paths remain literal
and fail validation. Users must supply the absolute path returned by
`which codex`. See Section G2.

`FPlatformProcess::CreateProc` can wire three pipes at once: stdout child write,
stdin child read, and stderr child write. For Codex CLI, stdout is JSONL,
stderr carries process/auth diagnostics, and stdin carries ConversationContext.
On Mac, `WritePipe` blocks on the write side by design, so large stdin payloads
must be written from the worker pump thread in chunks and the stdin write end
must be closed afterward to signal EOF.

`meta=(DeprecatedProperty)` does not hide a `UPROPERTY` from the Property
Editor. Remove `EditAnywhere` for deprecated config values that should still
deserialize but should not be edited in the UI.

macOS GUI launches often inherit a minimal default PATH rather than the user's
interactive shell PATH. Subprocess providers that rely on user-installed tools
must either use absolute paths or deliberately augment PATH in a tokenizer-safe
way.

## G. Known limitations / candidate Reform chunks

### G1 - Codex CLI provider interactive-mode mismatch (Resolved in v0.25)

`codex-agent start -w` waits for job `status != running`. `codex` CLI is
interactive: it processes the initial prompt, replies, then sits idle waiting
for further user input. The job status stays `running` forever, so `-w` never
returns. The UE chat pump on the parent `/bin/bash` stdout only sees
codex-agent's `"Job started: <uuid>"` header. Codex's actual reply lives in a
tmux pane that the parent never reads.

**Resolution**: v0.25 Reform B uses `codex exec --json --ephemeral` directly.
The provider now reads stdout JSONL events from the child process and emits the
`agent_message` text back to UE Assistant when the one-shot process exits.

### G2 - Tilde not expanded in `CodexBinaryPath` (v0.24.5 candidate)

`FPaths::FileExists` does not expand `~`. v0.25 leaves the Codex CLI preset
path empty, but a user-entered `~/bin/codex` style path still fails until the
user substitutes an absolute path.

Candidate fix: add an `ExpandTildeInPath()` helper in a small shell/path utility
file. Substitute leading `~` with `FPlatformProcess::UserHomeDir()` at config
validation and subprocess execution time.

### G3 - Hardcoded subprocess PATH augmentation list (deferred)

`GetCodexSubprocessPathPrefix()` lists five hardcoded directories:

- `$HOME/.bun/bin`;
- `$HOME/.local/bin`;
- `$HOME/.cargo/bin`;
- `/opt/homebrew/bin`;
- `/usr/local/bin`.

Users with non-standard layouts such as asdf, mise, or nix cannot extend this
list through settings. Candidate fix: add a `Codex Extra PATH Prefix` setting
as a colon-separated `FString` that prepends to the hardcoded list after
validation.

### G4 - Composed prompt command-line length (Resolved in v0.25)

After v0.24.4 Edit B, the composed prompt text flowed directly into `argv[1]`
of the bash subprocess. That exposed an ARG_MAX risk for long multi-turn chat
history.

**Resolution**: v0.25 passes only the latest user message as the final argv
prompt and writes ConversationContext to the child stdin pipe, then closes the
write end to signal EOF.

### G5 - Provider observability is uneven (deferred)

HTTP providers stream structured events and errors through the chat panel, but
subprocess providers still expose less structured failure evidence. The v0.24.4
integration tests protect argv boundaries; they do not solve runtime capture,
tmux pane visibility, or provider-specific diagnostic UX.

Candidate fix: add provider-level diagnostic events with transport, command
builder, stream parser, and tool-call phases. Keep secrets and prompt content
redacted from persisted logs.

### G6 - Codex CLI exec has no token-level streaming (deferred)

`codex exec --json` emits the assistant reply as one complete `item.completed`
event with `item.type == "agent_message"`, not token-level deltas. UE chat shows
the full reply atomically in v0.25. A future enhancement could investigate
whether codex exposes a streaming mode suitable for this provider.
