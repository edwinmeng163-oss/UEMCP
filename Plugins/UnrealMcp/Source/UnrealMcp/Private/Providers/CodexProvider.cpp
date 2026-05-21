#include "Providers/CodexProvider.h"

#include "Providers/CodexProviderExecHelpers.h"
#include "Providers/ProviderHelpers.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Providers/UnrealMcpApprovalPolicy.h"
#include <atomic>

// =====================================================================
// CodexProvider shell-quoting invariants (v0.25 - authoritative copy
// of Docs/AIProviderArchitecture.md Section B; keep both in sync)
// =====================================================================
// The full Arguments string passed to FPlatformProcess::CreateProc as the
// Arguments parameter, for the bash subprocess that wraps codex exec,
// must satisfy ALL of the following invariants:
//
// - The full `Arguments` string passed to `FPlatformProcess::CreateProc` has
//   exactly one literal ASCII space: the separator in `-c <command>`.
// - The full `Arguments` string contains zero literal ASCII double-quote
//   characters (`0x22`).
// - Trusted generated shell fragments MAY contain required shell syntax:
//   assignment `=`, `$HOME`, `$PATH`, `${IFS}`, `&&`, and `$'...'`
//   ANSI-C single-quote delimiters. NOTE: the `export` builtin is forbidden
//   for PATH augmentation (see invariant 8 below).
// - User-supplied or config-derived values MUST NEVER be concatenated raw into
//   the command.
// - User-supplied or config-derived values MUST appear only as output of
//   `QuoteForBashWordNoSpaces(...)`, or be rejected upstream by
//   `ContainsDangerousShellCharacters(...)`.
// - Command parts MUST be joined with `${IFS}`, never with literal ASCII space.
// - The command MUST NOT use `"..."` double-quote wrapping, `"$(cat ...)"`
//   substitution, or any other construct that introduces literal `"`
//   characters.
// - PATH augmentation MUST use the bare assignment form
//   `PATH=...:$PATH&&${IFS}<cmd>` (NOT `export${IFS}PATH=...&&${IFS}<cmd>`).
//   Reason: `export VAR=value` is the export BUILTIN, which word-splits its
//   single argument (`VAR=value`) after parameter expansion. When inherited
//   $PATH contains directories with literal spaces (e.g. macOS
//   `/Library/Application Support/JetBrains/Toolbox/scripts`), the expanded
//   assignment splits at the space and `export` receives multiple args, the
//   second of which is not a valid identifier and fails:
//     `export: 'Support/JetBrains/...': not a valid identifier`.
//   The bare `PATH=value` form (no `export` keyword) is a current-shell
//   variable assignment and POSIX guarantees its RHS is NOT word-split
//   post-expansion. PATH is already exported by the parent UE Editor
//   process, so re-assignment preserves the exported status and the new
//   value propagates to child processes via standard env inheritance.
// - ConversationContext for multi-turn chat MUST be passed via the child
//   subprocess's stdin pipe (closed after write to signal EOF), NOT
//   concatenated into the argv prompt. Argv prompt contains only the latest
//   user message. Rationale: avoids ARG_MAX cap; clean <stdin> block
//   separation in codex's prompt envelope.
// - Cancellation MUST use FPlatformProcess::TerminateProc(handle,
//   /*killTree=*/true) directly on the bash subprocess handle, not a separate
//   kill <jobId> subprocess command.
//
// Rationale: UE Unix CreateProc parses Arguments via ParseCmdLineToken in
// Engine/Source/Runtime/Core/Private/Unix/UnixPlatformProcess.cpp, which
// treats '"' as a quote delimiter and captures double-double-quoted text
// into argv. Any literal " in Arguments mangles the bash -c payload.
//
// Case studies:
//   - v0.24.3 introduced an invariant-(2) and -(7) violation via
//     literal "..." wrapping around the PATH= assignment; produced
//     "bash: -c: option requires an argument" at runtime.
//   - v0.24.4 Edit A removed that wrapping. Edit B removed a parallel
//     pre-existing invariant-(2) and -(7) violation in the prompt-pass
//     cat-substitution at line 577. The Edit-A change exposed an
//     invariant-(8) gap (`export` builtin word-splits $PATH containing
//     spaces); the v0.24.4 ship-time fix changed `export${IFS}PATH=`
//     to the bare `PATH=` assignment form and added the explicit
//     invariant (8) above.
//   - v0.25 Reform B replaced codex-agent + tmux + bun with direct
//     codex exec + 3 pipes + worker-thread chunked stdin write. Drops
//     3 runtime dependencies; resolves G1 (interactive-mode mismatch)
//     + G4 (composed prompt argv length).
// =====================================================================

namespace UnrealMcp::Providers
{
	const FString& GetCodexSubprocessPathPrefix();
}

namespace UnrealMcp::Providers::Internal
{
	FString QuoteForBashWordNoSpaces(const FString& Value)
	{
		FString Escaped;
		Escaped.Reserve(Value.Len() + 8);
		for (const TCHAR Character : Value)
		{
			switch (Character)
			{
			case TEXT('\\'):
				Escaped += TEXT("\\\\");
				break;
			case TEXT('\''):
				Escaped += TEXT("\\'");
				break;
			case TEXT(' '):
				Escaped += TEXT("\\x20");
				break;
			case TEXT('\t'):
				Escaped += TEXT("\\t");
				break;
			case TEXT('\n'):
				Escaped += TEXT("\\n");
				break;
			case TEXT('\r'):
				Escaped += TEXT("\\r");
				break;
			case TEXT('"'):
				Escaped += TEXT("\\x22");
				break;
			default:
				Escaped.AppendChar(Character);
				break;
			}
		}
		return FString::Printf(TEXT("$'%s'"), *Escaped);
	}

