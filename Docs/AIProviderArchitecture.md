# AI Provider Architecture (UnrealMcp)

> v0.24.4 - first written. Audience: contributors editing
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
| Codex | Subprocess (bash + codex-agent + codex CLI) | parent stdout pump | OAuth via codex login |
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

- `Codex` launches a local subprocess through `/bin/bash -c`;
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
- **PATH augmentation MUST use the bare assignment form** `PATH=...:$PATH&&${IFS}<cmd>`, NOT `export${IFS}PATH=...&&${IFS}<cmd>`. Rationale: `export VAR=value` is the export BUILTIN which word-splits its single argument (`VAR=value`) after parameter expansion. When inherited `$PATH` contains directories with literal spaces — common on macOS, e.g. `/Library/Application Support/JetBrains/Toolbox/scripts` — the expanded assignment splits at the space and `export` receives multiple args. The second arg is not a valid identifier and fails: `export: 'Support/JetBrains/...': not a valid identifier`. The bare `PATH=value` form (no `export` keyword) is a current-shell variable assignment whose RHS is POSIX-guaranteed NOT word-split post-expansion. `$PATH` is already exported by the parent UE Editor process, so re-assignment preserves the exported status and the new value propagates to child processes via standard env inheritance. (Discovered via the `SpawnedCommandActualBashExec` / `KillCommandActualBashExec` integration tests during v0.24.4 ship-time verification.)

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
// $'...'-wrapped composed prompt
CommandParts.Add(QuoteForBashWordNoSpaces(ComposePrompt(UserPrompt, ConversationContext)));

// PATH wrapper with no literal "
"export${IFS}PATH=$HOME/.bun/bin:...:$PATH&&${IFS}"

// separate IFS-joined args
CommandParts.Add(TEXT("-m"));
CommandParts.Add(QuoteForBashWordNoSpaces(ForcedCodexModel));
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

### Helper ownership

The shell helpers are production code and test surface:

- `QuoteForBashWordNoSpaces` escapes one value and wraps it in `$'...'`.
- `JoinShellArgumentsNoSpaces` joins quoted values with `${IFS}`.
- `ComposePrompt` folds prior conversation context into the latest user prompt.
- `BuildCodexStartCommand` builds the codex-agent `start` command payload.
- `BuildCodexKillCommand` builds the codex-agent `kill` command payload.

The helper declarations live in the private header
`Providers/CodexProviderShellHelpers.h` so integration tests can call the same
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

- v0.24.4 tests spawn `/bin/bash -c` with the production-built command;
- the fake binary is an argc/argv-printing helper script;
- stdout is parsed for `ARGC=` and `ARG=` lines;
- prompt content is verified after the real shell and UE process tokenizer path;
- both `start` and `kill` command builders are exercised.

Pure string tests are not enough for CreateProc-bound command strings.
v0.24.3's subprocess PATH prefix test passed because the string contained the
intended directories, but the real exec failed at the tokenizer layer that no
pure-string test exercised.

Rule of thumb: any `FString` that ultimately flows into
`FPlatformProcess::CreateProc` needs at minimum one integration test that
execs the constructed command against a deterministic helper and asserts argv
boundaries.

The tests intentionally use an argc/argv printer rather than `/bin/echo`.
`echo` collapses argv into one space-separated output line, which cannot
distinguish one argument containing an embedded space from two arguments.

## F. Known UE gotchas

`UnixPlatformProcess.cpp:ParseCmdLineToken` treats `"` as a quote delimiter and
captures double-double-quoted text into argv. See Section B.

`UObjectGlobals.cpp:PostInitPropertiesCheck` has thread-context expectations.
Direct re-invocation in test fixtures can assert at runtime. Extract a
non-`PostInitProperties` helper instead.

`FPaths::FileExists` does not expand `~` (tilde). The preset default for
`CodexBinaryPath`, `~/codex-orchestrator/bin/codex-agent`, is a literal path.
Users must currently substitute the absolute path each time the preset
auto-fills. See Section G2.

`meta=(DeprecatedProperty)` does not hide a `UPROPERTY` from the Property
Editor. Remove `EditAnywhere` for deprecated config values that should still
deserialize but should not be edited in the UI.

macOS GUI launches often inherit a minimal default PATH rather than the user's
interactive shell PATH. Subprocess providers that rely on user-installed tools
must either use absolute paths or deliberately augment PATH in a tokenizer-safe
way.

## G. Known limitations / candidate Reform chunks

### G1 - Codex CLI provider interactive-mode mismatch (v0.25 Reform B candidate)

`codex-agent start -w` waits for job `status != running`. `codex` CLI is
interactive: it processes the initial prompt, replies, then sits idle waiting
for further user input. The job status stays `running` forever, so `-w` never
returns. The UE chat pump on the parent `/bin/bash` stdout only sees
codex-agent's `"Job started: <uuid>"` header. Codex's actual reply lives in a
tmux pane that the parent never reads.

**Workaround**: use the Codex Desktop bridge, the `CodexAppServer` provider,
which talks over WebSocket to the local Codex Desktop bridge. PM validated this
path and it works today.

**Planned fix (Reform B)**: rewrite `SpawnProcess` to use one of:

- `codex exec <prompt>` one-shot mode. Codex prints the reply to stdout and
  exits 0. This is cleaner but loses the codex-agent job-control surface.
- `codex-agent start ... && codex-agent await-turn <jobId> && codex-agent capture <jobId>`.
  This preserves codex-agent job tracking, but requires parsing `await-turn`
  exit semantics and `capture` stdout format.

PM decision is deferred to v0.25 chunk planning.

### G2 - Tilde not expanded in `CodexBinaryPath` (v0.24.5 candidate)

`FPaths::FileExists` does not expand `~`. The preset default
`~/codex-orchestrator/bin/codex-agent` works only after the user manually edits
it to an absolute path.

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

### G4 - Composed prompt command-line length (accepted limit)

After v0.24.4 Edit B, the composed prompt text flows directly into `argv[1]` of
the bash subprocess. macOS `ARG_MAX` is roughly 256 KiB, and Apple Silicon
macOS 11+ is roughly 1 MiB. A 50-turn chat history with the standard system
prompt envelope is typically 50-200 KiB, which fits comfortably.

This is accepted for v0.24.4. Revisit only if users report hitting the cap. The
Reform B rewrite would likely pipe prompt text through stdin or use a one-shot
Codex command that owns prompt transport explicitly.

### G5 - Provider observability is uneven (deferred)

HTTP providers stream structured events and errors through the chat panel, but
subprocess providers still expose less structured failure evidence. The v0.24.4
integration tests protect argv boundaries; they do not solve runtime capture,
tmux pane visibility, or provider-specific diagnostic UX.

Candidate fix: add provider-level diagnostic events with transport, command
builder, stream parser, and tool-call phases. Keep secrets and prompt content
redacted from persisted logs.
