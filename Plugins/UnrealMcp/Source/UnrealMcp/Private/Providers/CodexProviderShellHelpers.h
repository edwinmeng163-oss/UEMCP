// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"

namespace UnrealMcp::Providers::Internal
{
	// Quote-and-escape a single FString for safe inclusion as one bash word
	// inside a $'...'-wrapped ANSI-C string. The returned FString:
	//   - is always wrapped in $'...' (so embedded spaces are tolerated by
	//     the UE CreateProc tokenizer, which would otherwise split on space)
	//   - contains 0 literal ASCII double-quote (0x22) characters
	//   - escapes \, ', space, tab, CR, LF, " using \\, \', \x20, \t, \r, \n, \x22
	FString QuoteForBashWordNoSpaces(const FString& Value);

	// Join a list of FStrings via "${IFS}" after quoting each via
	// QuoteForBashWordNoSpaces. The returned FString contains 0 literal spaces
	// and 0 literal double-quotes.
	FString JoinShellArgumentsNoSpaces(const TArray<FString>& Args);

	// Compose the prompt text passed to codex-agent. Folds ConversationContext
	// (if non-empty) and the latest UserPrompt into a single FString. This is
	// a pure string operation; no FS or env access.
	FString ComposePrompt(const FString& UserPrompt, const FString& ConversationContext);

	// Build the full <Arguments> payload (without the leading "-c " prefix)
	// for the "start" codex-agent invocation. See CodexProvider.cpp Edit E
	// for the invariants this function guarantees about its return value.
	FString BuildCodexStartCommand(
		const FString& CodexBinaryPath,
		const FString& ComposedPromptText,
		const FString& ProjectAbsoluteDir,
		const TArray<FString>& FilteredExtraArgs);

	// Build the full <Arguments> payload (without the leading "-c " prefix)
	// for the "kill" codex-agent invocation. Symmetric to the above; JobId
	// is regex-restricted upstream via LooksLikeJobId.
	FString BuildCodexKillCommand(
		const FString& CodexBinaryPath,
		const FString& JobId,
		const FString& ProjectAbsoluteDir);
}