	FString JoinShellArgumentsNoSpaces(const TArray<FString>& Args)
	{
		TArray<FString> QuotedArgs;
		QuotedArgs.Reserve(Args.Num());
		for (const FString& Arg : Args)
		{
			QuotedArgs.Add(QuoteForBashWordNoSpaces(Arg));
		}
		return FString::Join(QuotedArgs, TEXT("${IFS}"));
	}

	FString BuildCodexExecCommand(
		const FString& CodexBinaryPath,
		const FString& UserPromptOnly,
		const FString& ProjectAbsoluteDir,
		const TArray<FString>& FilteredExtraArgs)
	{
		static const TCHAR* const ForcedCodexModel = TEXT("gpt-5.5");
		static const TCHAR* const ForcedCodexReasoning = TEXT("xhigh");
		static const TCHAR* const ForcedCodexSandbox = TEXT("workspace-write");

		TArray<FString> CommandParts;
		CommandParts.Add(QuoteForBashWordNoSpaces(CodexBinaryPath));
		CommandParts.Add(TEXT("exec"));
		CommandParts.Add(TEXT("--json"));
		CommandParts.Add(TEXT("--ephemeral"));
		CommandParts.Add(TEXT("--skip-git-repo-check"));
		CommandParts.Add(TEXT("-C"));
		CommandParts.Add(QuoteForBashWordNoSpaces(ProjectAbsoluteDir));
		CommandParts.Add(TEXT("-c"));
		CommandParts.Add(QuoteForBashWordNoSpaces(FString::Printf(TEXT("model=\"%s\""), ForcedCodexModel)));
		CommandParts.Add(TEXT("-c"));
		CommandParts.Add(QuoteForBashWordNoSpaces(FString::Printf(TEXT("reasoning_effort=\"%s\""), ForcedCodexReasoning)));
		CommandParts.Add(TEXT("-c"));
		CommandParts.Add(QuoteForBashWordNoSpaces(FString::Printf(TEXT("sandbox_mode=\"%s\""), ForcedCodexSandbox)));
		if (!FilteredExtraArgs.IsEmpty())
		{
			CommandParts.Add(JoinShellArgumentsNoSpaces(FilteredExtraArgs));
		}
		CommandParts.Add(QuoteForBashWordNoSpaces(UserPromptOnly));

		return UnrealMcp::Providers::GetCodexSubprocessPathPrefix()
			+ FString::Join(CommandParts, TEXT("${IFS}"));
	}

	namespace
	{
		int32 ReadIntField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
		{
			double Number = 0.0;
			if (Object.IsValid() && Object->TryGetNumberField(FieldName, Number))
			{
				return FMath::TruncToInt(Number);
			}
			return 0;
		}
	}

	bool ParseCodexJsonlEvent(const FString& Line, FCodexJsonlEvent& OutEvent)
	{
		OutEvent = FCodexJsonlEvent();
		OutEvent.RawJson = Line;

		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<TCHAR>> Reader = TJsonReaderFactory<TCHAR>::Create(Line);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			return false;
		}

		FString Type;
		if (!RootObject->TryGetStringField(TEXT("type"), Type))
		{
			return true;
		}

		if (Type == TEXT("thread.started"))
		{
			OutEvent.Kind = ECodexJsonlEventKind::ThreadStarted;
			RootObject->TryGetStringField(TEXT("thread_id"), OutEvent.ThreadId);
			return true;
		}

		if (Type == TEXT("turn.started"))
		{
			OutEvent.Kind = ECodexJsonlEventKind::TurnStarted;
			return true;
		}

		if (Type == TEXT("item.completed"))
		{
			OutEvent.Kind = ECodexJsonlEventKind::ItemCompleted;
			const TSharedPtr<FJsonObject>* ItemObject = nullptr;
			if (RootObject->TryGetObjectField(TEXT("item"), ItemObject) && ItemObject && ItemObject->IsValid())
			{
				(*ItemObject)->TryGetStringField(TEXT("type"), OutEvent.ItemType);
				(*ItemObject)->TryGetStringField(TEXT("text"), OutEvent.ItemText);
			}
			return true;
		}

		if (Type == TEXT("turn.completed"))
		{
			OutEvent.Kind = ECodexJsonlEventKind::TurnCompleted;
			const TSharedPtr<FJsonObject>* UsageObject = nullptr;
			if (RootObject->TryGetObjectField(TEXT("usage"), UsageObject) && UsageObject && UsageObject->IsValid())
			{
				OutEvent.InputTokens = ReadIntField(*UsageObject, TEXT("input_tokens"));
				OutEvent.OutputTokens = ReadIntField(*UsageObject, TEXT("output_tokens"));
				OutEvent.CachedInputTokens = ReadIntField(*UsageObject, TEXT("cached_input_tokens"));
				OutEvent.ReasoningOutputTokens = ReadIntField(*UsageObject, TEXT("reasoning_output_tokens"));
			}
			return true;
		}

		if (Type == TEXT("error"))
		{
			OutEvent.Kind = ECodexJsonlEventKind::Error;
			if (!RootObject->TryGetStringField(TEXT("message"), OutEvent.ErrorMessage))
			{
				RootObject->TryGetStringField(TEXT("error"), OutEvent.ErrorMessage);
			}
			return true;
		}

