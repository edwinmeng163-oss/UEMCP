#include "UnrealMcpModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealMcpMemoryTools.h"
#include "UnrealMcpToolHandlerRegistry.h"
#include "UnrealMcpToolRegistry.h"

namespace UnrealMcp
{
	TSharedPtr<FJsonObject> MakeEmptyObject();
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);
	bool LoadJsonObject(const FString& JsonText, TSharedPtr<FJsonObject>& OutObject);
	FString JsonObjectToString(const TSharedPtr<FJsonObject>& JsonObject);
	TArray<TSharedPtr<FJsonValue>> MakeJsonStringArray(const TArray<FString>& Values);
	bool ResolveProjectPathInsideProject(const FString& RequestedPath, FString& OutPath, FString& OutFailureReason);

	namespace
	{
		static constexpr int32 WorkflowDefaultMaxSteps = 20;
		static constexpr int32 WorkflowHardMaxSteps = 100;

		bool GetBoolArgument(const FJsonObject& Object, const FString& FieldName, bool bDefaultValue)
		{
			bool bValue = bDefaultValue;
			Object.TryGetBoolField(FieldName, bValue);
			return bValue;
		}

		int32 GetClampedIntArgument(const FJsonObject& Object, const FString& FieldName, int32 DefaultValue, int32 MinValue, int32 MaxValue)
		{
			double NumberValue = static_cast<double>(DefaultValue);
			Object.TryGetNumberField(FieldName, NumberValue);
			return FMath::Clamp(static_cast<int32>(NumberValue), MinValue, MaxValue);
		}

		FString GetStringArgument(const FJsonObject& Object, const FString& FieldName, const FString& DefaultValue = FString())
		{
			FString Value = DefaultValue;
			Object.TryGetStringField(FieldName, Value);
			return Value;
		}

		void CopyOverrideField(const FJsonObject& Source, const TSharedPtr<FJsonObject>& Target, const FString& FieldName)
		{
			const TSharedPtr<FJsonValue>* Value = Source.Values.Find(FieldName);
			if (Value && Value->IsValid())
			{
				Target->SetField(FieldName, *Value);
			}
		}

		bool LoadWorkflowObject(const FJsonObject& Arguments, TSharedPtr<FJsonObject>& OutWorkflowObject, FString& OutSource, FString& OutFailureReason)
		{
			FString WorkflowJson;
			Arguments.TryGetStringField(TEXT("workflowJson"), WorkflowJson);
			if (!WorkflowJson.IsEmpty())
			{
				if (!LoadJsonObject(WorkflowJson, OutWorkflowObject) || !OutWorkflowObject.IsValid())
				{
					OutFailureReason = TEXT("workflowJson must be a valid JSON object.");
					return false;
				}
				OutSource = TEXT("workflowJson");
			}
			else
			{
				FString WorkflowPath;
				Arguments.TryGetStringField(TEXT("workflowPath"), WorkflowPath);
				if (!WorkflowPath.IsEmpty())
				{
					FString ResolvedPath;
					if (!ResolveProjectPathInsideProject(WorkflowPath, ResolvedPath, OutFailureReason))
					{
						return false;
					}

					FString FileText;
					if (!FFileHelper::LoadFileToString(FileText, *ResolvedPath))
					{
						OutFailureReason = FString::Printf(TEXT("Failed to read workflowPath '%s'."), *ResolvedPath);
						return false;
					}

					if (!LoadJsonObject(FileText, OutWorkflowObject) || !OutWorkflowObject.IsValid())
					{
						OutFailureReason = FString::Printf(TEXT("workflowPath '%s' did not contain a valid JSON object."), *ResolvedPath);
						return false;
					}
					OutSource = ResolvedPath;
				}
				else
				{
					OutWorkflowObject = MakeShared<FJsonObject>();
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments.Values)
					{
						OutWorkflowObject->SetField(Pair.Key, Pair.Value);
					}
					OutSource = TEXT("inline");
				}
			}

			static const TArray<FString> OverrideFields = {
				TEXT("workflowName"),
				TEXT("dryRun"),
				TEXT("stopOnFailure"),
				TEXT("maxSteps"),
				TEXT("writeMemory"),
				TEXT("memoryKey"),
				TEXT("allowHighRisk"),
				TEXT("allowCritical"),
				TEXT("includeStepStructuredContent"),
				TEXT("maxResultChars")
			};

			for (const FString& FieldName : OverrideFields)
			{
				CopyOverrideField(Arguments, OutWorkflowObject, FieldName);
			}
			return true;
		}

		bool GetStepArguments(const TSharedPtr<FJsonObject>& StepObject, TSharedPtr<FJsonObject>& OutArguments, FString& OutFailureReason)
		{
			OutArguments = MakeEmptyObject();
			if (!StepObject.IsValid())
			{
				OutFailureReason = TEXT("Workflow step was not a valid object.");
				return false;
			}

			FString ArgumentsJson;
			StepObject->TryGetStringField(TEXT("argumentsJson"), ArgumentsJson);
			if (!ArgumentsJson.IsEmpty())
			{
				if (!LoadJsonObject(ArgumentsJson, OutArguments) || !OutArguments.IsValid())
				{
					OutFailureReason = TEXT("Step argumentsJson must be a valid JSON object.");
					return false;
				}
				return true;
			}

			const TSharedPtr<FJsonObject>* ArgumentsObject = nullptr;
			if (StepObject->TryGetObjectField(TEXT("arguments"), ArgumentsObject) && ArgumentsObject && (*ArgumentsObject).IsValid())
			{
				OutArguments = *ArgumentsObject;
			}
			return true;
		}

		bool IsRiskAllowed(EToolRiskLevel RiskLevel, bool bAllowHighRisk, bool bAllowCritical)
		{
			if (RiskLevel == EToolRiskLevel::Critical)
			{
				return bAllowCritical;
			}
			if (RiskLevel == EToolRiskLevel::High)
			{
				return bAllowHighRisk;
			}
			return true;
		}

		FString MakePolicyBlockReason(const FString& ToolName, EToolRiskLevel RiskLevel)
		{
			if (RiskLevel == EToolRiskLevel::Critical)
			{
				return FString::Printf(TEXT("Step tool '%s' is critical risk. Pass allowCritical=true to execute it intentionally."), *ToolName);
			}
			if (RiskLevel == EToolRiskLevel::High)
			{
				return FString::Printf(TEXT("Step tool '%s' is high risk. Pass allowHighRisk=true to execute it intentionally."), *ToolName);
			}
			return FString();
		}

		FString ClipText(const FString& Text, int32 MaxChars)
		{
			if (Text.Len() <= MaxChars)
			{
				return Text;
			}
			return Text.Left(MaxChars) + TEXT("...");
		}

		TSharedPtr<FJsonObject> MakeWorkflowMemoryContent(
			const FString& WorkflowName,
			const FString& Status,
			const FString& Source,
			bool bDryRun,
			int32 TotalSteps,
			int32 ExecutedSteps,
			int32 FailedStepIndex,
			const TArray<TSharedPtr<FJsonValue>>& StepResults)
		{
			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetStringField(TEXT("workflowName"), WorkflowName);
			Content->SetStringField(TEXT("status"), Status);
			Content->SetStringField(TEXT("source"), Source);
			Content->SetBoolField(TEXT("dryRun"), bDryRun);
			Content->SetNumberField(TEXT("totalSteps"), TotalSteps);
			Content->SetNumberField(TEXT("executedSteps"), ExecutedSteps);
			Content->SetNumberField(TEXT("failedStepIndex"), FailedStepIndex);
			Content->SetArrayField(TEXT("stepResults"), StepResults);
			return Content;
		}

		FUnrealMcpExecutionResult WriteWorkflowMemory(
			const FString& MemoryKey,
			const FString& WorkflowName,
			const FString& Status,
			const FString& Source,
			bool bDryRun,
			int32 TotalSteps,
			int32 ExecutedSteps,
			int32 FailedStepIndex,
			const TArray<TSharedPtr<FJsonValue>>& StepResults)
		{
			TSharedPtr<FJsonObject> Content = MakeWorkflowMemoryContent(
				WorkflowName,
				Status,
				Source,
				bDryRun,
				TotalSteps,
				ExecutedSteps,
				FailedStepIndex,
				StepResults);

			TSharedPtr<FJsonObject> MemoryArgs = MakeShared<FJsonObject>();
			MemoryArgs->SetStringField(TEXT("key"), MemoryKey);
			MemoryArgs->SetStringField(TEXT("summary"), FString::Printf(TEXT("Workflow '%s' %s."), *WorkflowName, *Status));
			MemoryArgs->SetStringField(TEXT("status"), Status);
			MemoryArgs->SetStringField(TEXT("nextStep"), FailedStepIndex >= 0 ? TEXT("Inspect failed step, adjust arguments or permissions, then rerun workflow_run.") : TEXT("Use the workflow results as task evidence or continue with the next planned workflow."));
			MemoryArgs->SetStringField(TEXT("contentJson"), JsonObjectToString(Content));
			MemoryArgs->SetArrayField(TEXT("tags"), MakeJsonStringArray({ TEXT("workflow"), TEXT("mcp"), TEXT("chat.active_task") }));
			return ProjectMemoryWrite(*MemoryArgs);
		}
	}
}

