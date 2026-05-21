#include "Providers/CodexProvider.h"

#include "Providers/ProviderHelpers.h"
#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include <atomic>

// =====================================================================
// CodexProvider shell-quoting invariants (v0.24.4 - authoritative copy
// of Docs/AIProviderArchitecture.md Section B; keep both in sync)
// =====================================================================
// The full Arguments string passed to FPlatformProcess::CreateProc as the
// Arguments parameter, for the bash subprocess that wraps codex-agent,
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

	FString ComposePrompt(const FString& UserPrompt, const FString& ConversationContext)
	{
		const FString TrimmedContext = ConversationContext.TrimStartAndEnd();
		if (TrimmedContext.IsEmpty())
		{
			return UserPrompt;
		}
		return FString::Printf(
			TEXT("--- conversation context ---\n\n%s\n\n--- latest user prompt ---\n\n%s"),
			*TrimmedContext,
			*UserPrompt);
	}

	FString BuildCodexStartCommand(
		const FString& CodexBinaryPath,
		const FString& ComposedPromptText,
		const FString& ProjectAbsoluteDir,
		const TArray<FString>& FilteredExtraArgs)
	{
		static const TCHAR* const ForcedCodexModel = TEXT("gpt-5.5");
		static const TCHAR* const ForcedCodexReasoning = TEXT("xhigh");
		static const TCHAR* const ForcedCodexSandbox = TEXT("workspace-write");

		TArray<FString> CommandParts;
		CommandParts.Add(QuoteForBashWordNoSpaces(CodexBinaryPath));
		CommandParts.Add(TEXT("start"));
		CommandParts.Add(QuoteForBashWordNoSpaces(ComposedPromptText));
		CommandParts.Add(TEXT("-m"));
		CommandParts.Add(QuoteForBashWordNoSpaces(ForcedCodexModel));
		CommandParts.Add(TEXT("-r"));
		CommandParts.Add(QuoteForBashWordNoSpaces(ForcedCodexReasoning));
		CommandParts.Add(TEXT("-s"));
		CommandParts.Add(QuoteForBashWordNoSpaces(ForcedCodexSandbox));
		CommandParts.Add(TEXT("-d"));
		CommandParts.Add(QuoteForBashWordNoSpaces(ProjectAbsoluteDir));
		CommandParts.Add(TEXT("-w"));
		CommandParts.Add(TEXT("--strip-ansi"));
		if (!FilteredExtraArgs.IsEmpty())
		{
			CommandParts.Add(JoinShellArgumentsNoSpaces(FilteredExtraArgs));
		}

		return UnrealMcp::Providers::GetCodexSubprocessPathPrefix()
			+ FString::Join(CommandParts, TEXT("${IFS}"));
	}

	FString BuildCodexKillCommand(
		const FString& CodexBinaryPath,
		const FString& JobId,
		const FString& ProjectAbsoluteDir)
	{
		static_cast<void>(ProjectAbsoluteDir);

		TArray<FString> KillCommandParts;
		KillCommandParts.Add(QuoteForBashWordNoSpaces(CodexBinaryPath));
		KillCommandParts.Add(TEXT("kill"));
		KillCommandParts.Add(QuoteForBashWordNoSpaces(JobId));

		return UnrealMcp::Providers::GetCodexSubprocessPathPrefix()
			+ FString::Join(KillCommandParts, TEXT("${IFS}"));
	}
}

