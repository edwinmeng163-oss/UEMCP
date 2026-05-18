#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpTaskAtlasTools.h"

namespace
{
	UnrealMcp::FTaskAtlasEventRecord MakeTaskAtlasEvent(
		const FString& SessionId,
		const FDateTime& Timestamp,
		const FString& EventKind,
		const FString& ToolName = FString(),
		bool bCompletionMarker = false)
	{
		UnrealMcp::FTaskAtlasEventRecord Event;
		Event.SessionId = SessionId;
		Event.Timestamp = Timestamp;
		Event.TimestampUtc = Timestamp.ToIso8601();
		Event.EventKind = EventKind;
		Event.ToolName = ToolName;
		Event.Content = EventKind == TEXT("user_intent") ? TEXT("Build a test workflow. Done.") : TEXT("Completed.");
		Event.bCompletionMarker = bCompletionMarker;
		return Event;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasClusteringTest,
	"UnrealMcp.TaskAtlas.Clustering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasClusteringTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FDateTime Base(2026, 5, 18, 12, 0, 0);

	{
		TArray<UnrealMcp::FTaskAtlasEventRecord> Events;
		Events.Add(MakeTaskAtlasEvent(TEXT("session-a"), Base, TEXT("user_intent")));
		Events.Add(MakeTaskAtlasEvent(TEXT("session-a"), Base + FTimespan::FromMinutes(5), TEXT("tool_call"), TEXT("unreal.editor_status")));
		const TArray<UnrealMcp::FTaskAtlasTaskRecord> Tasks = UnrealMcp::ClusterTaskAtlasEventsForTests(Events);
		TestEqual(TEXT("Same-session events below threshold stay in one task."), Tasks.Num(), 1);
		TestEqual(TEXT("Critical path records observed tool."), Tasks[0].CriticalPath[0], TEXT("unreal.editor_status"));
	}

	{
		TArray<UnrealMcp::FTaskAtlasEventRecord> Events;
		Events.Add(MakeTaskAtlasEvent(TEXT("session-b"), Base, TEXT("user_intent")));
		Events.Add(MakeTaskAtlasEvent(TEXT("session-b"), Base + FTimespan::FromMinutes(1), TEXT("ai_summary"), FString(), true));
		Events.Add(MakeTaskAtlasEvent(TEXT("session-b"), Base + FTimespan::FromMinutes(12), TEXT("user_intent")));
		const TArray<UnrealMcp::FTaskAtlasTaskRecord> Tasks = UnrealMcp::ClusterTaskAtlasEventsForTests(Events);
		TestEqual(TEXT("Gap above threshold starts a new task after completion marker."), Tasks.Num(), 2);
	}

	{
		TArray<UnrealMcp::FTaskAtlasEventRecord> Events;
		Events.Add(MakeTaskAtlasEvent(TEXT("session-c"), Base, TEXT("user_intent")));
		Events.Add(MakeTaskAtlasEvent(TEXT("session-c"), Base + FTimespan::FromMinutes(12), TEXT("tool_call"), TEXT("unreal.tail_log")));
		const TArray<UnrealMcp::FTaskAtlasTaskRecord> Tasks = UnrealMcp::ClusterTaskAtlasEventsForTests(Events);
		TestEqual(TEXT("Gap above threshold stays in task until completion marker is seen."), Tasks.Num(), 1);
	}

	{
		TArray<UnrealMcp::FTaskAtlasEventRecord> Events;
		Events.Add(MakeTaskAtlasEvent(TEXT("session-d1"), Base, TEXT("user_intent")));
		Events.Add(MakeTaskAtlasEvent(TEXT("session-d2"), Base + FTimespan::FromMinutes(1), TEXT("user_intent")));
		const TArray<UnrealMcp::FTaskAtlasTaskRecord> Tasks = UnrealMcp::ClusterTaskAtlasEventsForTests(Events);
		TestEqual(TEXT("Different sessions never merge."), Tasks.Num(), 2);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasValidationTest,
	"UnrealMcp.TaskAtlas.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	{
		FJsonObject Arguments;
		Arguments.SetStringField(TEXT("kind"), TEXT("bad_kind"));
		Arguments.SetStringField(TEXT("content"), TEXT("Intent"));
		FUnrealMcpExecutionResult Result;
		TestTrue(TEXT("activity_log_annotate rejects invalid kind."), UnrealMcp::TryExecuteTaskAtlasTool(TEXT("unreal.activity_log_annotate"), Arguments, Result));
		TestTrue(TEXT("activity_log_annotate invalid kind is an error."), Result.bIsError);
		TestTrue(TEXT("activity_log_annotate reports InvalidEnum."), Result.Text.Contains(TEXT("user_intent")));
	}

	{
		FJsonObject Arguments;
		Arguments.SetNumberField(TEXT("filter"), 12.0);
		FUnrealMcpExecutionResult Result;
		TestTrue(TEXT("task_list dispatches."), UnrealMcp::TryExecuteTaskAtlasTool(TEXT("unreal.task_list"), Arguments, Result));
		TestTrue(TEXT("task_list rejects wrong filter type."), Result.bIsError);
		TestTrue(TEXT("task_list reports type mismatch."), Result.Text.Contains(TEXT("string")));
	}

	{
		FJsonObject Arguments;
		FUnrealMcpExecutionResult Result;
		TestTrue(TEXT("task_describe dispatches."), UnrealMcp::TryExecuteTaskAtlasTool(TEXT("unreal.task_describe"), Arguments, Result));
		TestTrue(TEXT("task_describe rejects missing taskId."), Result.bIsError);
		TestTrue(TEXT("task_describe reports missing field."), Result.Text.Contains(TEXT("taskId")));
	}

	{
		FJsonObject Arguments;
		Arguments.SetStringField(TEXT("taskId"), TEXT("session-20260518T120000Z"));
		Arguments.SetStringField(TEXT("rating"), TEXT("maybe"));
		FUnrealMcpExecutionResult Result;
		TestTrue(TEXT("task_rate dispatches."), UnrealMcp::TryExecuteTaskAtlasTool(TEXT("unreal.task_rate"), Arguments, Result));
		TestTrue(TEXT("task_rate rejects invalid rating."), Result.bIsError);
		TestTrue(TEXT("task_rate reports enum."), Result.Text.Contains(TEXT("success")));
	}

	{
		FJsonObject Arguments;
		Arguments.SetStringField(TEXT("taskId"), TEXT("session-20260518T120000Z"));
		Arguments.SetStringField(TEXT("pinned"), TEXT("true"));
		FUnrealMcpExecutionResult Result;
		TestTrue(TEXT("task_pin dispatches."), UnrealMcp::TryExecuteTaskAtlasTool(TEXT("unreal.task_pin"), Arguments, Result));
		TestTrue(TEXT("task_pin rejects wrong pinned type."), Result.bIsError);
		TestTrue(TEXT("task_pin reports boolean."), Result.Text.Contains(TEXT("boolean")));
	}

	return true;
}

#endif