		return true;
	}

	bool DetectCodexAuthError(const FString& Chunk)
	{
		return Chunk.Contains(TEXT("401 Unauthorized"), ESearchCase::IgnoreCase)
			|| Chunk.Contains(TEXT("Missing bearer or basic authentication"), ESearchCase::IgnoreCase)
			|| Chunk.Contains(TEXT("Reconnecting"), ESearchCase::IgnoreCase);
	}

	bool LooksLikeLegacyCodexAgentPath(const FString& Path)
	{
		FString Trimmed = Path.TrimStartAndEnd();
		while (Trimmed.EndsWith(TEXT("/")) || Trimmed.EndsWith(TEXT("\\")))
		{
			Trimmed.LeftChopInline(1, EAllowShrinking::No);
		}
		if (Trimmed.IsEmpty())
		{
			return false;
		}
		return FPaths::GetCleanFilename(Trimmed).Equals(TEXT("codex-agent"), ESearchCase::IgnoreCase);
	}

	bool ContainsUnsupportedReasoningFlag(const TArray<FString>& Tokens)
	{
		for (const FString& Token : Tokens)
		{
			if (Token == TEXT("-r")
				|| (Token.StartsWith(TEXT("-r")) && Token.Len() > 2)
				|| Token == TEXT("--reasoning")
				|| Token.StartsWith(TEXT("--reasoning=")))
			{
				return true;
			}
		}
		return false;
	}
}

namespace
{
#if PLATFORM_WINDOWS
	const TCHAR* CodexCliWindowsUnsupportedMessage = TEXT("Codex CLI provider is not supported on Windows. Use the CodexAppServer (Codex Desktop bridge) provider instead. See Docs/Release-2026-05.md.");

	void ReportCodexCliWindowsUnsupported(FString& OutError)
	{
		OutError = CodexCliWindowsUnsupportedMessage;
		UE_LOG(LogUnrealMcp, Warning, TEXT("%s"), CodexCliWindowsUnsupportedMessage);
	}
#endif

	bool ContainsDangerousShellCharacters(const FString& Value, FString& OutFailureReason)
	{
		TArray<FString> DisallowedCharacters;
		for (const TCHAR Character : Value)
		{
			if (Character == TEXT('\n'))
			{
				DisallowedCharacters.AddUnique(TEXT("\\n"));
				continue;
			}
			if (Character == TEXT('\r'))
			{
				DisallowedCharacters.AddUnique(TEXT("\\r"));
				continue;
			}
			if (FCString::Strchr(TEXT(";|&`$()><"), Character) != nullptr)
			{
				DisallowedCharacters.AddUnique(FString::Printf(TEXT("'%c'"), Character));
			}
		}
		if (DisallowedCharacters.Num() > 0)
		{
			OutFailureReason = FString::Printf(
				TEXT("contains disallowed shell metacharacters: %s"),
				*FString::Join(DisallowedCharacters, TEXT(", ")));
			return true;
		}

		return false;
	}

	bool TokenizeExtraArgs(const FString& InExtraArgs, TArray<FString>& OutTokens, FString& OutError)
	{
		FString FailureReason;
		if (ContainsDangerousShellCharacters(InExtraArgs, FailureReason))
		{
			OutError = FString::Printf(TEXT("CodexExtraArgs %s"), *FailureReason);
			return false;
		}

		FString Current;
		bool bInSingleQuote = false;
		bool bInDoubleQuote = false;
		for (int32 Index = 0; Index < InExtraArgs.Len(); ++Index)
		{
			const TCHAR Character = InExtraArgs[Index];
			if (Character == TEXT('\'') && !bInDoubleQuote)
			{
				bInSingleQuote = !bInSingleQuote;
				Current.AppendChar(Character);
				continue;
			}
			if (Character == TEXT('"') && !bInSingleQuote)
			{
				bInDoubleQuote = !bInDoubleQuote;
				Current.AppendChar(Character);
				continue;
			}
			if (FChar::IsWhitespace(Character) && !bInSingleQuote && !bInDoubleQuote)
			{
				if (!Current.IsEmpty())
				{
					OutTokens.Add(Current);
					Current.Reset();
				}
				continue;
			}
			Current.AppendChar(Character);
		}
		if (bInSingleQuote || bInDoubleQuote)
		{
			OutError = TEXT("CodexExtraArgs contains an unterminated quoted argument.");
			return false;
		}
		if (!Current.IsEmpty())
		{
			OutTokens.Add(Current);
		}
		return true;
	}

