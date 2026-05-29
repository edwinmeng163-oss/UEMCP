#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UnrealMcpCallToolPolicy.h"
#include "UnrealMcpToolRegistry.h"

namespace UnrealMcpCallToolPolicyTests
{
	UnrealMcp::FCallToolTargetFacts MakeFactsForTool(const FString& ToolName)
	{
		UnrealMcp::FCallToolTargetFacts Facts;
		if (const UnrealMcp::FToolRegistryEntry* Entry = UnrealMcp::FindToolRegistryEntry(ToolName))
		{
			Facts.bVisible = Entry->Exposure == UnrealMcp::EToolExposure::Visible;
		}
		Facts.SourceKind = UnrealMcp::ResolveToolSourceKind(ToolName);
		const UnrealMcp::FToolPolicy Policy = UnrealMcp::GetToolPolicy(ToolName);
		Facts.RiskLevel = Policy.RiskLevel;
		Facts.bRequiresLock = Policy.bRequiresLock;
		Facts.bRequiresWrite = Policy.bRequiresWrite;
		Facts.bRequiresRestart = Policy.bRequiresRestart;
		Facts.bRequiresExternalProcess = Policy.bRequiresExternalProcess;
		Facts.bRequiresBuild = Policy.bRequiresBuild;
		Facts.bDryRunSupport = Policy.bDryRunSupport;
		Facts.bIsWorkflowRun = ToolName == TEXT("unreal.workflow_run");
		Facts.Depth = 0;
		return Facts;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCallToolPolicyMatrixTest,
	"UnrealMcp.CallTool.PolicyMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCallToolPolicyMatrixTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	int32 AllowCount = 0;
	int32 ForceDryRunCount = 0;
	int32 DenyCount = 0;
	int32 VisibleCount = 0;

	for (const UnrealMcp::FToolRegistryEntry& Entry : UnrealMcp::GetToolRegistryEntries())
	{
		if (Entry.Exposure != UnrealMcp::EToolExposure::Visible)
		{
			continue;
		}

		++VisibleCount;
		const UnrealMcp::FCallToolTargetFacts Facts = UnrealMcpCallToolPolicyTests::MakeFactsForTool(Entry.Name);
		const UnrealMcp::FCallToolPolicyResult Result = UnrealMcp::ClassifyCallToolTarget_Pure(Facts);
		switch (Result.Decision)
		{
		case UnrealMcp::ECallToolDecision::Allow:
			++AllowCount;
			break;
		case UnrealMcp::ECallToolDecision::ForceDryRun:
			++ForceDryRunCount;
			break;
		case UnrealMcp::ECallToolDecision::Deny:
			++DenyCount;
			break;
		default:
			AddError(FString::Printf(TEXT("Unexpected call_tool decision for %s"), *Entry.Name));
			break;
		}
	}

	TestEqual(TEXT("visible tool count"), VisibleCount, 169);
	TestEqual(TEXT("force dry run count"), ForceDryRunCount, 26);
	TestEqual(TEXT("deny count"), DenyCount, 82);
	TestEqual(TEXT("allow count"), AllowCount, 61);

	auto ExpectDecision = [this](
		const TCHAR* ToolName,
		UnrealMcp::ECallToolDecision ExpectedDecision,
		const TCHAR* ExpectedReason)
	{
		const UnrealMcp::FCallToolTargetFacts Facts = UnrealMcpCallToolPolicyTests::MakeFactsForTool(ToolName);
		const UnrealMcp::FCallToolPolicyResult Result = UnrealMcp::ClassifyCallToolTarget_Pure(Facts);
		TestTrue(
			FString::Printf(TEXT("%s decision"), ToolName),
			Result.Decision == ExpectedDecision);
		TestEqual(
			FString::Printf(TEXT("%s reason"), ToolName),
			Result.Reason,
			FString(ExpectedReason));
	};

	ExpectDecision(TEXT("unreal.code_apply_change"), UnrealMcp::ECallToolDecision::ForceDryRun, TEXT("force_dry_run"));
	ExpectDecision(TEXT("unreal.execute_python"), UnrealMcp::ECallToolDecision::Deny, TEXT("dangerous_no_dryrun"));
	ExpectDecision(TEXT("unreal.editor_status"), UnrealMcp::ECallToolDecision::Allow, TEXT(""));
	ExpectDecision(TEXT("unreal.workflow_run"), UnrealMcp::ECallToolDecision::Deny, TEXT("workflow_run_forbidden"));
	ExpectDecision(TEXT("unreal.batch_configure_static_mesh_actors"), UnrealMcp::ECallToolDecision::Deny, TEXT("dangerous_no_dryrun"));
	ExpectDecision(TEXT("unreal.batch_set_point_light_properties"), UnrealMcp::ECallToolDecision::Deny, TEXT("dangerous_no_dryrun"));

	UnrealMcp::FCallToolTargetFacts DepthFacts = UnrealMcpCallToolPolicyTests::MakeFactsForTool(TEXT("unreal.editor_status"));
	DepthFacts.Depth = 1;
	const UnrealMcp::FCallToolPolicyResult DepthResult = UnrealMcp::ClassifyCallToolTarget_Pure(DepthFacts);
	TestTrue(TEXT("depth decision"), DepthResult.Decision == UnrealMcp::ECallToolDecision::Deny);
	TestEqual(TEXT("depth reason"), DepthResult.Reason, TEXT("call_tool_depth_exceeded"));

	UnrealMcp::FCallToolTargetFacts UserFacts = UnrealMcpCallToolPolicyTests::MakeFactsForTool(TEXT("unreal.editor_status"));
	UserFacts.SourceKind = UnrealMcp::Extension::ESourceKind::UserRegistry;
	const UnrealMcp::FCallToolPolicyResult UserResult = UnrealMcp::ClassifyCallToolTarget_Pure(UserFacts);
	TestTrue(TEXT("user registry decision"), UserResult.Decision == UnrealMcp::ECallToolDecision::Deny);
	TestEqual(TEXT("user registry reason"), UserResult.Reason, TEXT("user_tool_forbidden"));

	UnrealMcp::FCallToolTargetFacts HiddenFacts = UnrealMcpCallToolPolicyTests::MakeFactsForTool(TEXT("unreal.editor_status"));
	HiddenFacts.bVisible = false;
	const UnrealMcp::FCallToolPolicyResult HiddenResult = UnrealMcp::ClassifyCallToolTarget_Pure(HiddenFacts);
	TestTrue(TEXT("hidden decision"), HiddenResult.Decision == UnrealMcp::ECallToolDecision::Deny);
	TestEqual(TEXT("hidden reason"), HiddenResult.Reason, TEXT("not_visible"));

	return true;
}

#endif
