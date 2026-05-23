#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "UnrealMcpModule.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpWorkflowHiddenToolStepRejectedTest,
	"UnrealMcp.Workflow.HiddenToolStepRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpWorkflowHiddenToolStepRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FJsonObject Arguments;
	Arguments.SetStringField(TEXT("workflowName"), TEXT("hidden_tool_step_rejected"));
	Arguments.SetBoolField(TEXT("dryRun"), false);
	Arguments.SetBoolField(TEXT("writeMemory"), false);

	TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
	Step->SetStringField(TEXT("name"), TEXT("hidden_apply"));
	Step->SetStringField(TEXT("tool"), TEXT("unreal.mcp_apply_scaffold"));

	TArray<TSharedPtr<FJsonValue>> Steps;
	Steps.Add(MakeShared<FJsonValueObject>(Step));
	Arguments.SetArrayField(TEXT("steps"), Steps);

	FUnrealMcpModule& Module = FModuleManager::LoadModuleChecked<FUnrealMcpModule>(TEXT("UnrealMcp"));
	const FUnrealMcpExecutionResult Result = Module.ExecuteToolFromEditorUI(TEXT("unreal.workflow_run"), Arguments);

	TestTrue(TEXT("Workflow reports an error"), Result.bIsError);
	TestTrue(TEXT("Structured content is present"), Result.StructuredContent.IsValid());
	if (!Result.StructuredContent.IsValid())
	{
		return false;
	}

	bool bSucceeded = true;
	Result.StructuredContent->TryGetBoolField(TEXT("succeeded"), bSucceeded);
	TestFalse(TEXT("Workflow did not succeed"), bSucceeded);

	double ExecutedSteps = -1.0;
	Result.StructuredContent->TryGetNumberField(TEXT("executedSteps"), ExecutedSteps);
	TestEqual(TEXT("Hidden tool handler was not invoked"), static_cast<int32>(ExecutedSteps), 0);

	const TArray<TSharedPtr<FJsonValue>>* StepResults = nullptr;
	TestTrue(TEXT("Step results are present"), Result.StructuredContent->TryGetArrayField(TEXT("steps"), StepResults) && StepResults && StepResults->Num() == 1);
	if (!StepResults || StepResults->Num() != 1)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> StepResult = (*StepResults)[0].IsValid() ? (*StepResults)[0]->AsObject() : nullptr;
	TestTrue(TEXT("Step result is an object"), StepResult.IsValid());
	if (!StepResult.IsValid())
	{
		return false;
	}

	FString Status;
	StepResult->TryGetStringField(TEXT("status"), Status);
	TestEqual(TEXT("Hidden tool step failed"), Status, TEXT("failed"));

	FString Error;
	StepResult->TryGetStringField(TEXT("error"), Error);
	TestTrue(TEXT("Hidden surface error is reported"), Error.Contains(TEXT("hidden from the AI surface")));
	TestFalse(TEXT("Handler lookup was skipped"), StepResult->HasField(TEXT("handlerName")));

	return true;
}

#endif