	// TODO(v0.26+): reuse if -c flag enforcement is needed.
	bool ValidateFlagValue(
		const FAiProviderConfig& Config,
		const FString& FlagName,
		const FString& Value,
		const FString& RequiredValue,
		FString& OutError)
	{
		if (!Value.Equals(RequiredValue, ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(
				TEXT("Provider '%s': CodexExtraArgs must not override %s away from '%s' (got '%s')."),
				*UnrealMcp::Providers::ProviderIdForError(Config),
				*FlagName,
				*RequiredValue,
				*Value);
			return false;
		}
		return true;
	}

	class FCodexRun final : public IUnrealMcpAssistantHandle, public FRunnable, public TSharedFromThis<FCodexRun, ESPMode::ThreadSafe>
	{
	public:
		FCodexRun(
			FAiProviderConfig InConfig,
			const UUnrealMcpSettings& InSettings,
			const FUnrealMcpModule* InModule,
			FString InUserPrompt,
			FString InConversationContext,
			FString InPreviousResponseId,
			TFunction<void(const FUnrealMcpAssistantEvent&)> InOnEvent,
			TFunction<void(const FUnrealMcpAssistantTurnResult&)> InOnComplete)
			: Config(MoveTemp(InConfig))
			, Settings(&InSettings)
			, Module(InModule)
			, UserPrompt(MoveTemp(InUserPrompt))
			, ConversationContext(MoveTemp(InConversationContext))
			, PreviousResponseId(MoveTemp(InPreviousResponseId))
			, OnEvent(MoveTemp(InOnEvent))
			, OnComplete(MoveTemp(InOnComplete))
		{
		}

		virtual ~FCodexRun() override
		{
			bCancellationRequested.store(true, std::memory_order_relaxed);
			TerminateProcessIfRunning();
			if (PumpThread)
			{
				PumpThread->WaitForCompletion();
				delete PumpThread;
				PumpThread = nullptr;
			}
			CleanupProcessResources();
		}

		void Start()
		{
			SelfKeepAlive = AsShared();
			check(Settings);
			static_cast<void>(Module);
			// Codex CLI is one-shot: previous_response_id is ignored and compressed chat history goes through stdin.
			static_cast<void>(PreviousResponseId);

			if (!Settings->bEnableAiAssistant)
			{
				Finish(TEXT("AI assistant is disabled. Enable it in Project Settings > Plugins > Unreal MCP > AI."), true);
				return;
			}

			FString Error;
			TArray<FString> FilteredExtraArgs;
			if (!ValidateCodexConfig(Config, FilteredExtraArgs, Error))
			{
				Finish(Error, true);
				return;
			}

			if (!SpawnProcess(FilteredExtraArgs, Error))
			{
				Finish(Error, true);
				return;
			}

			EmitStatus(TEXT("Started local Codex exec."));
			PumpThread = FRunnableThread::Create(this, TEXT("UnrealMcpCodexExecPump"));
			if (!PumpThread)
			{
				TerminateProcessIfRunning();
				Finish(TEXT("Failed to start Codex exec pump thread."), true);
			}
		}

		virtual void Cancel() override
		{
			if (bCompleted.load(std::memory_order_acquire))
			{
				return;
			}
			bCancellationRequested.store(true, std::memory_order_relaxed);
			TerminateProcessIfRunning();
			Finish(TEXT("Generation stopped."), false, true);
		}

		virtual bool Steer(const FString& Instruction) override
		{
			const FString Trimmed = Instruction.TrimStartAndEnd();
			if (Trimmed.IsEmpty()
				|| bCompleted.load(std::memory_order_acquire)
				|| bCancellationRequested.load(std::memory_order_relaxed))
			{
				return false;
			}

			{
				const FScopeLock Lock(&StateMutex);
				PendingSteerInstructions.Add(Trimmed);
			}
			EmitStatus(TEXT("Steering is queued for the next turn; the Codex provider does not currently support mid-run guidance."));
			return false;
		}

		virtual void ResolveAssistantApproval(const FString& ApprovalIdString, bool bApproved) override
		{
			FGuid ApprovalId;
			if (FGuid::Parse(ApprovalIdString, ApprovalId))
			{
				UnrealMcp::Approval::ResolveApproval(
					ApprovalId,
					bApproved ? UnrealMcp::Approval::EUserDecision::Approved : UnrealMcp::Approval::EUserDecision::Rejected);
			}
		}

		virtual uint32 Run() override
		{
			WriteConversationContextToStdin();

			while (!bCancellationRequested.load(std::memory_order_relaxed))
			{
				DrainPipesOnce();
				if (!IsProcessRunning())
				{
					break;
				}
				FPlatformProcess::Sleep(0.05f);
			}

			for (int32 DrainAttempt = 0; DrainAttempt < 10; ++DrainAttempt)
			{
				DrainPipesOnce();
				FPlatformProcess::Sleep(0.01f);
			}
			FlushPartialPipeLines();

			if (bCompleted.load(std::memory_order_acquire))
			{
				return 0;
			}

			if (bCancellationRequested.load(std::memory_order_relaxed))
			{
				Finish(TEXT("Generation stopped."), false, true);
				return 0;
			}

			int32 ReturnCode = -1;
			const bool bHasReturnCode = GetProcessReturnCode(ReturnCode);
			FString FinalText;
			{
				const FScopeLock Lock(&StateMutex);
				FinalText = AccumulatedText;
			}

			if (!bHasReturnCode || ReturnCode != 0 || !bTurnCompletedSeen)
			{
				if (!bAuthErrorAlreadyEmitted)
				{
					const FString StderrTail = StderrRecentText.Right(200);
					FUnrealMcpAssistantEvent ErrEvent;
					ErrEvent.Type = EUnrealMcpAssistantEventType::Status;  // v0.25: EUnrealMcpAssistantEventType has no Error variant; chat panel renders Status text. The Text field below carries the explicit "Codex CLI error:" / "is not logged in" / "exited with code" prefix so users see the error wording. Terminal failure also sets bIsError=true on FUnrealMcpAssistantTurnResult in EmitTurnResult.
					ErrEvent.Text = FString::Printf(
						TEXT("Codex CLI exited with code %d%s. Stderr (last 200 chars): %s"),
						ReturnCode,
						bTurnCompletedSeen ? TEXT("") : TEXT(" before turn.completed"),
						*StderrTail);
					EmitEvent(ErrEvent);
					FinalText = ErrEvent.Text;
				}
				else if (FinalText.TrimStartAndEnd().IsEmpty())
				{
					FinalText = TEXT("Codex CLI is not logged in. Run 'codex login' in a terminal first, then retry the chat.");
				}
				Finish(FinalText, true);
				return 0;
			}

			if (FinalText.TrimStartAndEnd().IsEmpty())
			{
				FinalText = TEXT("Codex turn completed without producing an agent_message.");
			}
			Finish(FinalText, false);
			return 0;
		}

		virtual void Stop() override
		{
			bCancellationRequested.store(true, std::memory_order_relaxed);
		}

		static bool ValidateCodexConfig(const FAiProviderConfig& InConfig, TArray<FString>& OutFilteredExtraArgs, FString& OutError)
		{
#if PLATFORM_WINDOWS
			static_cast<void>(InConfig);
			static_cast<void>(OutFilteredExtraArgs);
			ReportCodexCliWindowsUnsupported(OutError);
			return false;
#else
			const FString TrimmedBinaryPath = InConfig.CodexBinaryPath.TrimStartAndEnd();

			if (TrimmedBinaryPath.IsEmpty())
			{
				OutError = FString::Printf(
					TEXT("Provider '%s': Codex Binary Path is empty. Set it to the codex CLI binary (find it with 'which codex' in a terminal)."),
					*InConfig.Id);
				return false;
			}

			FString FailureReason;
			if (ContainsDangerousShellCharacters(TrimmedBinaryPath, FailureReason))
			{
				OutError = FString::Printf(TEXT("Provider '%s': Codex Binary Path %s"), *InConfig.Id, *FailureReason);
				return false;
			}

			if (::UnrealMcp::Providers::Internal::LooksLikeLegacyCodexAgentPath(TrimmedBinaryPath))
			{
				OutError = FString::Printf(
					TEXT("Provider '%s': Codex Binary Path points to the legacy codex-agent wrapper. v0.25 uses codex exec directly; "
						 "set Codex Binary Path to the codex CLI binary (e.g. /opt/homebrew/bin/codex on Apple Silicon Homebrew, "
						 "or run 'which codex' in a terminal). codex-agent / bun / tmux are no longer required for the Codex CLI provider. "
						 "See Docs/AIProviderArchitecture.md Section G (G1) for the architectural change rationale."),
					*InConfig.Id);
				return false;
			}

			if (!FPaths::FileExists(TrimmedBinaryPath))
			{
				OutError = FString::Printf(TEXT("Provider '%s': Codex Binary Path does not exist: %s"), *InConfig.Id, *TrimmedBinaryPath);
				return false;
			}

			TArray<FString> Tokens;
			if (!TokenizeExtraArgs(InConfig.CodexExtraArgs, Tokens, OutError))
			{
				OutError = FString::Printf(TEXT("Provider '%s': %s"), *InConfig.Id, *OutError);
				return false;
			}

			if (::UnrealMcp::Providers::Internal::ContainsUnsupportedReasoningFlag(Tokens))
			{
				OutError = FString::Printf(
					TEXT("Provider '%s': Codex Extra Args contains an unsupported -r/--reasoning flag. v0.25 uses codex exec which does NOT accept -r. "
						 "Remove '-r <value>' (and any '-m gpt-5.5 -r xhigh'-style v0.24 default), or rewrite as '-c reasoning_effort=\"<value>\"'."),
					*InConfig.Id);
				return false;
			}

			OutFilteredExtraArgs = MoveTemp(Tokens);
			return true;
#endif
		}

	private:
		bool SpawnProcess(const TArray<FString>& FilteredExtraArgs, FString& OutError)
		{
			const FString BashPath = TEXT("/bin/bash");
			if (!FPaths::FileExists(BashPath))
			{
				OutError = TEXT("Codex provider requires /bin/bash.");
				return false;
			}

			if (!FPlatformProcess::CreatePipe(StdoutReadPipe, StdoutWritePipe))
			{
				OutError = TEXT("Failed to create stdout pipe.");
				return false;
			}
			if (!FPlatformProcess::CreatePipe(StderrReadPipe, StderrWritePipe))
			{
				OutError = TEXT("Failed to create stderr pipe.");
				CleanupProcessResources();
				return false;
			}
			if (!FPlatformProcess::CreatePipe(StdinReadPipe, StdinWritePipe, true))
			{
				OutError = TEXT("Failed to create stdin pipe.");
				CleanupProcessResources();
				return false;
			}

			const FString ProjectAbsoluteDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			const FString Command = ::UnrealMcp::Providers::Internal::BuildCodexExecCommand(
				Config.CodexBinaryPath.TrimStartAndEnd(),
				UserPrompt,
				ProjectAbsoluteDir,
				FilteredExtraArgs);
			const FString Arguments = FString::Printf(TEXT("-c %s"), *Command);

			uint32 ProcessId = 0;
			ProcessHandle = FPlatformProcess::CreateProc(
				*BashPath,
				*Arguments,
				false,
				true,
				true,
				&ProcessId,
				0,
				*ProjectAbsoluteDir,
				StdoutWritePipe,
				StdinReadPipe,
				StderrWritePipe);

			if (!ProcessHandle.IsValid())
			{
				CleanupProcessResources();
				OutError = TEXT("Failed to launch codex CLI subprocess.");
				return false;
			}

			return true;
		}

		void WriteConversationContextToStdin()
		{
			if (!StdinWritePipe)
			{
				return;
			}

			const FTCHARToUTF8 Utf8(*ConversationContext);
			const int32 TotalBytes = Utf8.Length();
			const uint8* DataPtr = reinterpret_cast<const uint8*>(Utf8.Get());

			constexpr int32 ChunkSize = 16 * 1024;
			int32 BytesWritten = 0;

			while (BytesWritten < TotalBytes && !bCancellationRequested.load(std::memory_order_relaxed))
			{
				const int32 BytesToWrite = FMath::Min(ChunkSize, TotalBytes - BytesWritten);
				int32 BytesActuallyWritten = 0;
				const bool bSuccess = FPlatformProcess::WritePipe(
					StdinWritePipe,
					DataPtr + BytesWritten,
					BytesToWrite,
					&BytesActuallyWritten);

				if (!bSuccess || BytesActuallyWritten <= 0)
				{
					UE_LOG(LogUnrealMcp, Warning,
						TEXT("CodexProvider: stdin write failed after %d/%d bytes; closing stdin and continuing without remaining context."),
						BytesWritten, TotalBytes);
					break;
				}

				BytesWritten += BytesActuallyWritten;
			}

			FPlatformProcess::ClosePipe(nullptr, StdinWritePipe);
			StdinWritePipe = nullptr;
		}

		void DrainPipesOnce()
		{
			if (StdoutReadPipe)
			{
				const FString StdoutChunk = FPlatformProcess::ReadPipe(StdoutReadPipe);
				if (!StdoutChunk.IsEmpty())
				{
					StdoutLineBuffer += StdoutChunk;
					ConsumeStdoutBufferLines();
				}
			}

			if (StderrReadPipe)
			{
				const FString StderrChunk = FPlatformProcess::ReadPipe(StderrReadPipe);
				if (!StderrChunk.IsEmpty())
				{
					StderrLineBuffer += StderrChunk;
					StderrRecentText += StderrChunk;
					if (StderrRecentText.Len() > 2000)
					{
						StderrRecentText = StderrRecentText.Right(2000);
					}
					ConsumeStderrBufferLines();
				}
			}
		}

		void ConsumeStdoutBufferLines()
		{
			int32 NewlineIdx = INDEX_NONE;
			while (StdoutLineBuffer.FindChar(TEXT('\n'), NewlineIdx))
			{
				FString Line = StdoutLineBuffer.Left(NewlineIdx);
				StdoutLineBuffer.RemoveAt(0, NewlineIdx + 1, EAllowShrinking::No);
				Line.TrimEndInline();
				if (Line.IsEmpty())
				{
					continue;
				}

				::UnrealMcp::Providers::Internal::FCodexJsonlEvent Event;
				if (!::UnrealMcp::Providers::Internal::ParseCodexJsonlEvent(Line, Event))
				{
					UE_LOG(LogUnrealMcp, Verbose, TEXT("CodexProvider: skipping malformed JSONL: %s"), *Line);
					continue;
				}

				DispatchCodexJsonlEvent(Event);
			}
		}

		void ConsumeStderrBufferLines()
		{
			int32 NewlineIdx = INDEX_NONE;
			while (StderrLineBuffer.FindChar(TEXT('\n'), NewlineIdx))
			{
				FString Line = StderrLineBuffer.Left(NewlineIdx);
				StderrLineBuffer.RemoveAt(0, NewlineIdx + 1, EAllowShrinking::No);
				Line.TrimEndInline();
				if (Line.IsEmpty())
				{
					continue;
				}

				UE_LOG(LogUnrealMcp, Verbose, TEXT("CodexProvider stderr: %s"), *Line);
				if (::UnrealMcp::Providers::Internal::DetectCodexAuthError(Line))
				{
					EmitFriendlyAuthError();
				}
			}
		}

		void FlushPartialPipeLines()
		{
			FString StdoutTail = StdoutLineBuffer;
			StdoutLineBuffer.Reset();
			StdoutTail.TrimEndInline();
			if (!StdoutTail.IsEmpty())
			{
				::UnrealMcp::Providers::Internal::FCodexJsonlEvent Event;
				if (::UnrealMcp::Providers::Internal::ParseCodexJsonlEvent(StdoutTail, Event))
				{
					DispatchCodexJsonlEvent(Event);
				}
				else
				{
					UE_LOG(LogUnrealMcp, Verbose, TEXT("CodexProvider: skipping malformed trailing JSONL: %s"), *StdoutTail);
				}
			}

			FString StderrTail = StderrLineBuffer;
			StderrLineBuffer.Reset();
			StderrTail.TrimEndInline();
			if (!StderrTail.IsEmpty())
			{
				UE_LOG(LogUnrealMcp, Verbose, TEXT("CodexProvider stderr: %s"), *StderrTail);
				if (::UnrealMcp::Providers::Internal::DetectCodexAuthError(StderrTail))
				{
					EmitFriendlyAuthError();
				}
			}
		}

		void DispatchCodexJsonlEvent(const ::UnrealMcp::Providers::Internal::FCodexJsonlEvent& Event)
		{
			using EKind = ::UnrealMcp::Providers::Internal::ECodexJsonlEventKind;
			switch (Event.Kind)
			{
			case EKind::ThreadStarted:
				CodexThreadId = Event.ThreadId;
				break;
			case EKind::TurnStarted:
				EmitStatus(TEXT("Codex turn started."));
				break;
			case EKind::ItemCompleted:
				if (Event.ItemType == TEXT("agent_message") && !Event.ItemText.IsEmpty())
				{
					{
						const FScopeLock Lock(&StateMutex);
						AccumulatedText += Event.ItemText;
					}
					FUnrealMcpAssistantEvent OutEvent;
					OutEvent.Type = EUnrealMcpAssistantEventType::TextDelta;
					OutEvent.Text = Event.ItemText;
					EmitEvent(OutEvent);
				}
				break;
			case EKind::TurnCompleted:
				bTurnCompletedSeen = true;
				break;
			case EKind::Error:
				if (::UnrealMcp::Providers::Internal::DetectCodexAuthError(Event.ErrorMessage))
				{
					EmitFriendlyAuthError();
				}
				else
				{
					FUnrealMcpAssistantEvent ErrEvent;
					ErrEvent.Type = EUnrealMcpAssistantEventType::Status;  // v0.25: EUnrealMcpAssistantEventType has no Error variant; chat panel renders Status text. The Text field below carries the explicit "Codex CLI error:" / "is not logged in" / "exited with code" prefix so users see the error wording. Terminal failure also sets bIsError=true on FUnrealMcpAssistantTurnResult in EmitTurnResult.
					ErrEvent.Text = FString::Printf(TEXT("Codex CLI error: %s"), *Event.ErrorMessage);
					EmitEvent(ErrEvent);
				}
				break;
			case EKind::Unknown:
				UE_LOG(LogUnrealMcp, Verbose, TEXT("CodexProvider: ignoring unknown JSONL event: %s"), *Event.RawJson);
				break;
			default:
				break;
			}
		}

		void EmitFriendlyAuthError()
		{
			if (bAuthErrorAlreadyEmitted)
			{
				return;
			}
			bAuthErrorAlreadyEmitted = true;
			FUnrealMcpAssistantEvent ErrEvent;
			ErrEvent.Type = EUnrealMcpAssistantEventType::Status;  // v0.25: EUnrealMcpAssistantEventType has no Error variant; chat panel renders Status text. The Text field below carries the explicit "Codex CLI error:" / "is not logged in" / "exited with code" prefix so users see the error wording. Terminal failure also sets bIsError=true on FUnrealMcpAssistantTurnResult in EmitTurnResult.
			ErrEvent.Text = TEXT("Codex CLI is not logged in. Run 'codex login' in a terminal first, then retry the chat.");
			EmitEvent(ErrEvent);
		}

		bool IsProcessRunning()
		{
			return ProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(ProcessHandle);
		}

		bool GetProcessReturnCode(int32& OutReturnCode)
		{
			return ProcessHandle.IsValid() && FPlatformProcess::GetProcReturnCode(ProcessHandle, &OutReturnCode);
		}

		void TerminateProcessIfRunning()
		{
			if (ProcessHandle.IsValid() && FPlatformProcess::IsProcRunning(ProcessHandle))
			{
				FPlatformProcess::TerminateProc(ProcessHandle, true);
			}
		}

		void CleanupProcessResources()
		{
			if (ProcessHandle.IsValid())
			{
				FPlatformProcess::CloseProc(ProcessHandle);
				ProcessHandle.Reset();
			}
			if (StdoutReadPipe || StdoutWritePipe)
			{
				FPlatformProcess::ClosePipe(StdoutReadPipe, StdoutWritePipe);
				StdoutReadPipe = nullptr;
				StdoutWritePipe = nullptr;
			}
			if (StderrReadPipe || StderrWritePipe)
			{
				FPlatformProcess::ClosePipe(StderrReadPipe, StderrWritePipe);
				StderrReadPipe = nullptr;
				StderrWritePipe = nullptr;
			}
			if (StdinReadPipe || StdinWritePipe)
			{
				FPlatformProcess::ClosePipe(StdinReadPipe, StdinWritePipe);
				StdinReadPipe = nullptr;
				StdinWritePipe = nullptr;
			}
		}

		void EmitStatus(const FString& Text) const
		{
			FUnrealMcpAssistantEvent Event;
			Event.Type = EUnrealMcpAssistantEventType::Status;
			Event.Text = Text;
			EmitEvent(Event);
		}

		void EmitEvent(const FUnrealMcpAssistantEvent& Event) const
		{
			if (!OnEvent)
			{
				return;
			}

			const FUnrealMcpAssistantEvent EventCopy = Event;
			TFunction<void(const FUnrealMcpAssistantEvent&)> EventCallback = OnEvent;
			AsyncTask(ENamedThreads::GameThread, [EventCopy, EventCallback = MoveTemp(EventCallback)]() mutable
			{
				if (EventCallback) { EventCallback(EventCopy); }
			});
		}

		void Finish(const FString& Message, bool bIsError, bool bWasCancelled = false)
		{
			bool bExpectedCompleted = false;
			if (!bCompleted.compare_exchange_strong(bExpectedCompleted, true, std::memory_order_acq_rel))
			{
				return;
			}

			FUnrealMcpAssistantTurnResult Result;
			Result.Text = Message;
			Result.bIsError = bIsError;
			Result.bWasCancelled = bWasCancelled;

			TFunction<void(const FUnrealMcpAssistantTurnResult&)> CompleteCallback = OnComplete;
			TSharedRef<FCodexRun, ESPMode::ThreadSafe> SelfForCleanup = AsShared();
			AsyncTask(ENamedThreads::GameThread, [Result = MoveTemp(Result), CompleteCallback = MoveTemp(CompleteCallback), SelfForCleanup]() mutable
			{
				if (CompleteCallback) { CompleteCallback(Result); }
				if (SelfForCleanup->PumpThread)
				{
					SelfForCleanup->PumpThread->WaitForCompletion();
					delete SelfForCleanup->PumpThread;
					SelfForCleanup->PumpThread = nullptr;
				}
				SelfForCleanup->CleanupProcessResources();
				SelfForCleanup->SelfKeepAlive.Reset();
			});
		}

		FAiProviderConfig Config;
		const UUnrealMcpSettings* Settings = nullptr;
		const FUnrealMcpModule* Module = nullptr;
		FString UserPrompt;
		FString ConversationContext;
		FString PreviousResponseId;
		TFunction<void(const FUnrealMcpAssistantEvent&)> OnEvent;
		TFunction<void(const FUnrealMcpAssistantTurnResult&)> OnComplete;
		FProcHandle ProcessHandle;
		void* StdinReadPipe = nullptr;
		void* StdinWritePipe = nullptr;
		void* StdoutReadPipe = nullptr;
		void* StdoutWritePipe = nullptr;
		void* StderrReadPipe = nullptr;
		void* StderrWritePipe = nullptr;
		FRunnableThread* PumpThread = nullptr;
		FCriticalSection StateMutex;
		TSharedPtr<FCodexRun, ESPMode::ThreadSafe> SelfKeepAlive;
		TArray<FString> PendingSteerInstructions;
		FString StdoutLineBuffer;
		FString StderrLineBuffer;
		FString StderrRecentText;
		FString AccumulatedText;
		bool bAuthErrorAlreadyEmitted = false;
		bool bTurnCompletedSeen = false;
		FString CodexThreadId;
		std::atomic<bool> bCancellationRequested{false};
		std::atomic<bool> bCompleted{false};
	};
}

namespace UnrealMcp
{
namespace Providers
{
	const FString& GetCodexSubprocessPathPrefix()
	{
		// Augment PATH for subprocesses so a bare codex CLI path or its
		// interpreter shims resolve when Unreal Editor inherits a minimal macOS
		// default PATH rather than the user's interactive shell PATH.
		// Common Mac developer-tool dirs covered:
		//   $HOME/.bun/bin      - user-installed JS tooling shims
		//   $HOME/.local/bin    - pip user installs and manual binaries
		//   $HOME/.cargo/bin    - Rust cargo
		//   /opt/homebrew/bin   - Homebrew on Apple Silicon
		//   /usr/local/bin      - Homebrew on Intel Mac
		// Shells silently ignore non-existent entries.
		//
		// Invariants:
		//   (1) Contains NO literal ASCII space and NO literal double-quote.
		//   (2) Uses `PATH=...&&${IFS}<cmd>` (assignment + control-op + IFS-join),
		//       NOT `export PATH=...&&${IFS}<cmd>`. The `export VAR=value` form
		//       word-splits its VALUE post-expansion when the inherited $PATH
		//       contains directories with literal spaces (e.g. `/Library/
		//       Application Support/JetBrains/Toolbox/scripts` on macOS), which
		//       splits the assignment argument and produces:
		//         "export: `Support/...': not a valid identifier".
		//       The bare `PATH=` form treats the assignment as a current-shell
		//       variable update (not a builtin invocation), so word-splitting
		//       does NOT occur on the value. PATH was already exported by the
		//       parent UE Editor process, so child processes inherit the new
		//       value via the standard env-propagation rules.
		//   (3) See Docs/AIProviderArchitecture.md Section B for the
		//       CreateProc tokenizer rationale (UE Unix ParseCmdLineToken treats
		//       `"` as a quote delimiter; literal `"` in argv breaks bash -c).
		// Codex CLI is validated as Mac/Linux only; Windows users use
		// CodexAppServer / Codex Desktop bridge instead.
		static const FString Prefix = TEXT(
			"PATH=$HOME/.bun/bin:$HOME/.local/bin:"
			"$HOME/.cargo/bin:/opt/homebrew/bin:/usr/local/bin:$PATH&&${IFS}");
		return Prefix;
	}
}

