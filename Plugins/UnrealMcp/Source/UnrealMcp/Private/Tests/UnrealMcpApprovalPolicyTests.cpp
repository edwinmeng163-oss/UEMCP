#if WITH_DEV_AUTOMATION_TESTS

#include "Providers/UnrealMcpApprovalPolicy.h"

#include "Async/Async.h"
#include "Async/Future.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "UnrealMcpModule.h"

namespace
{
	int32 ApprovalRiskValue(UnrealMcp::Approval::ERiskLevel Risk)
	{
		return static_cast<int32>(Risk);
	}

	int32 ApprovalDecisionValue(UnrealMcp::Approval::EDecision Decision)
	{
		return static_cast<int32>(Decision);
	}

	int32 ApprovalUserDecisionValue(UnrealMcp::Approval::EUserDecision Decision)
	{
		return static_cast<int32>(Decision);
	}

	TSharedPtr<FJsonObject> MakeApprovalTestArgs()
	{
		return MakeShared<FJsonObject>();
	}

	TSharedPtr<FJsonObject> MakeApplyScaffoldArgs(bool bDryRun)
	{
		TSharedPtr<FJsonObject> Args = MakeApprovalTestArgs();
		Args->SetBoolField(TEXT("dryRun"), bDryRun);
		return Args;
	}

	TSharedPtr<FJsonObject> MakeScaffoldArgs(const FString& ImplementationTrack)
	{
		TSharedPtr<FJsonObject> Args = MakeApprovalTestArgs();
		Args->SetStringField(TEXT("implementationTrack"), ImplementationTrack);
		return Args;
	}

	TSharedPtr<FJsonObject> MakeTaskAtlasMakeArgs(bool bPreferDocumentOnly, bool bForceWriteEvenIfBlocked)
	{
		TSharedPtr<FJsonObject> Args = MakeApprovalTestArgs();
		Args->SetBoolField(TEXT("preferDocumentOnly"), bPreferDocumentOnly);
		Args->SetBoolField(TEXT("forceWriteEvenIfBlocked"), bForceWriteEvenIfBlocked);
		return Args;
	}

	TSharedPtr<FJsonObject> MakeDryRunArgs(bool bDryRun)
	{
		TSharedPtr<FJsonObject> Args = MakeApprovalTestArgs();
		Args->SetBoolField(TEXT("dryRun"), bDryRun);
		return Args;
	}

