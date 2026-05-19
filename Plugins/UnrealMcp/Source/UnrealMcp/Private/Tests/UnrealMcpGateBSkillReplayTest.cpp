#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UnrealMcpActivityLog.h"
#include "UnrealMcpSkillTools.h"

namespace UnrealMcpGateBSkillReplayTest
{
	FString GateBGetSavedPath(const FString& RelativePath)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), RelativePath));
	}

	FString GateBMakeActivityLogPath(const FString& SessionId)
	{
		return GateBGetSavedPath(FString::Printf(TEXT("UnrealMcp/ActivityLog/%s.jsonl"), *SessionId));
	}

	FString GateBMakeSkillDraftRoot(const FString& SkillName)
	{
		return GateBGetSavedPath(FString::Printf(TEXT("UnrealMcp/SkillDrafts/%s"), *SkillName));
	}

	FString GateBGetStructuredString(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		FString Value;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetStringField(FieldName, Value);
		}
		return Value;
	}

	bool GateBWriteToolEvent(const FString& SessionId, const FString& ToolName, FString& OutFailureReason)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("toolName"), ToolName);
		Payload->SetBoolField(TEXT("isError"), false);

		UnrealMcp::FActivityLogEvent Event;
		Event.EventKind = TEXT("tool_call");
		Event.Summary = FString::Printf(TEXT("Called %s."), *ToolName);
		Event.Payload = Payload;
		Event.TaskLabel = TEXT("Gate B read-only triad skill replay");
		return UnrealMcp::TryWriteActivityEventForSession(SessionId, Event, OutFailureReason);
	}

	bool GateBContainsToolsInOrder(const FString& Text, const TArray<FString>& ToolNames)
	{
		int32 SearchStart = 0;
		for (const FString& ToolName : ToolNames)
		{
			const int32 FoundIndex = Text.Find(ToolName, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchStart);
			if (FoundIndex == INDEX_NONE)
			{
				return false;
			}
			SearchStart = FoundIndex + ToolName.Len();
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpGateBSkillReplayTest,
	"UnrealMcp.Verification.GateB.SkillReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpGateBSkillReplayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8).ToLower();
	const FString SessionId = FString::Printf(TEXT("gate-b-triad-%s"), *UniqueSuffix);
	const FString SkillName = FString::Printf(TEXT("gate-b-triad-skill-replay-%s"), *UniqueSuffix);
	const FString ActivityLogPath = UnrealMcpGateBSkillReplayTest::GateBMakeActivityLogPath(SessionId);
	const FString SkillDraftRoot = UnrealMcpGateBSkillReplayTest::GateBMakeSkillDraftRoot(SkillName);

	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*ActivityLogPath, false, true, true);
		IFileManager::Get().DeleteDirectory(*SkillDraftRoot, false, true);
	};

	const TArray<FString> ToolNames = {
		TEXT("unreal.editor.engine_version"),
		TEXT("unreal.project_settings_get"),
		TEXT("unreal.list_assets")
	};

	for (const FString& ToolName : ToolNames)
	{
		FString FailureReason;
		TestTrue(
			FString::Printf(TEXT("Synthetic activity event written for %s."), *ToolName),
			UnrealMcpGateBSkillReplayTest::GateBWriteToolEvent(SessionId, ToolName, FailureReason));
		if (!FailureReason.IsEmpty())
		{
			AddError(FailureReason);
		}
	}

	FJsonObject DistillArgs;
	DistillArgs.SetStringField(TEXT("sessionId"), SessionId);
	DistillArgs.SetStringField(TEXT("skillName"), SkillName);
	DistillArgs.SetStringField(TEXT("title"), TEXT("Gate B Read-Only Triad Skill Replay"));
	DistillArgs.SetStringField(TEXT("goal"), TEXT("Replay the Gate B read-only triad."));
	DistillArgs.SetBoolField(TEXT("writeDraft"), true);
	DistillArgs.SetBoolField(TEXT("includeEvents"), true);
	DistillArgs.SetBoolField(TEXT("overwrite"), true);
	DistillArgs.SetNumberField(TEXT("maxEvents"), 20.0);

	const FUnrealMcpExecutionResult DistillResult = UnrealMcp::SkillDistillFromActivity(DistillArgs);
	TestFalse(TEXT("SkillDistillFromActivity succeeds."), DistillResult.bIsError);
	const FString DraftPath = UnrealMcpGateBSkillReplayTest::GateBGetStructuredString(DistillResult, TEXT("draftPath"));
	TestTrue(TEXT("Distill writes a draft path."), !DraftPath.IsEmpty() && FPaths::FileExists(DraftPath));

	FJsonObject ApplyArgs;
	ApplyArgs.SetStringField(TEXT("skillPath"), DraftPath);
	ApplyArgs.SetStringField(TEXT("task"), TEXT("Gate B replay verification"));
	ApplyArgs.SetBoolField(TEXT("writeMemory"), false);
	ApplyArgs.SetBoolField(TEXT("includeFullText"), true);

	const FUnrealMcpExecutionResult ApplyResult = UnrealMcp::SkillApply(ApplyArgs);
	TestFalse(TEXT("SkillApply succeeds without network or HTTP loopback."), ApplyResult.bIsError);
	const FString SkillText = UnrealMcpGateBSkillReplayTest::GateBGetStructuredString(ApplyResult, TEXT("skillText"));
	TestTrue(
		TEXT("Applied skill text lists the triad tools in observed order."),
		UnrealMcpGateBSkillReplayTest::GateBContainsToolsInOrder(SkillText, ToolNames));

	return true;
}

#endif