	EAiProviderKind FCodexProvider::GetKind() const
	{
		return EAiProviderKind::Codex;
	}

	bool FCodexProvider::ValidateConfig(const FAiProviderConfig& Config, FString& OutError) const
	{
		TArray<FString> FilteredExtraArgs;
		return FCodexRun::ValidateCodexConfig(Config, FilteredExtraArgs, OutError);
	}

	TSharedRef<IUnrealMcpAssistantHandle, ESPMode::ThreadSafe> FCodexProvider::StartTurn(
		const FAiProviderConfig& Config,
		const FUnrealMcpModule* Module,
		const FString& UserPrompt,
		const FString& ConversationContext,
		const FString& PreviousResponseId,
		TFunction<void(const FUnrealMcpAssistantEvent&)> OnEvent,
		TFunction<void(const FUnrealMcpAssistantTurnResult&)> OnComplete)
	{
		const UUnrealMcpSettings* Settings = GetDefault<UUnrealMcpSettings>();
		const TSharedRef<FCodexRun, ESPMode::ThreadSafe> Run = MakeShared<FCodexRun, ESPMode::ThreadSafe>(
			Config,
			*Settings,
			Module,
			UserPrompt,
			ConversationContext,
			PreviousResponseId,
			MoveTemp(OnEvent),
			MoveTemp(OnComplete));
		Run->Start();
		return StaticCastSharedRef<IUnrealMcpAssistantHandle>(Run);
	}

	namespace Providers
	{
		TUniquePtr<IAssistantProvider> CreateCodexProvider()
		{
			return MakeUnique<FCodexProvider>();
		}
	}
}