	UnrealMcp::Approval::FApprovalRequest MakeApprovalRequest(const FString& ToolName)
	{
		UnrealMcp::Approval::FApprovalRequest Request;
		Request.ToolName = ToolName;
		Request.RiskLevelLabel = TEXT("high");
		Request.ReasonHumanReadable = TEXT("test approval request");
		Request.ArgumentsPreview = MakeApprovalTestArgs();
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpApprovalPolicy_BaselineRiskTest,
	"UnrealMcp.ApprovalPolicy.BaselineRisk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpApprovalPolicy_BaselineRiskTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcp::Approval;

	TestEqual(
		TEXT("mcp_apply_scaffold baseline is high"),
		ApprovalRiskValue(GetToolBaselineRisk(TEXT("unreal.mcp_apply_scaffold"))),
		ApprovalRiskValue(ERiskLevel::High));
	TestEqual(
		TEXT("mcp_tool_audit baseline is low"),
		ApprovalRiskValue(GetToolBaselineRisk(TEXT("unreal.mcp_tool_audit"))),
		ApprovalRiskValue(ERiskLevel::Low));
	TestEqual(
		TEXT("execute_python baseline is high"),
		ApprovalRiskValue(GetToolBaselineRisk(TEXT("unreal.execute_python"))),
		ApprovalRiskValue(ERiskLevel::High));
	TestEqual(
		TEXT("unknown tool baseline is low"),
		ApprovalRiskValue(GetToolBaselineRisk(TEXT("unknown_tool"))),
		ApprovalRiskValue(ERiskLevel::Low));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpApprovalPolicy_DecisionTest,
	"UnrealMcp.ApprovalPolicy.Decision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpApprovalPolicy_DecisionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcp::Approval;

	ERiskLevel Risk = ERiskLevel::Low;
	FString Reason;

	TestEqual(
		TEXT("apply_scaffold dryRun=true is allowed"),
		ApprovalDecisionValue(EvaluateApprovalPolicy(TEXT("unreal.mcp_apply_scaffold"), MakeApplyScaffoldArgs(true), true, Risk, Reason)),
		ApprovalDecisionValue(EDecision::Allow));
	TestEqual(TEXT("apply_scaffold dryRun=true downgrades to low"), ApprovalRiskValue(Risk), ApprovalRiskValue(ERiskLevel::Low));

	TestEqual(
		TEXT("apply_scaffold dryRun=false requires approval"),
		ApprovalDecisionValue(EvaluateApprovalPolicy(TEXT("unreal.mcp_apply_scaffold"), MakeApplyScaffoldArgs(false), true, Risk, Reason)),
		ApprovalDecisionValue(EDecision::RequireApproval));
	TestEqual(TEXT("apply_scaffold dryRun=false stays high"), ApprovalRiskValue(Risk), ApprovalRiskValue(ERiskLevel::High));

	TestEqual(
		TEXT("scaffold_mcp_tool cpp requires approval"),
		ApprovalDecisionValue(EvaluateApprovalPolicy(TEXT("unreal.scaffold_mcp_tool"), MakeScaffoldArgs(TEXT("cpp")), true, Risk, Reason)),
		ApprovalDecisionValue(EDecision::RequireApproval));
	TestEqual(TEXT("scaffold_mcp_tool cpp upgrades to high"), ApprovalRiskValue(Risk), ApprovalRiskValue(ERiskLevel::High));

	TestEqual(
		TEXT("scaffold_mcp_tool python is allowed"),
		ApprovalDecisionValue(EvaluateApprovalPolicy(TEXT("unreal.scaffold_mcp_tool"), MakeScaffoldArgs(TEXT("python")), true, Risk, Reason)),
		ApprovalDecisionValue(EDecision::Allow));
	TestEqual(TEXT("scaffold_mcp_tool python stays medium"), ApprovalRiskValue(Risk), ApprovalRiskValue(ERiskLevel::Medium));

	TestEqual(
		TEXT("mcp_tool_audit is allowed"),
		ApprovalDecisionValue(EvaluateApprovalPolicy(TEXT("unreal.mcp_tool_audit"), MakeApprovalTestArgs(), true, Risk, Reason)),
		ApprovalDecisionValue(EDecision::Allow));

	TestEqual(
		TEXT("external caller bypasses approval"),
		ApprovalDecisionValue(EvaluateApprovalPolicy(TEXT("unreal.mcp_apply_scaffold"), MakeApplyScaffoldArgs(false), false, Risk, Reason)),
		ApprovalDecisionValue(EDecision::Allow));
	TestEqual(TEXT("external bypass reason"), Reason, FString(TEXT("non-assistant caller; approval bypassed")));

	TestEqual(
		TEXT("task_atlas_make_composite document-only is allowed"),
		ApprovalDecisionValue(EvaluateApprovalPolicy(TEXT("unreal.task_atlas_make_composite"), MakeTaskAtlasMakeArgs(true, false), true, Risk, Reason)),
		ApprovalDecisionValue(EDecision::Allow));
	TestEqual(TEXT("task_atlas_make_composite document-only downgrades to low"), ApprovalRiskValue(Risk), ApprovalRiskValue(ERiskLevel::Low));

	TestEqual(
		TEXT("task_atlas_make_composite real write requires approval"),
		ApprovalDecisionValue(EvaluateApprovalPolicy(TEXT("unreal.task_atlas_make_composite"), MakeTaskAtlasMakeArgs(false, false), true, Risk, Reason)),
		ApprovalDecisionValue(EDecision::RequireApproval));
	TestEqual(TEXT("task_atlas_make_composite real write is high"), ApprovalRiskValue(Risk), ApprovalRiskValue(ERiskLevel::High));

	TestEqual(
		TEXT("task_atlas_delete_made_tool dryRun=true is allowed"),
		ApprovalDecisionValue(EvaluateApprovalPolicy(TEXT("unreal.task_atlas_delete_made_tool"), MakeDryRunArgs(true), true, Risk, Reason)),
		ApprovalDecisionValue(EDecision::Allow));
	TestEqual(
		TEXT("task_atlas_delete_made_tool omitted dryRun requires approval"),
		ApprovalDecisionValue(EvaluateApprovalPolicy(TEXT("unreal.task_atlas_delete_made_tool"), MakeApprovalTestArgs(), true, Risk, Reason)),
		ApprovalDecisionValue(EDecision::RequireApproval));

	TestEqual(
		TEXT("task_atlas_promote_to_rag dryRun=true is allowed"),
		ApprovalDecisionValue(EvaluateApprovalPolicy(TEXT("unreal.task_atlas_promote_to_rag"), MakeDryRunArgs(true), true, Risk, Reason)),
		ApprovalDecisionValue(EDecision::Allow));
	TestEqual(
		TEXT("task_atlas_promote_to_rag real write requires approval"),
		ApprovalDecisionValue(EvaluateApprovalPolicy(TEXT("unreal.task_atlas_promote_to_rag"), MakeDryRunArgs(false), true, Risk, Reason)),
		ApprovalDecisionValue(EDecision::RequireApproval));

	TestEqual(
		TEXT("task_atlas_smoke_made_tool dryRun=true is allowed"),
		ApprovalDecisionValue(EvaluateApprovalPolicy(TEXT("unreal.task_atlas_smoke_made_tool"), MakeDryRunArgs(true), true, Risk, Reason)),
		ApprovalDecisionValue(EDecision::Allow));
	TestEqual(
		TEXT("task_atlas_smoke_made_tool real smoke requires approval"),
		ApprovalDecisionValue(EvaluateApprovalPolicy(TEXT("unreal.task_atlas_smoke_made_tool"), MakeDryRunArgs(false), true, Risk, Reason)),
		ApprovalDecisionValue(EDecision::RequireApproval));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpApprovalPolicy_PendingRegistryTest,
	"UnrealMcp.ApprovalPolicy.PendingRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpApprovalPolicy_PendingRegistryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcp::Approval;

	const FGuid ApprovedId = RegisterPendingApproval(MakeApprovalRequest(TEXT("unreal.mcp_apply_scaffold")));
	TestTrue(TEXT("RegisterPendingApproval returns a valid GUID"), ApprovedId.IsValid());
	TFuture<EUserDecision> ApprovedFuture = Async(EAsyncExecution::Thread, [ApprovedId]()
	{
		return WaitForApproval(ApprovedId, 1.0);
	});
	FPlatformProcess::Sleep(0.01f);
	TestTrue(TEXT("ResolveApproval approved succeeds"), ResolveApproval(ApprovedId, EUserDecision::Approved));
	TestEqual(
		TEXT("WaitForApproval returns Approved"),
		ApprovalUserDecisionValue(ApprovedFuture.Get()),
		ApprovalUserDecisionValue(EUserDecision::Approved));

	const FGuid RejectedId = RegisterPendingApproval(MakeApprovalRequest(TEXT("unreal.mcp_apply_scaffold")));
	TFuture<EUserDecision> RejectedFuture = Async(EAsyncExecution::Thread, [RejectedId]()
	{
		return WaitForApproval(RejectedId, 1.0);
	});
	FPlatformProcess::Sleep(0.01f);
	TestTrue(TEXT("ResolveApproval rejected succeeds"), ResolveApproval(RejectedId, EUserDecision::Rejected));
	TestEqual(
		TEXT("WaitForApproval returns Rejected"),
		ApprovalUserDecisionValue(RejectedFuture.Get()),
		ApprovalUserDecisionValue(EUserDecision::Rejected));

	const FGuid TimeoutId = RegisterPendingApproval(MakeApprovalRequest(TEXT("unreal.mcp_apply_scaffold")));
	TestEqual(
		TEXT("WaitForApproval returns TimedOut"),
		ApprovalUserDecisionValue(WaitForApproval(TimeoutId, 0.01)),
		ApprovalUserDecisionValue(EUserDecision::TimedOut));

	const FGuid CancelId = RegisterPendingApproval(MakeApprovalRequest(TEXT("unreal.mcp_apply_scaffold")));
	CancelApproval(CancelId);
	TestEqual(
		TEXT("CancelApproval makes WaitForApproval return Rejected"),
		ApprovalUserDecisionValue(WaitForApproval(CancelId, 1.0)),
		ApprovalUserDecisionValue(EUserDecision::Rejected));

	TestFalse(TEXT("ResolveApproval unknown ID returns false"), ResolveApproval(FGuid::NewGuid(), EUserDecision::Approved));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpApprovalPolicy_SequenceIntegrationTest,
	"UnrealMcp.ApprovalPolicy.SequenceIntegration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpApprovalPolicy_SequenceIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace UnrealMcp::Approval;

	int32 FakeExecuteCount = 0;
	TArray<FUnrealMcpAssistantEvent> ApprovalEvents;
	auto FakeExecute = [&](const FString& ToolName, const TSharedPtr<FJsonObject>& Args)
	{
		static_cast<void>(ToolName);
		static_cast<void>(Args);
		++FakeExecuteCount;
		FUnrealMcpExecutionResult Result;
		Result.Text = TEXT("ok");
		return Result;
	};

	auto RunSimulatedCall = [&](
		const FString& ToolName,
		const TSharedPtr<FJsonObject>& Args,
		bool bResolve,
		EUserDecision DecisionToResolve,
		double TimeoutSeconds) -> EUserDecision
	{
		ERiskLevel Risk = ERiskLevel::Low;
		FString Reason;
		const EDecision Decision = EvaluateApprovalPolicy(ToolName, Args, true, Risk, Reason);
		if (Decision == EDecision::Allow)
		{
			FakeExecute(ToolName, Args);
			return EUserDecision::Approved;
		}

		TestEqual(TEXT("high-risk simulated call requires approval"), ApprovalDecisionValue(Decision), ApprovalDecisionValue(EDecision::RequireApproval));

		FApprovalRequest Request;
		Request.ToolName = ToolName;
		Request.RiskLevelLabel = RiskLevelToString(Risk);
		Request.ReasonHumanReadable = Reason;
		Request.ArgumentsPreview = Args;
		const FGuid ApprovalId = RegisterPendingApproval(Request);

		FUnrealMcpAssistantEvent Event;
		Event.Type = EUnrealMcpAssistantEventType::ApprovalRequired;
		Event.ToolName = ToolName;
		Event.ApprovalIdString = ApprovalId.ToString();
		Event.ApprovalRiskLevel = RiskLevelToString(Risk);
		Event.ApprovalReason = Reason;
		ApprovalEvents.Add(Event);

		const int32 ExecuteCountBeforeDecision = FakeExecuteCount;
		TestEqual(TEXT("FakeExecute is not called before approval decision"), FakeExecuteCount, ExecuteCountBeforeDecision);

		if (bResolve)
		{
			TestTrue(TEXT("ResolveApproval succeeds for simulated call"), ResolveApproval(ApprovalId, DecisionToResolve));
		}

		const EUserDecision UserDecision = WaitForApproval(ApprovalId, TimeoutSeconds);
		if (UserDecision == EUserDecision::Approved)
		{
			FakeExecute(ToolName, Args);
		}
		return UserDecision;
	};

	TestEqual(
		TEXT("python scaffold allow path returns synthetic approved"),
		ApprovalUserDecisionValue(RunSimulatedCall(TEXT("unreal.scaffold_mcp_tool"), MakeScaffoldArgs(TEXT("python")), false, EUserDecision::Pending, 0.01)),
		ApprovalUserDecisionValue(EUserDecision::Approved));
	TestEqual(TEXT("python scaffold executes once"), FakeExecuteCount, 1);
	TestEqual(TEXT("python scaffold emits no approval event"), ApprovalEvents.Num(), 0);

	TestEqual(
		TEXT("apply dry-run allow path returns synthetic approved"),
		ApprovalUserDecisionValue(RunSimulatedCall(TEXT("unreal.mcp_apply_scaffold"), MakeApplyScaffoldArgs(true), false, EUserDecision::Pending, 0.01)),
		ApprovalUserDecisionValue(EUserDecision::Approved));
	TestEqual(TEXT("apply dry-run executes second"), FakeExecuteCount, 2);
	TestEqual(TEXT("apply dry-run emits no approval event"), ApprovalEvents.Num(), 0);

	TestEqual(
		TEXT("real apply approved decision"),
		ApprovalUserDecisionValue(RunSimulatedCall(TEXT("unreal.mcp_apply_scaffold"), MakeApplyScaffoldArgs(false), true, EUserDecision::Approved, 1.0)),
		ApprovalUserDecisionValue(EUserDecision::Approved));
	TestEqual(TEXT("real apply emits approval event"), ApprovalEvents.Num(), 1);
	TestEqual(TEXT("approval event type"), static_cast<int32>(ApprovalEvents.Last().Type), static_cast<int32>(EUnrealMcpAssistantEventType::ApprovalRequired));
	TestFalse(TEXT("approval event has id"), ApprovalEvents.Last().ApprovalIdString.IsEmpty());
	TestEqual(TEXT("approved real apply executes after decision"), FakeExecuteCount, 3);

	TestEqual(
		TEXT("real apply rejected decision"),
		ApprovalUserDecisionValue(RunSimulatedCall(TEXT("unreal.mcp_apply_scaffold"), MakeApplyScaffoldArgs(false), true, EUserDecision::Rejected, 1.0)),
		ApprovalUserDecisionValue(EUserDecision::Rejected));
	TestEqual(TEXT("rejected real apply emits another event"), ApprovalEvents.Num(), 2);
	TestEqual(TEXT("rejected real apply does not execute"), FakeExecuteCount, 3);

	TestEqual(
		TEXT("real apply timeout decision"),
		ApprovalUserDecisionValue(RunSimulatedCall(TEXT("unreal.mcp_apply_scaffold"), MakeApplyScaffoldArgs(false), false, EUserDecision::Pending, 0.01)),
		ApprovalUserDecisionValue(EUserDecision::TimedOut));
	TestEqual(TEXT("timeout real apply emits third event"), ApprovalEvents.Num(), 3);
	TestEqual(TEXT("timeout real apply does not execute"), FakeExecuteCount, 3);

	return true;
}

#endif