FUnrealMcpExecutionResult FUnrealMcpModule::RunWorkflow(const FJsonObject& Arguments) const
{
	using namespace UnrealMcp;

	TSharedPtr<FJsonObject> WorkflowObject;
	FString WorkflowSource;
	FString FailureReason;
	if (!LoadWorkflowObject(Arguments, WorkflowObject, WorkflowSource, FailureReason))
	{
		TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
		StructuredContent->SetStringField(TEXT("action"), TEXT("workflow_run"));
		StructuredContent->SetStringField(TEXT("error"), FailureReason);
		return MakeExecutionResult(FailureReason, StructuredContent, true);
	}

	const FString WorkflowName = GetStringArgument(*WorkflowObject, TEXT("workflowName"), TEXT("unnamed_workflow"));
	const bool bDryRun = GetBoolArgument(*WorkflowObject, TEXT("dryRun"), true);
	const bool bStopOnFailure = GetBoolArgument(*WorkflowObject, TEXT("stopOnFailure"), true);
	const bool bWriteMemory = GetBoolArgument(*WorkflowObject, TEXT("writeMemory"), true);
	const bool bAllowHighRisk = GetBoolArgument(*WorkflowObject, TEXT("allowHighRisk"), false);
	const bool bAllowCritical = GetBoolArgument(*WorkflowObject, TEXT("allowCritical"), false);
	const bool bIncludeStepStructuredContent = GetBoolArgument(*WorkflowObject, TEXT("includeStepStructuredContent"), false);
	const FString MemoryKey = GetStringArgument(*WorkflowObject, TEXT("memoryKey"), TEXT("chat.active_task"));
	const int32 MaxSteps = GetClampedIntArgument(*WorkflowObject, TEXT("maxSteps"), WorkflowDefaultMaxSteps, 1, WorkflowHardMaxSteps);
	const int32 MaxResultChars = GetClampedIntArgument(*WorkflowObject, TEXT("maxResultChars"), 1200, 100, 20000);

	const TArray<TSharedPtr<FJsonValue>>* StepsArray = nullptr;
	if (!WorkflowObject->TryGetArrayField(TEXT("steps"), StepsArray) || !StepsArray || StepsArray->Num() == 0)
	{
		TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
		StructuredContent->SetStringField(TEXT("action"), TEXT("workflow_run"));
		StructuredContent->SetStringField(TEXT("workflowName"), WorkflowName);
		StructuredContent->SetStringField(TEXT("source"), WorkflowSource);
		StructuredContent->SetStringField(TEXT("error"), TEXT("Workflow requires a non-empty steps array."));
		return MakeExecutionResult(TEXT("Workflow requires a non-empty steps array."), StructuredContent, true);
	}

	if (StepsArray->Num() > MaxSteps)
	{
		TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
		StructuredContent->SetStringField(TEXT("action"), TEXT("workflow_run"));
		StructuredContent->SetStringField(TEXT("workflowName"), WorkflowName);
		StructuredContent->SetStringField(TEXT("source"), WorkflowSource);
		StructuredContent->SetNumberField(TEXT("totalSteps"), StepsArray->Num());
		StructuredContent->SetNumberField(TEXT("maxSteps"), MaxSteps);
		StructuredContent->SetStringField(TEXT("error"), TEXT("Workflow exceeded maxSteps."));
		return MakeExecutionResult(
			FString::Printf(TEXT("Workflow '%s' has %d steps, exceeding maxSteps=%d."), *WorkflowName, StepsArray->Num(), MaxSteps),
			StructuredContent,
			true);
	}

	TArray<TSharedPtr<FJsonValue>> StepResults;
	int32 ExecutedSteps = 0;
	int32 PlannedSteps = 0;
	int32 SkippedSteps = 0;
	int32 FailedStepIndex = -1;
	bool bHadFailure = false;

	for (int32 StepIndex = 0; StepIndex < StepsArray->Num(); ++StepIndex)
	{
		const TSharedPtr<FJsonValue>& StepValue = (*StepsArray)[StepIndex];
		TSharedPtr<FJsonObject> StepResult = MakeShared<FJsonObject>();
		StepResult->SetNumberField(TEXT("index"), StepIndex);

		if (!StepValue.IsValid() || StepValue->Type != EJson::Object || !StepValue->AsObject().IsValid())
		{
			StepResult->SetStringField(TEXT("status"), TEXT("failed"));
			StepResult->SetStringField(TEXT("error"), TEXT("Step must be a JSON object."));
			StepResults.Add(MakeShared<FJsonValueObject>(StepResult));
			bHadFailure = true;
			FailedStepIndex = StepIndex;
			if (bStopOnFailure)
			{
				break;
			}
			continue;
		}

		const TSharedPtr<FJsonObject> StepObject = StepValue->AsObject();
		const FString StepName = GetStringArgument(*StepObject, TEXT("name"), FString::Printf(TEXT("step_%d"), StepIndex + 1));
		const FString StepTool = GetStringArgument(*StepObject, TEXT("tool"));
		const bool bSkip = GetBoolArgument(*StepObject, TEXT("skip"), false);
		const bool bExpectError = GetBoolArgument(*StepObject, TEXT("expectError"), false);
		const bool bContinueOnError = GetBoolArgument(*StepObject, TEXT("continueOnError"), false);

		StepResult->SetStringField(TEXT("name"), StepName);
		StepResult->SetStringField(TEXT("tool"), StepTool);
		StepResult->SetBoolField(TEXT("expectError"), bExpectError);
		StepResult->SetBoolField(TEXT("continueOnError"), bContinueOnError);

		if (bSkip)
		{
			StepResult->SetStringField(TEXT("status"), TEXT("skipped"));
			StepResults.Add(MakeShared<FJsonValueObject>(StepResult));
			++SkippedSteps;
			continue;
		}

		if (StepTool.IsEmpty())
		{
			StepResult->SetStringField(TEXT("status"), TEXT("failed"));
			StepResult->SetStringField(TEXT("error"), TEXT("Step requires a tool field."));
			StepResults.Add(MakeShared<FJsonValueObject>(StepResult));
			bHadFailure = true;
			FailedStepIndex = StepIndex;
			if (bStopOnFailure && !bContinueOnError)
			{
				break;
			}
			continue;
		}

		if (StepTool == TEXT("unreal.workflow_run"))
		{
			StepResult->SetStringField(TEXT("status"), TEXT("failed"));
			StepResult->SetStringField(TEXT("error"), TEXT("Nested unreal.workflow_run calls are blocked to avoid recursive pipelines."));
			StepResults.Add(MakeShared<FJsonValueObject>(StepResult));
			bHadFailure = true;
			FailedStepIndex = StepIndex;
			if (bStopOnFailure && !bContinueOnError)
			{
				break;
			}
			continue;
		}

		const FString HandlerName = ResolveToolHandlerName(StepTool);
		const FToolHandlerRegistryEntry* HandlerEntry = FindToolHandlerRegistryEntry(HandlerName);
		const FToolPolicy StepPolicy = GetToolPolicy(StepTool);
		StepResult->SetStringField(TEXT("handlerName"), HandlerName);
		StepResult->SetBoolField(TEXT("hasHandler"), HandlerEntry != nullptr);
		StepResult->SetStringField(TEXT("riskLevel"), LexToString(StepPolicy.RiskLevel));
		StepResult->SetBoolField(TEXT("requiresWrite"), StepPolicy.bRequiresWrite);
		StepResult->SetBoolField(TEXT("requiresBuild"), StepPolicy.bRequiresBuild);
		StepResult->SetBoolField(TEXT("requiresExternalProcess"), StepPolicy.bRequiresExternalProcess);

		if (!HandlerEntry)
		{
			StepResult->SetStringField(TEXT("status"), TEXT("failed"));
			StepResult->SetStringField(TEXT("error"), FString::Printf(TEXT("Tool '%s' has no registered handler."), *StepTool));
			StepResults.Add(MakeShared<FJsonValueObject>(StepResult));
			bHadFailure = true;
			FailedStepIndex = StepIndex;
			if (bStopOnFailure && !bContinueOnError)
			{
				break;
			}
			continue;
		}

		const bool bPolicyAllowed = IsRiskAllowed(StepPolicy.RiskLevel, bAllowHighRisk, bAllowCritical);
		StepResult->SetBoolField(TEXT("policyAllowed"), bPolicyAllowed);
		if (!bPolicyAllowed)
		{
			const FString PolicyBlockReason = MakePolicyBlockReason(StepTool, StepPolicy.RiskLevel);
			StepResult->SetStringField(TEXT("policyBlockReason"), PolicyBlockReason);
			if (!bDryRun)
			{
				StepResult->SetStringField(TEXT("status"), TEXT("blocked"));
				StepResults.Add(MakeShared<FJsonValueObject>(StepResult));
				bHadFailure = true;
				FailedStepIndex = StepIndex;
				if (bStopOnFailure && !bContinueOnError)
				{
					break;
				}
				continue;
			}
		}

		TSharedPtr<FJsonObject> StepArguments;
		if (!GetStepArguments(StepObject, StepArguments, FailureReason))
		{
			StepResult->SetStringField(TEXT("status"), TEXT("failed"));
			StepResult->SetStringField(TEXT("error"), FailureReason);
			StepResults.Add(MakeShared<FJsonValueObject>(StepResult));
			bHadFailure = true;
			FailedStepIndex = StepIndex;
			if (bStopOnFailure && !bContinueOnError)
			{
				break;
			}
			continue;
		}

		TArray<FString> ArgumentKeys;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : StepArguments->Values)
		{
			ArgumentKeys.Add(Pair.Key);
		}
		ArgumentKeys.Sort();
		StepResult->SetArrayField(TEXT("argumentKeys"), MakeJsonStringArray(ArgumentKeys));

		if (bDryRun)
		{
			StepResult->SetStringField(TEXT("status"), bPolicyAllowed ? TEXT("planned") : TEXT("planned_blocked_by_policy"));
			StepResults.Add(MakeShared<FJsonValueObject>(StepResult));
			++PlannedSteps;
			continue;
		}

		FUnrealMcpExecutionResult StepExecutionResult = ExecuteTool(StepTool, *StepArguments);
		++ExecutedSteps;
		StepResult->SetBoolField(TEXT("toolReturnedError"), StepExecutionResult.bIsError);
		StepResult->SetStringField(TEXT("textPreview"), ClipText(StepExecutionResult.Text, MaxResultChars));
		if (bIncludeStepStructuredContent && StepExecutionResult.StructuredContent.IsValid())
		{
			StepResult->SetObjectField(TEXT("structuredContent"), StepExecutionResult.StructuredContent);
		}

		bool bStepSucceeded = false;
		if (bExpectError)
		{
			bStepSucceeded = StepExecutionResult.bIsError;
			StepResult->SetStringField(TEXT("status"), bStepSucceeded ? TEXT("expected_error") : TEXT("failed_expected_error_not_observed"));
		}
		else
		{
			bStepSucceeded = !StepExecutionResult.bIsError;
			StepResult->SetStringField(TEXT("status"), bStepSucceeded ? TEXT("completed") : TEXT("failed"));
		}

		if (!bStepSucceeded)
		{
			bHadFailure = true;
			FailedStepIndex = StepIndex;
		}

		StepResults.Add(MakeShared<FJsonValueObject>(StepResult));
		if (!bStepSucceeded && bStopOnFailure && !bContinueOnError)
		{
			break;
		}
	}

	const FString WorkflowStatus = bDryRun
		? TEXT("planned")
		: (bHadFailure ? TEXT("paused") : TEXT("completed"));

	FUnrealMcpExecutionResult MemoryResult;
	bool bMemoryWriteAttempted = false;
	if (bWriteMemory)
	{
		bMemoryWriteAttempted = true;
		MemoryResult = WriteWorkflowMemory(
			MemoryKey,
			WorkflowName,
			WorkflowStatus,
			WorkflowSource,
			bDryRun,
			StepsArray->Num(),
			ExecutedSteps,
			FailedStepIndex,
			StepResults);
	}

	TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
	StructuredContent->SetStringField(TEXT("action"), TEXT("workflow_run"));
	StructuredContent->SetStringField(TEXT("workflowName"), WorkflowName);
	StructuredContent->SetStringField(TEXT("source"), WorkflowSource);
	StructuredContent->SetStringField(TEXT("status"), WorkflowStatus);
	StructuredContent->SetBoolField(TEXT("dryRun"), bDryRun);
	StructuredContent->SetBoolField(TEXT("succeeded"), !bHadFailure);
	StructuredContent->SetBoolField(TEXT("allowHighRisk"), bAllowHighRisk);
	StructuredContent->SetBoolField(TEXT("allowCritical"), bAllowCritical);
	StructuredContent->SetNumberField(TEXT("totalSteps"), StepsArray->Num());
	StructuredContent->SetNumberField(TEXT("plannedSteps"), PlannedSteps);
	StructuredContent->SetNumberField(TEXT("executedSteps"), ExecutedSteps);
	StructuredContent->SetNumberField(TEXT("skippedSteps"), SkippedSteps);
	StructuredContent->SetNumberField(TEXT("failedStepIndex"), FailedStepIndex);
	StructuredContent->SetBoolField(TEXT("memoryWriteAttempted"), bMemoryWriteAttempted);
	StructuredContent->SetStringField(TEXT("memoryKey"), MemoryKey);
	if (bMemoryWriteAttempted)
	{
		StructuredContent->SetBoolField(TEXT("memoryWriteSucceeded"), !MemoryResult.bIsError);
		StructuredContent->SetStringField(TEXT("memoryWriteText"), ClipText(MemoryResult.Text, 1000));
	}
	StructuredContent->SetArrayField(TEXT("steps"), StepResults);

	FString Text;
	if (bDryRun)
	{
		Text = FString::Printf(TEXT("Workflow '%s' dry run planned %d step(s). Set dryRun=false to execute."), *WorkflowName, PlannedSteps);
	}
	else if (bHadFailure)
	{
		Text = FString::Printf(TEXT("Workflow '%s' paused after %d/%d executed step(s). Failed step index: %d. State saved to '%s' when writeMemory=true."), *WorkflowName, ExecutedSteps, StepsArray->Num(), FailedStepIndex, *MemoryKey);
	}
	else
	{
		Text = FString::Printf(TEXT("Workflow '%s' completed %d/%d executed step(s)."), *WorkflowName, ExecutedSteps, StepsArray->Num());
	}

	if (bMemoryWriteAttempted && MemoryResult.bIsError)
	{
		Text += FString::Printf(TEXT(" Memory write failed: %s"), *MemoryResult.Text);
	}

	return MakeExecutionResult(Text, StructuredContent, bHadFailure);
}
