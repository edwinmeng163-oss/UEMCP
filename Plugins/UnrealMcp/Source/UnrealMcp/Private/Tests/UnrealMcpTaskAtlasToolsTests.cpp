#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
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
		bool bCompletionMarker = false,
		const FString& EventId = FString(),
		const FString& CaptureStatus = FString(),
		const FString& CaptureRef = FString())
	{
		UnrealMcp::FTaskAtlasEventRecord Event;
		Event.SessionId = SessionId;
		Event.EventId = EventId;
		Event.Timestamp = Timestamp;
		Event.TimestampUtc = Timestamp.ToIso8601();
		Event.EventKind = EventKind;
		Event.ToolName = ToolName;
		Event.CaptureStatus = CaptureStatus;
		Event.CaptureRef = CaptureRef;
		Event.Content = EventKind == TEXT("user_intent") ? TEXT("Build a test workflow. Done.") : TEXT("Completed.");
		Event.bCompletionMarker = bCompletionMarker;
		return Event;
	}

	TSharedPtr<FJsonObject> GetObjectAt(const TArray<TSharedPtr<FJsonValue>>& Values, int32 Index)
	{
		return Values.IsValidIndex(Index) && Values[Index].IsValid() && Values[Index]->Type == EJson::Object ? Values[Index]->AsObject() : nullptr;
	}

	FString GetStringField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		FString Value;
		if (Object.IsValid())
		{
			Object->TryGetStringField(FieldName, Value);
		}
		return Value;
	}

	int32 GetIntField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		double Value = -1.0;
		if (Object.IsValid())
		{
			Object->TryGetNumberField(FieldName, Value);
		}
		return static_cast<int32>(Value);
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
	FUnrealMcpTaskAtlasSchemaV2Test,
	"UnrealMcp.TaskAtlas.SchemaV2",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasSchemaV2Test::RunTest(const FString& Parameters)
{
	(void)Parameters;

	{
		const FString Line = TEXT("{\"sessionId\":\"schema-v2-parse\",\"eventId\":\"event-parse-1\",\"ts\":\"2026-05-18T12:00:00Z\",\"eventKind\":\"tool_call\",\"payload\":{\"toolName\":\"unreal.editor_status\",\"captureStatus\":\"captured\",\"captureRef\":\"CapturedToolArgs/schema-v2-parse/event-parse-1.json\",\"isError\":false}}");
		UnrealMcp::FTaskAtlasEventRecord Event;
		TestTrue(TEXT("activity line parses"), UnrealMcp::ParseTaskAtlasActivityLineForTests(Line, Event));
		TestEqual(TEXT("eventId parsed from top-level record"), Event.EventId, TEXT("event-parse-1"));
		TestEqual(TEXT("captureStatus parsed from payload"), Event.CaptureStatus, TEXT("captured"));
		TestEqual(TEXT("captureRef parsed from payload"), Event.CaptureRef, TEXT("CapturedToolArgs/schema-v2-parse/event-parse-1.json"));
	}

	const FDateTime Base(2026, 5, 18, 12, 0, 0);
	TArray<UnrealMcp::FTaskAtlasEventRecord> Events;
	Events.Add(MakeTaskAtlasEvent(TEXT("schema-v2"), Base, TEXT("user_intent")));
	Events.Add(MakeTaskAtlasEvent(TEXT("schema-v2"), Base + FTimespan::FromSeconds(1), TEXT("tool_call"), TEXT("unreal.editor_status"), false, TEXT("event-step-0"), TEXT("captured"), TEXT("CapturedToolArgs/schema-v2/event-step-0.json")));
	Events.Add(MakeTaskAtlasEvent(TEXT("schema-v2"), Base + FTimespan::FromSeconds(2), TEXT("tool_call"), TEXT("unreal.editor_status"), false, TEXT("event-step-1"), TEXT("captured"), TEXT("CapturedToolArgs/schema-v2/event-step-1.json")));
	Events.Add(MakeTaskAtlasEvent(TEXT("schema-v2"), Base + FTimespan::FromSeconds(3), TEXT("tool_call"), TEXT("unreal.code_apply_change"), false, TEXT("event-step-2"), TEXT("redacted"), TEXT("CapturedToolArgs/schema-v2/event-step-2.json")));
	Events.Add(MakeTaskAtlasEvent(TEXT("schema-v2"), Base + FTimespan::FromSeconds(4), TEXT("ai_summary"), FString(), true));

	TSharedPtr<FJsonObject> TaskJson = UnrealMcp::BuildTaskAtlasTaskJsonForTests(Events);
	TestTrue(TEXT("task json builds"), TaskJson.IsValid());
	if (!TaskJson.IsValid())
	{
		return false;
	}

	double SchemaVersion = 0.0;
	TaskJson->TryGetNumberField(TEXT("schemaVersion"), SchemaVersion);
	TestEqual(TEXT("schemaVersion bumped"), SchemaVersion, 2.0);

	const TArray<TSharedPtr<FJsonValue>>* CriticalPath = nullptr;
	TestTrue(TEXT("criticalPath array"), TaskJson->TryGetArrayField(TEXT("criticalPath"), CriticalPath) && CriticalPath);
	if (CriticalPath)
	{
		TestEqual(TEXT("criticalPath remains deduped"), CriticalPath->Num(), 2);
		TestEqual(TEXT("criticalPath first tool"), (*CriticalPath)[0]->AsString(), TEXT("unreal.editor_status"));
		TestEqual(TEXT("criticalPath second tool"), (*CriticalPath)[1]->AsString(), TEXT("unreal.code_apply_change"));
	}

	const TArray<TSharedPtr<FJsonValue>>* StepRefs = nullptr;
	TestTrue(TEXT("stepRefs array"), TaskJson->TryGetArrayField(TEXT("stepRefs"), StepRefs) && StepRefs);
	if (!StepRefs)
	{
		return false;
	}
	TestEqual(TEXT("stepRefs preserve duplicate tool calls"), StepRefs->Num(), 3);
	TestEqual(TEXT("stepRefTotal"), GetIntField(TaskJson, TEXT("stepRefTotal")), 3);
	bool bTruncated = true;
	TaskJson->TryGetBoolField(TEXT("stepRefsTruncated"), bTruncated);
	TestFalse(TEXT("stepRefs not truncated"), bTruncated);

	const TSharedPtr<FJsonObject> Step0 = GetObjectAt(*StepRefs, 0);
	const TSharedPtr<FJsonObject> Step1 = GetObjectAt(*StepRefs, 1);
	const TSharedPtr<FJsonObject> Step2 = GetObjectAt(*StepRefs, 2);
	TestEqual(TEXT("step0 ordinal"), GetIntField(Step0, TEXT("ordinal")), 0);
	TestEqual(TEXT("step0 eventId"), GetStringField(Step0, TEXT("eventId")), TEXT("event-step-0"));
	TestEqual(TEXT("step0 policy"), GetStringField(Step0, TEXT("policyClassAtCapture")), TEXT("allow"));
	TestEqual(TEXT("step1 ordinal"), GetIntField(Step1, TEXT("ordinal")), 1);
	TestEqual(TEXT("step1 duplicate tool retained"), GetStringField(Step1, TEXT("tool")), TEXT("unreal.editor_status"));
	TestEqual(TEXT("step1 captureRef"), GetStringField(Step1, TEXT("captureRef")), TEXT("CapturedToolArgs/schema-v2/event-step-1.json"));
	TestEqual(TEXT("step2 ordinal"), GetIntField(Step2, TEXT("ordinal")), 2);
	TestEqual(TEXT("step2 force-dry-run policy"), GetStringField(Step2, TEXT("policyClassAtCapture")), TEXT("force_dry_run"));
	TestEqual(TEXT("step2 redacted accepted"), GetStringField(Step2, TEXT("captureStatus")), TEXT("redacted"));
	TestEqual(TEXT("preview eligibility"), GetStringField(TaskJson, TEXT("replayEligibility")), TEXT("preview_ready"));
	TestEqual(TEXT("preview reason empty"), GetStringField(TaskJson, TEXT("replayUnavailableReason")), TEXT(""));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpTaskAtlasReplayEligibilityTest,
	"UnrealMcp.TaskAtlas.ReplayEligibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpTaskAtlasReplayEligibilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FDateTime Base(2026, 5, 18, 13, 0, 0);

	{
		TArray<UnrealMcp::FTaskAtlasEventRecord> Events;
		Events.Add(MakeTaskAtlasEvent(TEXT("schema-v2-partial"), Base, TEXT("tool_call"), TEXT("unreal.editor_status"), false, TEXT("partial-0"), TEXT("captured"), TEXT("CapturedToolArgs/schema-v2-partial/partial-0.json")));
		Events.Add(MakeTaskAtlasEvent(TEXT("schema-v2-partial"), Base + FTimespan::FromSeconds(1), TEXT("tool_call"), TEXT("unreal.editor_status"), false, TEXT("partial-1")));
		TSharedPtr<FJsonObject> TaskJson = UnrealMcp::BuildTaskAtlasTaskJsonForTests(Events);
		TestTrue(TEXT("partial task json builds"), TaskJson.IsValid());
		TestEqual(TEXT("partial eligibility"), GetStringField(TaskJson, TEXT("replayEligibility")), TEXT("partial"));
		TestTrue(TEXT("partial reason names missing captureRef"), GetStringField(TaskJson, TEXT("replayUnavailableReason")).Contains(TEXT("missing captureRef")));
	}

	{
		TArray<UnrealMcp::FTaskAtlasEventRecord> Events;
		Events.Add(MakeTaskAtlasEvent(TEXT("schema-v2-old"), Base, TEXT("tool_call"), TEXT("unreal.editor_status"), false, TEXT("old-0")));
		TSharedPtr<FJsonObject> TaskJson = UnrealMcp::BuildTaskAtlasTaskJsonForTests(Events);
		TestTrue(TEXT("old task json builds"), TaskJson.IsValid());
		TestEqual(TEXT("old task downgrades to skeleton"), GetStringField(TaskJson, TEXT("replayEligibility")), TEXT("skeleton_pre_capture"));
		TestTrue(TEXT("old task reason names no captures"), GetStringField(TaskJson, TEXT("replayUnavailableReason")).Contains(TEXT("no captured")));
	}

	{
		TArray<UnrealMcp::FTaskAtlasEventRecord> Events;
		Events.Add(MakeTaskAtlasEvent(TEXT("schema-v2-blocked"), Base, TEXT("tool_call"), TEXT("unreal.execute_python"), false, TEXT("blocked-0"), TEXT("captured"), TEXT("CapturedToolArgs/schema-v2-blocked/blocked-0.json")));
		TSharedPtr<FJsonObject> TaskJson = UnrealMcp::BuildTaskAtlasTaskJsonForTests(Events);
		TestTrue(TEXT("blocked task json builds"), TaskJson.IsValid());
		TestEqual(TEXT("blocked eligibility"), GetStringField(TaskJson, TEXT("replayEligibility")), TEXT("blocked"));
		TestTrue(TEXT("blocked reason names denied tool"), GetStringField(TaskJson, TEXT("replayUnavailableReason")).Contains(TEXT("unreal.execute_python")));
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