namespace
{
	const TCHAR* ForcedCodexModel = TEXT("gpt-5.5");
	const TCHAR* ForcedCodexReasoning = TEXT("xhigh");
	const TCHAR* ForcedCodexSandbox = TEXT("workspace-write");
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
				continue;
			}
			if (Character == TEXT('"') && !bInSingleQuote)
			{
				bInDoubleQuote = !bInDoubleQuote;
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
	bool ValidateAndFilterExtraArgs(const FAiProviderConfig& Config, TArray<FString>& OutFilteredArgs, FString& OutError)
	{
		TArray<FString> Tokens;
		if (!TokenizeExtraArgs(Config.CodexExtraArgs.TrimStartAndEnd(), Tokens, OutError))
		{
			OutError = FString::Printf(TEXT("Provider '%s': %s"), *UnrealMcp::Providers::ProviderIdForError(Config), *OutError);
			return false;
		}

		for (int32 Index = 0; Index < Tokens.Num(); ++Index)
		{
			const FString& Token = Tokens[Index];
			auto ConsumeNextValue = [&](const FString& FlagName, FString& OutValue) -> bool
			{
				if (Index + 1 >= Tokens.Num())
				{
					OutError = FString::Printf(TEXT("Provider '%s': CodexExtraArgs flag %s requires a value."), *UnrealMcp::Providers::ProviderIdForError(Config), *FlagName);
					return false;
				}
				OutValue = Tokens[++Index];
				return true;
			};

			if (Token == TEXT("-m") || Token == TEXT("--model"))
			{
				FString Value;
				if (!ConsumeNextValue(Token, Value)) { return false; }
				if (!ValidateFlagValue(Config, Token, Value, ForcedCodexModel, OutError)) { return false; }
				continue;
			}
			if (Token.StartsWith(TEXT("--model=")))
			{
				const FString Value = Token.RightChop(8);
				if (!ValidateFlagValue(Config, TEXT("--model"), Value, ForcedCodexModel, OutError)) { return false; }
				continue;
			}
			if (Token.StartsWith(TEXT("-m")) && Token.Len() > 2)
			{
				FString Value = Token.RightChop(2);
				if (Value.StartsWith(TEXT("="))) { Value.RightChopInline(1, EAllowShrinking::No); }
				if (!ValidateFlagValue(Config, TEXT("-m"), Value, ForcedCodexModel, OutError)) { return false; }
				continue;
			}
			if (Token == TEXT("-r") || Token == TEXT("--reasoning"))
			{
				FString Value;
				if (!ConsumeNextValue(Token, Value)) { return false; }
				if (!ValidateFlagValue(Config, Token, Value, ForcedCodexReasoning, OutError)) { return false; }
				continue;
			}
			if (Token.StartsWith(TEXT("--reasoning=")))
			{
				const FString Value = Token.RightChop(12);
				if (!ValidateFlagValue(Config, TEXT("--reasoning"), Value, ForcedCodexReasoning, OutError)) { return false; }
				continue;
			}
			if (Token.StartsWith(TEXT("-r")) && Token.Len() > 2)
			{
				FString Value = Token.RightChop(2);
				if (Value.StartsWith(TEXT("="))) { Value.RightChopInline(1, EAllowShrinking::No); }
				if (!ValidateFlagValue(Config, TEXT("-r"), Value, ForcedCodexReasoning, OutError)) { return false; }
				continue;
			}
			if (Token == TEXT("--fast"))
			{
				OutError = FString::Printf(TEXT("Provider '%s': CodexExtraArgs must not use --fast because this provider forces model '%s'."), *UnrealMcp::Providers::ProviderIdForError(Config), ForcedCodexModel);
				return false;
			}
			if (Token == TEXT("-s") || Token == TEXT("--sandbox"))
			{
				FString Value;
				if (!ConsumeNextValue(Token, Value)) { return false; }
				if (!ValidateFlagValue(Config, Token, Value, ForcedCodexSandbox, OutError)) { return false; }
				continue;
			}
			if (Token.StartsWith(TEXT("--sandbox=")))
			{
				const FString Value = Token.RightChop(10);
				if (!ValidateFlagValue(Config, TEXT("--sandbox"), Value, ForcedCodexSandbox, OutError)) { return false; }
				continue;
			}
			if (Token.StartsWith(TEXT("-s")) && Token.Len() > 2)
			{
				FString Value = Token.RightChop(2);
				if (Value.StartsWith(TEXT("="))) { Value.RightChopInline(1, EAllowShrinking::No); }
				if (!ValidateFlagValue(Config, TEXT("-s"), Value, ForcedCodexSandbox, OutError)) { return false; }
				continue;
			}

			OutFilteredArgs.Add(Token);
		}
		return true;
	}
	bool LooksLikeJobId(const FString& Candidate)
	{
		const FString Trimmed = Candidate.TrimStartAndEnd();
		if (Trimmed.Len() < 4 || Trimmed.Len() > 96)
		{
			return false;
		}
		for (const TCHAR Character : Trimmed)
		{
			if (!FChar::IsAlnum(Character) && Character != TEXT('-') && Character != TEXT('_'))
			{
				return false;
			}
		}
		return true;
	}
	FString ExtractLikelyJobId(const FString& Chunk)
	{
		TArray<FString> Lines;
		Chunk.ParseIntoArrayLines(Lines, false);
		for (const FString& Line : Lines)
		{
			FString Candidate;
			if (Line.Split(TEXT("Job started:"), nullptr, &Candidate, ESearchCase::IgnoreCase))
			{
				TArray<FString> Parts;
				Candidate.ParseIntoArrayWS(Parts);
				if (Parts.Num() > 0 && LooksLikeJobId(Parts[0]))
				{
					return Parts[0];
					}
				}
				const FString Trimmed = Line.TrimStartAndEnd();
			if (LooksLikeJobId(Trimmed))
			{
				return Trimmed;
			}
		}
		return FString();
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
			// Codex CLI is currently single-shot: previous_response_id is ignored and any compressed chat history is folded into the prompt text.
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

			// TODO(v0.25 Reform B): WritePromptTempFile's output is no longer read by
			// the bash command after the v0.24.4 prompt-pass refactor. The function is
			// retained for one release to preserve any external tooling that may inspect
			// the temp file; Reform B will rewrite SpawnProcess to use `codex exec`
			// (one-shot) and remove this branch.
			if (!WritePromptTempFile(Error))
			{
				Finish(Error, true);
				return;
			}

			if (!SpawnProcess(FilteredExtraArgs, Error))
			{
				Finish(Error, true);
				return;
			}

			EmitStatus(TEXT("Started local Codex agent."));
			PumpThread = FRunnableThread::Create(this, TEXT("UnrealMcpCodexStdoutPump"));
			if (!PumpThread)
			{
				TerminateProcessIfRunning();
				Finish(TEXT("Failed to start Codex stdout pump thread."), true);
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
			TryKillCapturedJob();
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

		virtual uint32 Run() override
		{
			while (!bCancellationRequested.load(std::memory_order_relaxed))
			{
				DrainStdoutOnce();
				if (!IsProcessRunning())
				{
					break;
				}
				FPlatformProcess::Sleep(0.05f);
			}

			for (int32 DrainAttempt = 0; DrainAttempt < 10; ++DrainAttempt)
			{
				if (!DrainStdoutOnce())
				{
					break;
				}
			}

			if (bCompleted.load(std::memory_order_acquire))
			{
				return 0;
			}

			if (bCancellationRequested.load(std::memory_order_relaxed))
			{
				Finish(TEXT("Generation stopped."), false, true);
				return 0;
			}

			int32 ReturnCode = 0;
			const bool bHasReturnCode = GetProcessReturnCode(ReturnCode);
			FString FinalText;
			{
				const FScopeLock Lock(&StateMutex);
				FinalText = AccumulatedText;
			}

			if (bHasReturnCode && ReturnCode != 0)
			{
				if (FinalText.TrimStartAndEnd().IsEmpty())
				{
					FinalText = FString::Printf(TEXT("Codex process exited with code %d."), ReturnCode);
				}
				else
				{
					FinalText += FString::Printf(TEXT("\n\nCodex process exited with code %d."), ReturnCode);
				}
				Finish(FinalText, true);
				return 0;
			}

			if (FinalText.TrimStartAndEnd().IsEmpty())
			{
				FinalText = TEXT("Codex process completed without producing output.");
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
			const FString ProviderId = UnrealMcp::Providers::ProviderIdForError(InConfig);
			const FString BinaryPath = InConfig.CodexBinaryPath.TrimStartAndEnd();
			if (BinaryPath.IsEmpty())
			{
				OutError = FString::Printf(TEXT("Provider '%s': Codex binary path is empty."), *ProviderId);
				return false;
			}

			FString FailureReason;
			if (ContainsDangerousShellCharacters(BinaryPath, FailureReason))
			{
				OutError = FString::Printf(TEXT("Provider '%s': Codex binary path %s"), *ProviderId, *FailureReason);
				return false;
			}

			if (!FPaths::FileExists(BinaryPath))
			{
				OutError = FString::Printf(TEXT("Provider '%s': Codex binary path does not exist: %s"), *ProviderId, *BinaryPath);
				return false;
			}

			if (!ValidateAndFilterExtraArgs(InConfig, OutFilteredExtraArgs, OutError))
			{
				return false;
			}

			return true;
#endif
		}

	private:
		bool WritePromptTempFile(FString& OutError)
		{
			const FString PromptDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp"), TEXT("codex_prompts"));
			if (!IFileManager::Get().MakeDirectory(*PromptDir, true))
			{
				OutError = FString::Printf(TEXT("Failed to create Codex prompt directory: %s"), *PromptDir);
				return false;
			}

			PromptTempFilePath = FPaths::Combine(
				PromptDir,
				FString::Printf(TEXT("%s.txt"), *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower)));

			const FString PromptText = ::UnrealMcp::Providers::Internal::ComposePrompt(UserPrompt, ConversationContext);
			if (!FFileHelper::SaveStringToFile(PromptText, *PromptTempFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutError = FString::Printf(TEXT("Failed to write Codex prompt file: %s"), *PromptTempFilePath);
				return false;
			}

			return true;
		}

		bool SpawnProcess(const TArray<FString>& FilteredExtraArgs, FString& OutError)
		{
			const FString BashPath = TEXT("/bin/bash");
			if (!FPaths::FileExists(BashPath))
			{
				OutError = TEXT("Codex provider requires /bin/bash for the prompt-file fallback command.");
				return false;
			}

			if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
			{
				OutError = TEXT("Failed to create pipe for Codex stdout.");
				return false;
			}

			const FString Command = ::UnrealMcp::Providers::Internal::BuildCodexStartCommand(
				Config.CodexBinaryPath.TrimStartAndEnd(),
				::UnrealMcp::Providers::Internal::ComposePrompt(UserPrompt, ConversationContext),
				FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()),
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
				*FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()),
				WritePipe,
				nullptr,
				WritePipe);

			if (!ProcessHandle.IsValid())
			{
				CleanupProcessResources();
				OutError = TEXT("Failed to launch codex-agent subprocess.");
				return false;
			}

			return true;
		}

		bool DrainStdoutOnce()
		{
			if (!ReadPipe)
			{
				return false;
			}

			const FString Chunk = FPlatformProcess::ReadPipe(ReadPipe);
			if (Chunk.IsEmpty())
			{
				return false;
			}

			{
				const FScopeLock Lock(&StateMutex);
				AccumulatedText += Chunk;
				if (CapturedJobId.IsEmpty())
				{
					CapturedJobId = ExtractLikelyJobId(Chunk);
				}
			}

			FUnrealMcpAssistantEvent Event;
			Event.Type = EUnrealMcpAssistantEventType::TextDelta;
			Event.Text = Chunk;
			EmitEvent(Event);
			return true;
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

		void TryKillCapturedJob()
		{
			FString JobId;
			{
				const FScopeLock Lock(&StateMutex);
				JobId = CapturedJobId;
			}
			if (JobId.IsEmpty())
			{
				return;
			}

			const FString KillCommand = ::UnrealMcp::Providers::Internal::BuildCodexKillCommand(
				Config.CodexBinaryPath.TrimStartAndEnd(),
				JobId,
				FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
			const FString KillArguments = FString::Printf(TEXT("-c %s"), *KillCommand);

			FProcHandle KillHandle = FPlatformProcess::CreateProc(
				TEXT("/bin/bash"),
				*KillArguments,
				true,
				true,
				true,
				nullptr,
				0,
				*FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()),
				nullptr,
				nullptr);
			if (KillHandle.IsValid())
			{
				FPlatformProcess::CloseProc(KillHandle);
			}
		}

		void CleanupProcessResources()
		{
			if (ProcessHandle.IsValid())
			{
				FPlatformProcess::CloseProc(ProcessHandle);
				ProcessHandle.Reset();
			}
			if (ReadPipe || WritePipe)
			{
				FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
				ReadPipe = nullptr;
				WritePipe = nullptr;
			}
			if (!PromptTempFilePath.IsEmpty())
			{
				IFileManager::Get().Delete(*PromptTempFilePath, false, true, true);
				PromptTempFilePath.Reset();
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
		void* ReadPipe = nullptr;
		void* WritePipe = nullptr;
		FRunnableThread* PumpThread = nullptr;
		FCriticalSection StateMutex;
		TSharedPtr<FCodexRun, ESPMode::ThreadSafe> SelfKeepAlive;
		TArray<FString> PendingSteerInstructions;
		FString PromptTempFilePath;
		FString CapturedJobId;
		FString AccumulatedText;
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
		// Augment PATH for subprocesses so codex-agent script interpreter calls
		// such as bare bun/node resolve when Unreal Editor inherits a minimal
		// macOS default PATH rather than the user's interactive shell PATH.
		// Common Mac developer-tool dirs covered:
		//   $HOME/.bun/bin      - Bun (Codex Orchestrator dependency)
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
