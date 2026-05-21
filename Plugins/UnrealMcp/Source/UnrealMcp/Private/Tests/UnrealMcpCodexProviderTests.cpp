#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Providers/CodexProviderExecHelpers.h"

#if PLATFORM_MAC || PLATFORM_LINUX
#include <sys/stat.h>
#endif

namespace UnrealMcp::Providers
{
	const FString& GetCodexSubprocessPathPrefix();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCodexProviderSubprocessPathPrefixTest,
	"UnrealMcp.CodexProvider.SubprocessPathPrefix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCodexProviderSubprocessPathPrefixTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString& Prefix = UnrealMcp::Providers::GetCodexSubprocessPathPrefix();
	TestFalse(TEXT("Prefix is not empty."), Prefix.IsEmpty());
	TestTrue(TEXT("Prefix contains $HOME/.bun/bin."), Prefix.Contains(TEXT("$HOME/.bun/bin")));
	TestTrue(TEXT("Prefix contains $HOME/.local/bin."), Prefix.Contains(TEXT("$HOME/.local/bin")));
	TestTrue(TEXT("Prefix contains $HOME/.cargo/bin."), Prefix.Contains(TEXT("$HOME/.cargo/bin")));
	TestTrue(TEXT("Prefix contains /opt/homebrew/bin."), Prefix.Contains(TEXT("/opt/homebrew/bin")));
	TestTrue(TEXT("Prefix contains /usr/local/bin."), Prefix.Contains(TEXT("/usr/local/bin")));
	TestTrue(TEXT("Prefix ends with :$PATH&&${IFS}."), Prefix.EndsWith(TEXT(":$PATH&&${IFS}")));
	TestFalse(TEXT("Prefix MUST contain zero literal double-quote chars."), Prefix.Contains(TEXT("\"")));
	TestTrue(TEXT("Prefix uses bare PATH= assignment."), Prefix.StartsWith(TEXT("PATH=")));
	TestFalse(TEXT("Prefix MUST NOT use 'export PATH=' form."), Prefix.Contains(TEXT("export")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCodexProviderExecCommandBuildTest,
	"UnrealMcp.CodexProvider.ExecCommandBuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCodexProviderExecCommandBuildTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString FakeBinary = TEXT("/tmp/fake-codex");
	const FString UserPrompt = TEXT("hello world test");
	const FString ProjDir = TEXT("/tmp/test-proj");
	TArray<FString> ExtraArgs = { TEXT("-c"), TEXT("custom_key=\"value\"") };

	const FString Cmd = UnrealMcp::Providers::Internal::BuildCodexExecCommand(
		FakeBinary, UserPrompt, ProjDir, ExtraArgs);

	int32 HelperSpaceCount = 0;
	for (const TCHAR Ch : Cmd) { if (Ch == TEXT(' ')) { ++HelperSpaceCount; } }
	TestEqual(TEXT("BuildCodexExecCommand output has 0 literal ASCII spaces."), HelperSpaceCount, 0);

	const FString FinalArgs = FString::Printf(TEXT("-c %s"), *Cmd);
	int32 FinalSpaceCount = 0;
	for (const TCHAR Ch : FinalArgs) { if (Ch == TEXT(' ')) { ++FinalSpaceCount; } }
	TestEqual(TEXT("Final Arguments string has exactly 1 literal ASCII space."), FinalSpaceCount, 1);

	TestFalse(TEXT("Cmd has 0 literal \" chars."), Cmd.Contains(TEXT("\"")));
	TestFalse(TEXT("FinalArgs has 0 literal \" chars."), FinalArgs.Contains(TEXT("\"")));

	TestTrue(TEXT("Cmd contains fake binary."), Cmd.Contains(TEXT("/tmp/fake-codex")));
	TestTrue(TEXT("Cmd contains exec subcommand."), Cmd.Contains(TEXT("${IFS}exec${IFS}")) || Cmd.Contains(TEXT("${IFS}exec$")));
	TestTrue(TEXT("Cmd contains --json."), Cmd.Contains(TEXT("--json")));
	TestTrue(TEXT("Cmd contains --ephemeral."), Cmd.Contains(TEXT("--ephemeral")));
	TestTrue(TEXT("Cmd contains --skip-git-repo-check."), Cmd.Contains(TEXT("--skip-git-repo-check")));
	TestTrue(TEXT("Cmd contains -C <projdir> $'/tmp/test-proj'."), Cmd.Contains(TEXT("$'/tmp/test-proj'")));
	TestTrue(TEXT("Cmd contains baseline model flag."), Cmd.Contains(TEXT("model=\\x22gpt-5.5\\x22")) || Cmd.Contains(TEXT("model='gpt-5.5'")));
	TestTrue(TEXT("Cmd contains baseline reasoning_effort flag."), Cmd.Contains(TEXT("reasoning_effort=")));
	TestTrue(TEXT("Cmd contains baseline sandbox_mode flag."), Cmd.Contains(TEXT("sandbox_mode=")));

	const int32 PromptIdx = Cmd.Find(TEXT("$'hello\\x20world\\x20test'"));
	const int32 BaselineIdx = Cmd.Find(TEXT("model="));
	TestTrue(TEXT("User prompt index > baseline flag index (prompt comes after options)."), PromptIdx > BaselineIdx);
	TestFalse(TEXT("Cmd does NOT contain any context substring."), Cmd.Contains(TEXT("conversation")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCodexProviderJsonlEventParserTest,
	"UnrealMcp.CodexProvider.JsonlEventParser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCodexProviderJsonlEventParserTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcp::Providers::Internal;
	using EKind = ECodexJsonlEventKind;
	FCodexJsonlEvent Event;

	TestTrue(TEXT("thread.started parses"), ParseCodexJsonlEvent(TEXT("{\"type\":\"thread.started\",\"thread_id\":\"abc-123\"}"), Event));
	TestEqual(TEXT("thread.started kind"), Event.Kind, EKind::ThreadStarted);
	TestEqual(TEXT("thread_id"), Event.ThreadId, FString(TEXT("abc-123")));

	TestTrue(TEXT("turn.started parses"), ParseCodexJsonlEvent(TEXT("{\"type\":\"turn.started\"}"), Event));
	TestEqual(TEXT("turn.started kind"), Event.Kind, EKind::TurnStarted);

	TestTrue(TEXT("item.completed agent_message parses"),
		ParseCodexJsonlEvent(TEXT("{\"type\":\"item.completed\",\"item\":{\"id\":\"item_0\",\"type\":\"agent_message\",\"text\":\"hello world\"}}"), Event));
	TestEqual(TEXT("item kind"), Event.Kind, EKind::ItemCompleted);
	TestEqual(TEXT("item type"), Event.ItemType, FString(TEXT("agent_message")));
	TestEqual(TEXT("item text"), Event.ItemText, FString(TEXT("hello world")));

	TestTrue(TEXT("turn.completed parses"),
		ParseCodexJsonlEvent(TEXT("{\"type\":\"turn.completed\",\"usage\":{\"input_tokens\":100,\"output_tokens\":20,\"cached_input_tokens\":5,\"reasoning_output_tokens\":15}}"), Event));
	TestEqual(TEXT("turn kind"), Event.Kind, EKind::TurnCompleted);
	TestEqual(TEXT("usage input"), Event.InputTokens, 100);
	TestEqual(TEXT("usage output"), Event.OutputTokens, 20);

	TestTrue(TEXT("error parses"),
		ParseCodexJsonlEvent(TEXT("{\"type\":\"error\",\"message\":\"401 Unauthorized: Missing bearer or basic authentication\"}"), Event));
	TestEqual(TEXT("error kind"), Event.Kind, EKind::Error);
	TestTrue(TEXT("error message contains 401"), Event.ErrorMessage.Contains(TEXT("401")));

	TestTrue(TEXT("unknown event parses but kind=Unknown"),
		ParseCodexJsonlEvent(TEXT("{\"type\":\"future_event\",\"payload\":{}}"), Event));
	TestEqual(TEXT("unknown kind"), Event.Kind, EKind::Unknown);
	TestFalse(TEXT("raw json populated"), Event.RawJson.IsEmpty());

	TestFalse(TEXT("malformed JSON rejected"),
		ParseCodexJsonlEvent(TEXT("{\"type\": \"broken"), Event));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCodexProviderAuthDetectionTest,
	"UnrealMcp.CodexProvider.AuthDetection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCodexProviderAuthDetectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcp::Providers::Internal;

	TestTrue(TEXT("401 Unauthorized detected"), DetectCodexAuthError(TEXT("401 Unauthorized: Missing bearer or basic authentication in header")));
	TestTrue(TEXT("Reconnecting detected"), DetectCodexAuthError(TEXT("Reconnecting due to auth failure")));
	TestTrue(TEXT("HTTP error 401 detected"), DetectCodexAuthError(TEXT("HTTP error: 401 Unauthorized")));
	TestTrue(TEXT("Missing bearer detected"), DetectCodexAuthError(TEXT("Missing bearer or basic authentication")));
	TestFalse(TEXT("Generic server error NOT detected"), DetectCodexAuthError(TEXT("general server error")));
	TestFalse(TEXT("Empty string NOT detected"), DetectCodexAuthError(TEXT("")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCodexProviderMigrationPredicatesTest,
	"UnrealMcp.CodexProvider.MigrationPredicates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCodexProviderMigrationPredicatesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcp::Providers::Internal;

	TestTrue(TEXT("legacy codex-agent path detected"), LooksLikeLegacyCodexAgentPath(TEXT("/Users/u/codex-orchestrator/bin/codex-agent")));
	TestTrue(TEXT("legacy codex-agent with trailing slash detected"), LooksLikeLegacyCodexAgentPath(TEXT("/Users/u/codex-orchestrator/bin/codex-agent/")));
	TestTrue(TEXT("legacy case-insensitive"), LooksLikeLegacyCodexAgentPath(TEXT("/Users/u/Codex-Agent")));
	TestFalse(TEXT("codex binary NOT legacy"), LooksLikeLegacyCodexAgentPath(TEXT("/opt/homebrew/bin/codex")));
	TestFalse(TEXT("empty string NOT legacy"), LooksLikeLegacyCodexAgentPath(TEXT("")));
	TestFalse(TEXT("file named 'codexagent' NOT detected (needs hyphen)"), LooksLikeLegacyCodexAgentPath(TEXT("/usr/local/bin/codexagent")));

	TestTrue(TEXT("-r token rejected"), ContainsUnsupportedReasoningFlag({TEXT("-r"), TEXT("high")}));
	TestTrue(TEXT("-rhigh concatenated rejected"), ContainsUnsupportedReasoningFlag({TEXT("-rhigh")}));
	TestTrue(TEXT("-r=high rejected"), ContainsUnsupportedReasoningFlag({TEXT("-r=high")}));
	TestTrue(TEXT("--reasoning rejected"), ContainsUnsupportedReasoningFlag({TEXT("--reasoning"), TEXT("high")}));
	TestTrue(TEXT("--reasoning=high rejected"), ContainsUnsupportedReasoningFlag({TEXT("--reasoning=high")}));
	TestTrue(TEXT("v0.24 default rejected"), ContainsUnsupportedReasoningFlag({TEXT("-m"), TEXT("gpt-5.5"), TEXT("-r"), TEXT("xhigh")}));
	TestFalse(TEXT("lone -m accepted"), ContainsUnsupportedReasoningFlag({TEXT("-m"), TEXT("gpt-5.5")}));
	TestFalse(TEXT("-c reasoning_effort accepted"), ContainsUnsupportedReasoningFlag({TEXT("-c"), TEXT("reasoning_effort=\"high\"")}));
	TestFalse(TEXT("empty list accepted"), ContainsUnsupportedReasoningFlag({}));

	return true;
}

namespace UnrealMcpCodexProviderTestsHelpers
{
	bool WriteFakeCodexScript(FString& OutScriptPath, FString& OutArgvFile, FString& OutStdinFile, FString& OutError)
	{
		const FString TempDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("UnrealMcpTests"));
		IFileManager::Get().MakeDirectory(*TempDir, true);

		const FString Guid = FGuid::NewGuid().ToString(EGuidFormats::DigitsLower);
		OutScriptPath = FPaths::Combine(TempDir, FString::Printf(TEXT("fake-codex-%s.sh"), *Guid));
		OutArgvFile = FPaths::Combine(TempDir, FString::Printf(TEXT("fake-codex-argv-%s.txt"), *Guid));
		OutStdinFile = FPaths::Combine(TempDir, FString::Printf(TEXT("fake-codex-stdin-%s.txt"), *Guid));

		const FString ScriptBody = FString::Printf(TEXT(
			"#!/bin/sh\n"
			"ARGV_FILE='%s'\n"
			"STDIN_FILE='%s'\n"
			": > \"$ARGV_FILE\"\n"
			"for arg do\n"
			"    printf '%%s\\n' \"$arg\" >> \"$ARGV_FILE\"\n"
			"done\n"
			"cat > \"$STDIN_FILE\"\n"
			"printf '{\"type\":\"thread.started\",\"thread_id\":\"test-thread-id\"}\\n'\n"
			"sleep 0.05\n"
			"printf '{\"type\":\"turn.started\"}\\n'\n"
			"sleep 0.05\n"
			"printf '{\"type\":\"item.completed\",\"item\":{\"id\":\"item_0\",\"type\":\"agent'\n"
			"sleep 0.05\n"
			"printf '_message\",\"text\":\"hello from fake codex\"}}\\n'\n"
			"sleep 0.05\n"
			"printf '{\"type\":\"turn.completed\",\"usage\":{\"input_tokens\":42,\"output_tokens\":8,\"cached_input_tokens\":0,\"reasoning_output_tokens\":3}}\\n'\n"
			"exit 0\n"),
			*OutArgvFile, *OutStdinFile);

		if (!FFileHelper::SaveStringToFile(ScriptBody, *OutScriptPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Failed to write fake codex script: %s"), *OutScriptPath);
			return false;
		}

#if PLATFORM_MAC || PLATFORM_LINUX
		(void)::chmod(TCHAR_TO_UTF8(*OutScriptPath), 0755);
#endif
		return true;
	}

	bool RunBashCMinusCWithStdin(
		const FString& Arguments,
		const FString& ConversationContext,
		FString& OutStdout,
		FString& OutStderr,
		int32& OutExitCode,
		FString& OutError)
	{
		void* StdoutRead = nullptr;
		void* StdoutWrite = nullptr;
		void* StderrRead = nullptr;
		void* StderrWrite = nullptr;
		void* StdinRead = nullptr;
		void* StdinWrite = nullptr;

		if (!FPlatformProcess::CreatePipe(StdoutRead, StdoutWrite) ||
			!FPlatformProcess::CreatePipe(StderrRead, StderrWrite) ||
			!FPlatformProcess::CreatePipe(StdinRead, StdinWrite, true))
		{
			OutError = TEXT("Failed to create one of the 3 pipes.");
			return false;
		}

		const FString BashPath = TEXT("/bin/bash");
		FProcHandle Proc = FPlatformProcess::CreateProc(
			*BashPath, *Arguments,
			false, true, true,
			nullptr, 0,
			*FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()),
			StdoutWrite, StdinRead, StderrWrite);

		if (!Proc.IsValid())
		{
			FPlatformProcess::ClosePipe(StdoutRead, StdoutWrite);
			FPlatformProcess::ClosePipe(StderrRead, StderrWrite);
			FPlatformProcess::ClosePipe(StdinRead, StdinWrite);
			OutError = TEXT("CreateProc failed.");
			return false;
		}

		if (!ConversationContext.IsEmpty())
		{
			const FTCHARToUTF8 Utf8(*ConversationContext);
			int32 Written = 0;
			FPlatformProcess::WritePipe(StdinWrite, reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length(), &Written);
		}
		FPlatformProcess::ClosePipe(nullptr, StdinWrite);
		StdinWrite = nullptr;

		const double StartTime = FPlatformTime::Seconds();
		const double TimeoutSec = 10.0;
		while (FPlatformProcess::IsProcRunning(Proc))
		{
			OutStdout += FPlatformProcess::ReadPipe(StdoutRead);
			OutStderr += FPlatformProcess::ReadPipe(StderrRead);
			if (FPlatformTime::Seconds() - StartTime > TimeoutSec)
			{
				FPlatformProcess::TerminateProc(Proc, true);
				FPlatformProcess::ClosePipe(StdoutRead, StdoutWrite);
				FPlatformProcess::ClosePipe(StderrRead, StderrWrite);
				FPlatformProcess::ClosePipe(StdinRead, nullptr);
				FPlatformProcess::CloseProc(Proc);
				OutError = TEXT("Subprocess timeout.");
				return false;
			}
			FPlatformProcess::Sleep(0.01f);
		}
		OutStdout += FPlatformProcess::ReadPipe(StdoutRead);
		OutStderr += FPlatformProcess::ReadPipe(StderrRead);

		int32 Ret = -1;
		FPlatformProcess::GetProcReturnCode(Proc, &Ret);
		OutExitCode = Ret;
		FPlatformProcess::ClosePipe(StdoutRead, StdoutWrite);
		FPlatformProcess::ClosePipe(StderrRead, StderrWrite);
		FPlatformProcess::ClosePipe(StdinRead, nullptr);
		FPlatformProcess::CloseProc(Proc);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCodexProviderExecCommandActualBashExecTest,
	"UnrealMcp.CodexProvider.ExecCommandActualBashExec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCodexProviderExecCommandActualBashExecTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcpCodexProviderTestsHelpers;
	if (!FPaths::FileExists(TEXT("/bin/bash"))) { AddInfo(TEXT("/bin/bash not present; skipping.")); return true; }

	FString ScriptPath, ArgvFile, StdinFile, WriteErr;
	if (!WriteFakeCodexScript(ScriptPath, ArgvFile, StdinFile, WriteErr)) { AddError(WriteErr); return false; }
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*ScriptPath, false, true, true);
		IFileManager::Get().Delete(*ArgvFile, false, true, true);
		IFileManager::Get().Delete(*StdinFile, false, true, true);
	};

	const FString UserPrompt = TEXT("test prompt with spaces");
	const FString Context = TEXT("line1\nline2 with spaces\nline3");
	const FString ProjDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const TArray<FString> NoExtras;

	const FString Cmd = UnrealMcp::Providers::Internal::BuildCodexExecCommand(ScriptPath, UserPrompt, ProjDir, NoExtras);
	const FString Args = FString::Printf(TEXT("-c %s"), *Cmd);

	FString Stdout, Stderr;
	int32 ExitCode = -1;
	FString RunErr;
	if (!RunBashCMinusCWithStdin(Args, Context, Stdout, Stderr, ExitCode, RunErr))
	{
		AddError(FString::Printf(TEXT("bash -c failed: %s. Stderr: %s"), *RunErr, *Stderr));
		return false;
	}
	TestEqual(TEXT("bash -c exit code 0"), ExitCode, 0);

	FString ArgvContent;
	FFileHelper::LoadFileToString(ArgvContent, *ArgvFile);
	TestTrue(TEXT("argv has 'exec'"), ArgvContent.Contains(TEXT("\nexec\n")) || ArgvContent.StartsWith(TEXT("exec\n")));
	TestTrue(TEXT("argv has --json"), ArgvContent.Contains(TEXT("\n--json\n")));
	TestTrue(TEXT("argv has --ephemeral"), ArgvContent.Contains(TEXT("\n--ephemeral\n")));
	TestTrue(TEXT("argv has -C"), ArgvContent.Contains(TEXT("\n-C\n")));
	TestTrue(TEXT("argv has -c"), ArgvContent.Contains(TEXT("\n-c\n")));
	TestTrue(TEXT("argv has user prompt at end"), ArgvContent.TrimEnd().EndsWith(UserPrompt));

	FString StdinContent;
	FFileHelper::LoadFileToString(StdinContent, *StdinFile);
	TestEqual(TEXT("stdin file matches ConversationContext"), StdinContent, Context);

	TestTrue(TEXT("stdout contains thread.started"), Stdout.Contains(TEXT("thread.started")));
	TestTrue(TEXT("stdout contains turn.started"), Stdout.Contains(TEXT("turn.started")));
	TestTrue(TEXT("stdout contains complete agent_message event"), Stdout.Contains(TEXT("\"text\":\"hello from fake codex\"")));
	TestTrue(TEXT("stdout contains turn.completed"), Stdout.Contains(TEXT("turn.completed")));

	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
