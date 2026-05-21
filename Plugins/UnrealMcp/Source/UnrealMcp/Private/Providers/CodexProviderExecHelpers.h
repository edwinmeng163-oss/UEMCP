// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"

namespace UnrealMcp::Providers::Internal
{
	// ============================================================
	// Shell-quote helpers (unchanged from v0.24.4 ShellHelpers.h)
	// ============================================================

	// Quote-and-escape a single FString for safe inclusion as one bash word
	// inside a $'...'-wrapped ANSI-C string. See Docs/AIProviderArchitecture.md
	// Section B invariants 1-10.
	FString QuoteForBashWordNoSpaces(const FString& Value);

	// Join a list of FStrings via "${IFS}" after quoting each via
	// QuoteForBashWordNoSpaces. Returned FString contains 0 literal spaces.
	FString JoinShellArgumentsNoSpaces(const TArray<FString>& Args);

	// ============================================================
	// Codex exec command builder (v0.25 NEW)
	// ============================================================

	// Build the full <Arguments> payload (sans leading "-c ") for the
	// codex exec invocation:
	//
	//   PATH=...&&${IFS}<codex-binary>${IFS}exec${IFS}--json${IFS}--ephemeral${IFS}
	//   --skip-git-repo-check${IFS}-C${IFS}$'<projdir>'${IFS}
	//   -c${IFS}$'model="gpt-5.5"'${IFS}-c${IFS}$'reasoning_effort="xhigh"'${IFS}
	//   -c${IFS}$'sandbox_mode="workspace-write"'${IFS}<user-extra-args>${IFS}
	//   $'<user-prompt>'
	//
	// ARGUMENT ORDER (critical): codex exec parses as `exec [OPTIONS] [PROMPT]`,
	// so the user prompt MUST be the LAST argv element. All options (--json,
	// --ephemeral, --skip-git-repo-check, -C, -c key=value, plus user FilteredExtraArgs)
	// come BEFORE the prompt.
	//
	// BASELINE -c FLAGS (injected by this helper, always present):
	//   -c model="gpt-5.5"
	//   -c reasoning_effort="xhigh"
	//   -c sandbox_mode="workspace-write"
	//
	// User FilteredExtraArgs are appended AFTER baseline (codex's argv parser
	// takes the LAST occurrence of duplicate keys, so user `-c model="other"`
	// overrides the baseline).
	//
	// OUTPUT INVARIANTS:
	//   - Contains 0 literal ASCII space characters (all parts IFS-joined).
	//   - Contains 0 literal ASCII double-quote (0x22) characters.
	//   - Contains no `export` keyword (invariant 8: bare PATH=... assignment form).
	//
	// The caller wraps this in `FString::Printf(TEXT("-c %s"), *Output)` which
	// introduces the one and only literal space (invariant 1).
	FString BuildCodexExecCommand(
		const FString& CodexBinaryPath,
		const FString& UserPromptOnly,
		const FString& ProjectAbsoluteDir,
		const TArray<FString>& FilteredExtraArgs);

	// ============================================================
	// JSONL stream parser (v0.25 NEW)
	// ============================================================

	enum class ECodexJsonlEventKind : uint8
	{
		ThreadStarted,
		TurnStarted,
		ItemCompleted,
		TurnCompleted,
		Error,
		Unknown,
	};

	struct FCodexJsonlEvent
	{
		ECodexJsonlEventKind Kind = ECodexJsonlEventKind::Unknown;
		FString RawJson;
		FString ThreadId;
		FString ItemType;
		FString ItemText;
		FString ErrorMessage;
		int32 InputTokens = 0;
		int32 OutputTokens = 0;
		int32 CachedInputTokens = 0;
		int32 ReasoningOutputTokens = 0;
	};

	// Parse one complete JSONL line into an event. Returns false on JSON parse
	// failure. Unknown "type" values return true with Kind=Unknown and the
	// raw JSON stashed in RawJson.
	bool ParseCodexJsonlEvent(const FString& Line, FCodexJsonlEvent& OutEvent);

	// ============================================================
	// Predicate helpers (v0.25 NEW, exposed for ValidateConfig testability)
	// ============================================================

	// Detect known auth-failure patterns in a JSONL error.message field OR a
	// raw stderr line.
	bool DetectCodexAuthError(const FString& Chunk);

	// Detect if a CodexBinaryPath value points to the legacy codex-agent wrapper.
	bool LooksLikeLegacyCodexAgentPath(const FString& Path);

	// Detect if a tokenized CodexExtraArgs list contains any -r / --reasoning
	// form (codex exec doesn't accept -r; users must migrate to -c reasoning_effort="...").
	bool ContainsUnsupportedReasoningFlag(const TArray<FString>& Tokens);
}
