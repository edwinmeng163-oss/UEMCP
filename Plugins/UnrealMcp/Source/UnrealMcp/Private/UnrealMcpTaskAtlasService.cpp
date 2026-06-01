#include "UnrealMcpTaskAtlasService.h"

#include "UnrealMcpCallToolPolicy.h"
#include "UnrealMcpCapturedArgsStore.h"
#include "UnrealMcpHashUtils.h"
#include "UnrealMcpToolRegistry.h"
#include "UnrealMcpTaskAtlasTools.h"
#include "UnrealMcpUserToolLock.h"
#include "UnrealMcpUserToolRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/CriticalSection.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if PLATFORM_MAC || PLATFORM_LINUX
#include <limits.h>
#include <stdlib.h>
#endif

namespace UnrealMcp::TaskAtlasComposite
{
	bool BuildCompositeUserToolFiles(
		const FString& ToolId,
		const FString& Title,
		const FString& Description,
		const FString& TaskId,
		const FString& ReplayEligibility,
		const FString& ReplayUnavailableReason,
		const TArray<FString>& CriticalPath,
		const TArray<TSharedPtr<FJsonValue>>& StepRefs,
		const TSet<FString>& VisibleCoreToolNames,
		FString& OutToolName,
		FString& OutMainPy,
		FString& OutMainPySha256,
		FString& OutToolJson,
		TSharedPtr<FJsonObject>& OutSmokeArgs,
		TArray<FString>& OutStepTools,
		FString& OutFailureReason);
}

namespace UnrealMcp::TaskAtlasService
{
	// Service functions return result structs. They do not throw or use checkf for user/data errors.
	// Report errors through Outcome, ErrorCode, ErrorMessage, RejectedReasons, FailureDiagnosticPath, and structured JSON fields.

	namespace
	{
		static FCriticalSection GTaskAtlasServiceMutationLock;

		struct FLoadedTask
		{
			FTaskAtlasModel Model;
			FString Label;
			FString Description;
			FString ReplayEligibility;
			FString ReplayUnavailableReason;
			FString TaskPath;
		};

#if WITH_DEV_AUTOMATION_TESTS
		FString GTaskAtlasServiceMadeToolsRootForTests;
		FString GTaskAtlasServiceSavedRootForTests;
		bool bTaskAtlasServiceCorruptNextStagingShaForTests = false;
		bool bTaskAtlasServiceRejectNextTargetReloadForTests = false;
#endif

		FString TaskAtlasServiceNormalizeAbsolutePath(const FString& Path)
		{
			FString Result = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(Result);
			FPaths::CollapseRelativeDirectories(Result);
			Result.RemoveFromEnd(TEXT("/"));
			return Result;
		}

		bool TaskAtlasServiceTryResolveRealPath(const FString& Path, FString& OutPath)
		{
			OutPath.Reset();
#if PLATFORM_MAC || PLATFORM_LINUX
			char Buffer[PATH_MAX + 1] = {};
			FTCHARToUTF8 Converter(*Path);
			if (realpath(Converter.Get(), Buffer) == nullptr)
			{
				return false;
			}
			FUTF8ToTCHAR TargetConverter(Buffer);
			OutPath = TaskAtlasServiceNormalizeAbsolutePath(FString(TargetConverter.Length(), TargetConverter.Get()));
			return true;
#else
			(void)Path;
			return false;
#endif
		}

		FString TaskAtlasServiceCanonicalExistingPath(const FString& Path)
		{
			FString Resolved;
			if (TaskAtlasServiceTryResolveRealPath(Path, Resolved))
			{
				return Resolved;
			}
			return TaskAtlasServiceNormalizeAbsolutePath(Path);
		}

		bool TaskAtlasServicePathEqualsOrChild(const FString& ChildPath, const FString& RootPath)
		{
			FString Child = TaskAtlasServiceNormalizeAbsolutePath(ChildPath);
			FString Root = TaskAtlasServiceNormalizeAbsolutePath(RootPath);
#if PLATFORM_WINDOWS || PLATFORM_MAC
			Child = Child.ToLower();
			Root = Root.ToLower();
#endif
			return Child == Root || Child.StartsWith(Root + TEXT("/"), ESearchCase::CaseSensitive);
		}

		bool TaskAtlasServiceIsSafeDirectoryName(const FString& DirectoryName)
		{
			return !DirectoryName.IsEmpty()
				&& !DirectoryName.Contains(TEXT("/"), ESearchCase::CaseSensitive)
				&& !DirectoryName.Contains(TEXT("\\"), ESearchCase::CaseSensitive)
				&& !DirectoryName.Contains(TEXT(".."), ESearchCase::CaseSensitive)
				&& !DirectoryName.Contains(TEXT(":"), ESearchCase::CaseSensitive)
				&& FPaths::IsRelative(DirectoryName);
		}

		bool TaskAtlasServiceIsSymlink(const FString& Path)
		{
			return FPlatformFileManager::Get().GetPlatformFile().IsSymlink(*Path) == ESymlinkResult::Symlink;
		}

		const TCHAR* TaskAtlasServiceDecisionToString(ECallToolDecision Decision)
		{
			switch (Decision)
			{
			case ECallToolDecision::Allow:
				return TEXT("allow");
			case ECallToolDecision::ForceDryRun:
				return TEXT("force_dry_run");
			case ECallToolDecision::Deny:
			default:
				return TEXT("deny");
			}
		}

		FCallToolTargetFacts TaskAtlasServiceMakePolicyFacts(const FString& RawToolName)
		{
			const FString ToolName = RawToolName.TrimStartAndEnd();
			FCallToolTargetFacts Facts;

			if (ToolName.StartsWith(TEXT("user."), ESearchCase::CaseSensitive))
			{
				Facts.bVisible = true;
				Facts.SourceKind = Extension::ESourceKind::UserRegistry;
				Facts.Depth = 0;
				return Facts;
			}

			if (const FToolRegistryEntry* Entry = FindToolRegistryEntry(ToolName))
			{
				Facts.bVisible = Entry->Exposure == EToolExposure::Visible;
			}
			Facts.SourceKind = ResolveToolSourceKind(ToolName);
			const FToolPolicy Policy = GetToolPolicy(ToolName);
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

		FString TaskAtlasServiceCaptureSummary(const FString& CaptureStatus, const FString& FallbackSummary)
		{
			const FString Summary = FallbackSummary.TrimStartAndEnd().ToLower();
			if (Summary == TEXT("captured")
				|| Summary == TEXT("missing")
				|| Summary == TEXT("redacted")
				|| Summary == TEXT("too_large")
				|| Summary == TEXT("blocked_by_policy"))
			{
				return Summary;
			}

			const FString Status = CaptureStatus.TrimStartAndEnd().ToLower();
			if (Status == TEXT("captured")
				|| Status == TEXT("redacted")
				|| Status == TEXT("too_large")
				|| Status == TEXT("blocked_by_policy"))
			{
				return Status;
			}
			return TEXT("missing");
		}

		bool TaskAtlasServiceHasCapturedArgs(const FString& CaptureStatus)
		{
			const FString Status = CaptureStatus.TrimStartAndEnd().ToLower();
			return Status == TEXT("captured") || Status == TEXT("redacted");
		}

		bool TaskAtlasServiceLoadJsonObject(const FString& Path, TSharedPtr<FJsonObject>& OutObject)
		{
			FString JsonText;
			if (!FFileHelper::LoadFileToString(JsonText, *Path))
			{
				return false;
			}
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
			return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
		}

		bool TaskAtlasServiceReadRequiredString(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, FString& OutValue)
		{
			OutValue.Reset();
			if (!Object.IsValid() || !Object->TryGetStringField(FieldName, OutValue))
			{
				return false;
			}
			OutValue = OutValue.TrimStartAndEnd();
			return !OutValue.IsEmpty();
		}

		FDateTime TaskAtlasServiceReadCreatedUtc(const TSharedPtr<FJsonObject>& Object, const FString& ToolJsonPath)
		{
			FString CreatedUtcText;
			FDateTime CreatedUtc;
			if (Object.IsValid()
				&& Object->TryGetStringField(TEXT("createdUtc"), CreatedUtcText)
				&& FDateTime::ParseIso8601(*CreatedUtcText.TrimStartAndEnd(), CreatedUtc))
			{
				return CreatedUtc;
			}

			const FFileStatData Stat = IFileManager::Get().GetStatData(*ToolJsonPath);
			return Stat.bIsValid ? Stat.ModificationTime : FDateTime();
		}

		bool TaskAtlasServiceHasFailureMarker(const FString& Directory)
		{
			static const TCHAR* MarkerNames[] = {
				TEXT("_failure.marker"),
				TEXT("failure.marker"),
				TEXT("_failure.json"),
				TEXT("failure.json")
			};
			for (const TCHAR* MarkerName : MarkerNames)
			{
				if (FPaths::FileExists(FPaths::Combine(Directory, MarkerName)))
				{
					return true;
				}
			}
			return false;
		}

		FString TaskAtlasServiceReadFailureDiagnosticPath(const TSharedPtr<FJsonObject>& Object)
		{
			FString Path;
			if (!Object.IsValid() || !Object->TryGetStringField(TEXT("failureDiagnosticPath"), Path))
			{
				return FString();
			}
			Path = Path.TrimStartAndEnd();
			if (Path.IsEmpty())
			{
				return FString();
			}
			Path = FPaths::IsRelative(Path)
				? FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), Path))
				: FPaths::ConvertRelativePathToFull(Path);
			Path = TaskAtlasServiceNormalizeAbsolutePath(Path);
			return FPaths::FileExists(Path) ? Path : FString();
		}

		FString TaskAtlasServiceMadeToolsRootDir()
		{
#if WITH_DEV_AUTOMATION_TESTS
			if (!GTaskAtlasServiceMadeToolsRootForTests.IsEmpty())
			{
				return TaskAtlasServiceNormalizeAbsolutePath(GTaskAtlasServiceMadeToolsRootForTests);
			}
#endif
			UserRegistry::InitializeUserToolRegistry();
			return TaskAtlasServiceNormalizeAbsolutePath(UserRegistry::GetUserToolsRootDir());
		}

		FString TaskAtlasServiceSavedRootDir()
		{
#if WITH_DEV_AUTOMATION_TESTS
			if (!GTaskAtlasServiceSavedRootForTests.IsEmpty())
			{
				return TaskAtlasServiceNormalizeAbsolutePath(GTaskAtlasServiceSavedRootForTests);
			}
#endif
			return TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp")));
		}

		FString TaskAtlasServiceTaskRootDir()
		{
			return FPaths::Combine(TaskAtlasServiceSavedRootDir(), TEXT("Tasks"));
		}

		FString TaskAtlasServiceDraftRootDir()
		{
			return FPaths::Combine(TaskAtlasServiceSavedRootDir(), TEXT("TaskAtlasDrafts"));
		}

		FString TaskAtlasServiceFailureRootDir()
		{
			return FPaths::Combine(TaskAtlasServiceSavedRootDir(), TEXT("MakeToolSetFailures"));
		}

		bool TaskAtlasServiceIsSafeTaskId(const FString& TaskId)
		{
			const FString Trimmed = TaskId.TrimStartAndEnd();
			if (Trimmed.IsEmpty() || Trimmed.Len() > 180)
			{
				return false;
			}
			for (const TCHAR Character : Trimmed)
			{
				if (!FChar::IsAlnum(Character) && Character != TEXT('-') && Character != TEXT('_') && Character != TEXT('.'))
				{
					return false;
				}
			}
			return true;
		}

		FString TaskAtlasServiceSanitizeTaskToken(const FString& Value)
		{
			FString Result;
			for (const TCHAR Character : Value.TrimStartAndEnd().ToLower())
			{
				if (FChar::IsAlnum(Character) || Character == TEXT('-') || Character == TEXT('_') || Character == TEXT('.'))
				{
					Result.AppendChar(Character);
				}
				else if (FChar::IsWhitespace(Character))
				{
					Result.AppendChar(TEXT('-'));
				}
			}
			while (Result.Contains(TEXT("--")))
			{
				Result.ReplaceInline(TEXT("--"), TEXT("-"));
			}
			Result.RemoveFromStart(TEXT("-"));
			Result.RemoveFromEnd(TEXT("-"));
			return Result.IsEmpty() ? TEXT("task") : Result.Left(120);
		}

		FString TaskAtlasServiceTrimUnderscores(FString Value)
		{
			while (Value.StartsWith(TEXT("_")))
			{
				Value.RightChopInline(1);
			}
			while (Value.EndsWith(TEXT("_")))
			{
				Value.LeftChopInline(1);
			}
			return Value;
		}

		void TaskAtlasServiceAppendNormalizedUnderscore(FString& OutValue)
		{
			if (!OutValue.IsEmpty() && !OutValue.EndsWith(TEXT("_")))
			{
				OutValue.AppendChar(TEXT('_'));
			}
		}

		FString TaskAtlasServiceSanitizeToolIdPart(const FString& Source)
		{
			FString Slug;
			for (const TCHAR Ch : Source)
			{
				if (Ch >= 'A' && Ch <= 'Z')
				{
					Slug.AppendChar(static_cast<TCHAR>(Ch + ('a' - 'A')));
				}
				else if ((Ch >= 'a' && Ch <= 'z') || (Ch >= '0' && Ch <= '9'))
				{
					Slug.AppendChar(Ch);
				}
				else
				{
					TaskAtlasServiceAppendNormalizedUnderscore(Slug);
				}
			}
			return TaskAtlasServiceTrimUnderscores(Slug);
		}

		// duplicated from STaskAtlasWindow.cpp; chunk 6 will consolidate.
		FString TaskAtlasServiceMakeAtlasToolId(const FString& Label, const FString& TaskId)
		{
			FString ToolId = TaskAtlasServiceSanitizeToolIdPart(Label);
			if (ToolId.IsEmpty())
			{
				ToolId = TaskAtlasServiceSanitizeToolIdPart(TaskId);
			}
			if (ToolId.IsEmpty())
			{
				ToolId = TEXT("workflow");
			}
			ToolId = FString::Printf(TEXT("atlas_%s"), *ToolId);
			if (ToolId.Len() > 64)
			{
				ToolId = TaskAtlasServiceTrimUnderscores(ToolId.Left(64));
			}
			return ToolId.IsEmpty() ? FString(TEXT("atlas_workflow")) : ToolId;
		}

		TArray<TSharedPtr<FJsonValue>> TaskAtlasServiceMakeStringArray(const TArray<FString>& Values)
		{
			TArray<TSharedPtr<FJsonValue>> JsonValues;
			JsonValues.Reserve(Values.Num());
			for (const FString& Value : Values)
			{
				JsonValues.Add(MakeShared<FJsonValueString>(Value));
			}
			return JsonValues;
		}

		FString TaskAtlasServiceJsonToPrettyString(const TSharedPtr<FJsonObject>& Object)
		{
			if (!Object.IsValid())
			{
				return TEXT("{}");
			}

			FString Output;
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
			FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
			return Output;
		}

		bool TaskAtlasServiceWriteJsonObject(const TSharedPtr<FJsonObject>& Object, const FString& Path)
		{
			if (!Object.IsValid())
			{
				return false;
			}
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			return FFileHelper::SaveStringToFile(
				TaskAtlasServiceJsonToPrettyString(Object),
				*Path,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		FString TaskAtlasServiceEligibilityToString(EEligibility Eligibility)
		{
			switch (Eligibility)
			{
			case EEligibility::PreviewReady:
				return TEXT("preview_ready");
			case EEligibility::Partial:
				return TEXT("partial");
			case EEligibility::Blocked:
				return TEXT("blocked");
			case EEligibility::SkeletonPreCapture:
			default:
				return TEXT("skeleton_pre_capture");
			}
		}

		FString TaskAtlasServiceOutcomeToString(EMakeCompositeOutcome Outcome)
		{
			switch (Outcome)
			{
			case EMakeCompositeOutcome::CompositeWritten:
				return TEXT("CompositeWritten");
			case EMakeCompositeOutcome::DocumentOnly:
				return TEXT("DocumentOnly");
			case EMakeCompositeOutcome::Blocked:
				return TEXT("Blocked");
			case EMakeCompositeOutcome::StagingFailed:
				return TEXT("StagingFailed");
			case EMakeCompositeOutcome::ReloadRejected:
				return TEXT("ReloadRejected");
			case EMakeCompositeOutcome::Skipped:
			default:
				return TEXT("Skipped");
			}
		}

		TSharedPtr<FJsonObject> TaskAtlasServiceEligibilityToJson(const FEligibilityResult& Eligibility)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("eligibility"), TaskAtlasServiceEligibilityToString(Eligibility.Eligibility));
			Object->SetNumberField(TEXT("blockedFirstStep"), Eligibility.BlockedFirstStep);
			Object->SetStringField(TEXT("blockedFirstReason"), Eligibility.BlockedFirstReason);
			Object->SetNumberField(TEXT("allowCount"), Eligibility.AllowCount);
			Object->SetNumberField(TEXT("forceDryRunCount"), Eligibility.ForceDryRunCount);
			Object->SetNumberField(TEXT("denyCount"), Eligibility.DenyCount);
			Object->SetNumberField(TEXT("capturedArgsCount"), Eligibility.CapturedArgsCount);
			Object->SetNumberField(TEXT("totalStepCount"), Eligibility.TotalStepCount);

			TArray<TSharedPtr<FJsonValue>> StepValues;
			StepValues.Reserve(Eligibility.Steps.Num());
			for (const FStepPolicy& Step : Eligibility.Steps)
			{
				TSharedPtr<FJsonObject> StepObject = MakeShared<FJsonObject>();
				StepObject->SetNumberField(TEXT("ordinal"), Step.Ordinal);
				StepObject->SetStringField(TEXT("toolName"), Step.ToolName);
				StepObject->SetStringField(TEXT("policyDecision"), Step.PolicyDecision);
				StepObject->SetStringField(TEXT("denyReason"), Step.DenyReason);
				StepObject->SetBoolField(TEXT("hasCapturedArgs"), Step.bHasCapturedArgs);
				StepObject->SetStringField(TEXT("captureEventId"), Step.CaptureEventId);
				StepObject->SetStringField(TEXT("captureSummary"), Step.CaptureSummary);
				StepObject->SetBoolField(TEXT("forceDryRun"), Step.bForceDryRun);
				StepValues.Add(MakeShared<FJsonValueObject>(StepObject));
			}
			Object->SetArrayField(TEXT("steps"), StepValues);
			return Object;
		}

		TSharedPtr<FJsonObject> TaskAtlasServiceMakeResultContent(const FMakeCompositeResult& Result)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("action"), TEXT("task_atlas_make_composite"));
			Object->SetStringField(TEXT("outcome"), TaskAtlasServiceOutcomeToString(Result.Outcome));
			Object->SetStringField(TEXT("toolName"), Result.ToolName);
			Object->SetStringField(TEXT("generatedDir"), Result.GeneratedDir);
			Object->SetStringField(TEXT("stagingDir"), Result.StagingDir);
			Object->SetStringField(TEXT("documentPath"), Result.DocumentPath);
			Object->SetStringField(TEXT("compositeKind"), Result.CompositeKind);
			Object->SetStringField(TEXT("replayStatus"), Result.ReplayStatus);
			Object->SetArrayField(TEXT("rejectedReasons"), TaskAtlasServiceMakeStringArray(Result.RejectedReasons));
			Object->SetStringField(TEXT("failureDiagnosticPath"), Result.FailureDiagnosticPath);
			Object->SetStringField(TEXT("errorCode"), Result.ErrorCode);
			Object->SetStringField(TEXT("errorMessage"), Result.ErrorMessage);
			Object->SetObjectField(TEXT("eligibility"), TaskAtlasServiceEligibilityToJson(Result.Eligibility));
			return Object;
		}

		TArray<FString> TaskAtlasServiceReadStringArrayField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
		{
			TArray<FString> Values;
			const TArray<TSharedPtr<FJsonValue>>* JsonValues = nullptr;
			if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, JsonValues) || !JsonValues)
			{
				return Values;
			}
			for (const TSharedPtr<FJsonValue>& Value : *JsonValues)
			{
				FString StringValue;
				if (Value.IsValid() && Value->TryGetString(StringValue) && !StringValue.TrimStartAndEnd().IsEmpty())
				{
					Values.Add(StringValue.TrimStartAndEnd());
				}
			}
			return Values;
		}

		FString TaskAtlasServiceGetStringField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
		{
			FString Value;
			if (Object.IsValid())
			{
				Object->TryGetStringField(FieldName, Value);
			}
			return Value.TrimStartAndEnd();
		}

		bool TaskAtlasServiceLoadTaskById(const FString& TaskId, FLoadedTask& OutTask, FString& OutFailureReason)
		{
			OutTask = FLoadedTask();
			OutFailureReason.Reset();

			const FString TrimmedTaskId = TaskId.TrimStartAndEnd();
			if (!TaskAtlasServiceIsSafeTaskId(TrimmedTaskId))
			{
				OutFailureReason = TEXT("Task id is missing or unsafe.");
				return false;
			}

			const FString TaskPath = FPaths::Combine(TaskAtlasServiceTaskRootDir(), TrimmedTaskId + TEXT(".json"));
			TSharedPtr<FJsonObject> TaskJson;
			if (!TaskAtlasServiceLoadJsonObject(TaskPath, TaskJson))
			{
				OutFailureReason = FString::Printf(TEXT("Task '%s' was not found."), *TrimmedTaskId);
				return false;
			}

			OutTask.Model.TaskId = TaskAtlasServiceGetStringField(TaskJson, TEXT("taskId"));
			if (OutTask.Model.TaskId.IsEmpty())
			{
				OutTask.Model.TaskId = TrimmedTaskId;
			}
			OutTask.Label = TaskAtlasServiceGetStringField(TaskJson, TEXT("label"));
			OutTask.Description = TaskAtlasServiceGetStringField(TaskJson, TEXT("aiSummaryText"));
			if (OutTask.Description.IsEmpty())
			{
				OutTask.Description = TaskAtlasServiceGetStringField(TaskJson, TEXT("userIntentText"));
			}
			if (OutTask.Description.IsEmpty())
			{
				OutTask.Description = FString::Printf(TEXT("Composite user tool generated from Task Atlas task %s."), *OutTask.Model.TaskId);
			}
			OutTask.ReplayEligibility = TaskAtlasServiceGetStringField(TaskJson, TEXT("replayEligibility"));
			OutTask.ReplayUnavailableReason = TaskAtlasServiceGetStringField(TaskJson, TEXT("replayUnavailableReason"));
			OutTask.TaskPath = TaskPath;
			OutTask.Model.CriticalPath = TaskAtlasServiceReadStringArrayField(TaskJson, TEXT("criticalPath"));

			const TArray<TSharedPtr<FJsonValue>>* StepRefs = nullptr;
			if (TaskJson->TryGetArrayField(TEXT("stepRefs"), StepRefs) && StepRefs)
			{
				for (const TSharedPtr<FJsonValue>& StepValue : *StepRefs)
				{
					const TSharedPtr<FJsonObject> StepObject = StepValue.IsValid() && StepValue->Type == EJson::Object ? StepValue->AsObject() : nullptr;
					if (!StepObject.IsValid())
					{
						continue;
					}

					FTaskAtlasStepRef Step;
					Step.ToolName = TaskAtlasServiceGetStringField(StepObject, TEXT("tool"));
					Step.EventId = TaskAtlasServiceGetStringField(StepObject, TEXT("eventId"));
					Step.CaptureStatus = TaskAtlasServiceGetStringField(StepObject, TEXT("captureStatus"));
					Step.CaptureRef = TaskAtlasServiceGetStringField(StepObject, TEXT("captureRef"));
					Step.CaptureSummary = TaskAtlasServiceGetStringField(StepObject, TEXT("captureSummary"));
					OutTask.Model.StepRefs.Add(MoveTemp(Step));
				}
			}
			return true;
		}

		TSet<FString> TaskAtlasServiceVisibleCoreToolNames()
		{
			TSet<FString> VisibleTools;
			for (const FToolRegistryEntry& Entry : GetToolRegistryEntries())
			{
				if (Entry.Exposure == EToolExposure::Visible && Entry.Name.StartsWith(TEXT("unreal."), ESearchCase::CaseSensitive))
				{
					VisibleTools.Add(Entry.Name);
				}
			}
			return VisibleTools;
		}

		TArray<TSharedPtr<FJsonValue>> TaskAtlasServiceMakeStepRefValues(const FTaskAtlasModel& Task, const FEligibilityResult& Eligibility)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			Values.Reserve(Task.StepRefs.Num());
			for (int32 Index = 0; Index < Task.StepRefs.Num(); ++Index)
			{
				const FTaskAtlasStepRef& StepRef = Task.StepRefs[Index];
				TSharedPtr<FJsonObject> StepObject = MakeShared<FJsonObject>();
				StepObject->SetNumberField(TEXT("ordinal"), Index);
				StepObject->SetStringField(TEXT("tool"), StepRef.ToolName);
				StepObject->SetStringField(TEXT("eventId"), StepRef.EventId);
				StepObject->SetStringField(TEXT("captureStatus"), StepRef.CaptureStatus);
				if (!StepRef.CaptureRef.TrimStartAndEnd().IsEmpty())
				{
					StepObject->SetStringField(TEXT("captureRef"), StepRef.CaptureRef);
				}
				if (Eligibility.Steps.IsValidIndex(Index))
				{
					StepObject->SetStringField(TEXT("policyClassAtCapture"), Eligibility.Steps[Index].PolicyDecision);
				}
				Values.Add(MakeShared<FJsonValueObject>(StepObject));
			}
			return Values;
		}

		bool TaskAtlasServiceWriteDocumentDraft(
			const FLoadedTask& Task,
			const FEligibilityResult& Eligibility,
			const FString& Kind,
			FString& OutPath,
			FString& OutFailureReason)
		{
			OutPath.Reset();
			OutFailureReason.Reset();

			const FString SafeTaskId = TaskAtlasServiceSanitizeTaskToken(Task.Model.TaskId);
			const FString Path = FPaths::Combine(TaskAtlasServiceDraftRootDir(), FString::Printf(TEXT("%s.%s.md"), *SafeTaskId, *Kind));
			FString Markdown;
			Markdown += FString::Printf(TEXT("# %s\n\n"), *(Task.Label.IsEmpty() ? Task.Model.TaskId : Task.Label));
			Markdown += FString::Printf(TEXT("- taskId: `%s`\n"), *Task.Model.TaskId);
			Markdown += FString::Printf(TEXT("- outcome: `%s`\n"), *Kind);
			if (Eligibility.BlockedFirstStep >= 0)
			{
				Markdown += FString::Printf(
					TEXT("- blocked first step: `%d`, reason `%s`\n"),
					Eligibility.BlockedFirstStep,
					*Eligibility.BlockedFirstReason);
			}
			Markdown += TEXT("\n## Denied Steps\n\n");
			bool bAnyDenied = false;
			for (const FStepPolicy& Step : Eligibility.Steps)
			{
				if (Step.PolicyDecision == TEXT("deny"))
				{
					bAnyDenied = true;
					Markdown += FString::Printf(
						TEXT("- step `%d` `%s`: `%s`\n"),
						Step.Ordinal,
						*Step.ToolName,
						*Step.DenyReason);
				}
			}
			if (!bAnyDenied)
			{
				Markdown += TEXT("- none\n");
			}
			Markdown += TEXT("\nRecommendation: use Distill Skill or To RAG for this task until the workflow has safe captured preview data.\n");

			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			if (!FFileHelper::SaveStringToFile(Markdown, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to write document draft '%s'."), *Path);
				return false;
			}

			OutPath = TaskAtlasServiceNormalizeAbsolutePath(Path);
			return true;
		}

		bool TaskAtlasServiceNormalizeCompositeMetadata(
			const FString& ToolName,
			const FString& MainPySha256,
			const FLoadedTask& Task,
			const FString& CreatedUtc,
			FString& InOutToolJson,
			FString& OutFailureReason)
		{
			OutFailureReason.Reset();
			TSharedPtr<FJsonObject> ToolJson;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InOutToolJson);
			if (!FJsonSerializer::Deserialize(Reader, ToolJson) || !ToolJson.IsValid())
			{
				OutFailureReason = TEXT("Generated tool.json did not parse.");
				return false;
			}

			ToolJson->SetStringField(TEXT("name"), ToolName);
			ToolJson->SetStringField(TEXT("generator"), TEXT("task_atlas_make_composite"));
			ToolJson->SetStringField(TEXT("compositeKind"), TEXT("preview"));
			ToolJson->SetStringField(TEXT("replayStatus"), TEXT("preview_ready"));
			ToolJson->SetStringField(TEXT("replayEligibility"), TEXT("preview_ready"));
			ToolJson->SetStringField(TEXT("sourceTaskId"), Task.Model.TaskId);
			ToolJson->SetStringField(TEXT("taskId"), Task.Model.TaskId);
			ToolJson->SetStringField(TEXT("createdUtc"), CreatedUtc);
			ToolJson->SetStringField(TEXT("pythonHandlerSha256"), MainPySha256);

			InOutToolJson = TaskAtlasServiceJsonToPrettyString(ToolJson);
			return true;
		}

		FString TaskAtlasServiceBuildReadme(const FLoadedTask& Task, const FString& ToolName, const FEligibilityResult& Eligibility)
		{
			FString Text;
			Text += FString::Printf(TEXT("# %s\n\n"), *ToolName);
			Text += TEXT("Generated by Task Atlas MakeComposite.\n\n");
			Text += TEXT("This is preview-only composite glue. It is not real replay, and write-capable steps remain controlled by call_tool policy, including forced dry-run behavior.\n\n");
			Text += FString::Printf(TEXT("- sourceTaskId: `%s`\n"), *Task.Model.TaskId);
			Text += FString::Printf(TEXT("- replayStatus: `%s`\n"), TEXT("preview_ready"));
			Text += FString::Printf(TEXT("- stepCount: `%d`\n"), Eligibility.TotalStepCount);
			Text += TEXT("\nCaptured defaults are sanitized public data only; raw private captured arguments are not written into this generated tool.\n");
			return Text;
		}

		bool TaskAtlasServiceValidateStaging(
			const FString& StagingDir,
			const FString& ToolName,
			const FString& ExpectedSha256,
			TSharedPtr<FJsonObject>& OutDiagnostic,
			FString& OutErrorCode,
			FString& OutErrorMessage)
		{
			OutDiagnostic = MakeShared<FJsonObject>();
			OutDiagnostic->SetStringField(TEXT("stagingDir"), StagingDir);
			OutErrorCode.Reset();
			OutErrorMessage.Reset();

			const FString MainPyPath = FPaths::Combine(StagingDir, TEXT("main.py"));
			const FString ToolJsonPath = FPaths::Combine(StagingDir, TEXT("tool.json"));
			if (!FPaths::FileExists(MainPyPath))
			{
				OutErrorCode = TEXT("staging_missing_main_py");
				OutErrorMessage = TEXT("Staging validation failed: main.py is missing.");
				return false;
			}

			TSharedPtr<FJsonObject> ToolJson;
			if (!TaskAtlasServiceLoadJsonObject(ToolJsonPath, ToolJson))
			{
				OutErrorCode = TEXT("staging_invalid_tool_json");
				OutErrorMessage = TEXT("Staging validation failed: tool.json is missing or invalid.");
				return false;
			}

			static const TCHAR* RequiredStringFields[] = {
				TEXT("name"),
				TEXT("generator"),
				TEXT("compositeKind"),
				TEXT("replayStatus"),
				TEXT("pythonHandlerSha256"),
				TEXT("sourceTaskId"),
				TEXT("createdUtc")
			};
			for (const TCHAR* FieldName : RequiredStringFields)
			{
				FString Value;
				if (!TaskAtlasServiceReadRequiredString(ToolJson, FieldName, Value))
				{
					OutErrorCode = FString::Printf(TEXT("staging_missing_%s"), FieldName);
					OutErrorMessage = FString::Printf(TEXT("Staging validation failed: tool.json missing required field '%s'."), FieldName);
					return false;
				}
			}

			FString DeclaredToolName;
			ToolJson->TryGetStringField(TEXT("name"), DeclaredToolName);
			if (DeclaredToolName != ToolName)
			{
				OutErrorCode = TEXT("staging_tool_name_mismatch");
				OutErrorMessage = TEXT("Staging validation failed: tool.json name does not match the target tool.");
				return false;
			}

			FString Generator;
			ToolJson->TryGetStringField(TEXT("generator"), Generator);
			if (Generator != TEXT("task_atlas_make_composite"))
			{
				OutErrorCode = TEXT("staging_generator_mismatch");
				OutErrorMessage = TEXT("Staging validation failed: tool.json generator is incorrect.");
				return false;
			}

			TArray<uint8> MainPyBytes;
			if (!FFileHelper::LoadFileToArray(MainPyBytes, *MainPyPath))
			{
				OutErrorCode = TEXT("staging_main_py_read_failed");
				OutErrorMessage = TEXT("Staging validation failed: could not read main.py.");
				return false;
			}

			FString DeclaredSha256;
			ToolJson->TryGetStringField(TEXT("pythonHandlerSha256"), DeclaredSha256);
			const FString ActualSha256 = HashUtils::Sha256LowerHex(MainPyBytes).ToLower();
			OutDiagnostic->SetStringField(TEXT("declaredSha256"), DeclaredSha256);
			OutDiagnostic->SetStringField(TEXT("actualSha256"), ActualSha256);
			OutDiagnostic->SetStringField(TEXT("expectedSha256"), ExpectedSha256);
			if (!DeclaredSha256.Equals(ExpectedSha256, ESearchCase::CaseSensitive)
				|| !ActualSha256.Equals(DeclaredSha256, ESearchCase::CaseSensitive))
			{
				OutErrorCode = TEXT("staging_sha_mismatch");
				OutErrorMessage = TEXT("Staging validation failed: pythonHandlerSha256 does not match main.py.");
				return false;
			}

			const TSharedPtr<FJsonObject>* InputSchema = nullptr;
			if (!ToolJson->TryGetObjectField(TEXT("inputSchema"), InputSchema) || !InputSchema || !InputSchema->IsValid())
			{
				OutErrorCode = TEXT("staging_missing_input_schema");
				OutErrorMessage = TEXT("Staging validation failed: tool.json missing inputSchema.");
				return false;
			}
			bool bAdditionalProperties = true;
			(*InputSchema)->TryGetBoolField(TEXT("additionalProperties"), bAdditionalProperties);
			if (bAdditionalProperties)
			{
				OutErrorCode = TEXT("staging_open_input_schema");
				OutErrorMessage = TEXT("Staging validation failed: inputSchema must be closed at the root.");
				return false;
			}

			return true;
		}

		bool TaskAtlasServiceWriteStagingFiles(
			const FString& StagingDir,
			const FString& MainPy,
			const FString& ToolJson,
			const FString& Readme,
			FString& OutFailureReason)
		{
			OutFailureReason.Reset();
			if (!IFileManager::Get().MakeDirectory(*StagingDir, true))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to create staging directory '%s'."), *StagingDir);
				return false;
			}
			if (!FFileHelper::SaveStringToFile(MainPy, *FPaths::Combine(StagingDir, TEXT("main.py")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutFailureReason = TEXT("Failed to write staging main.py.");
				return false;
			}
			if (!FFileHelper::SaveStringToFile(ToolJson, *FPaths::Combine(StagingDir, TEXT("tool.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutFailureReason = TEXT("Failed to write staging tool.json.");
				return false;
			}
			if (!FFileHelper::SaveStringToFile(Readme, *FPaths::Combine(StagingDir, TEXT("README.md")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutFailureReason = TEXT("Failed to write staging README.md.");
				return false;
			}
			return true;
		}

		FString TaskAtlasServiceWriteReloadRejectedDiagnostic(
			const FString& ToolName,
			const FString& TaskId,
			const FString& Eligibility,
			const TArray<FString>& RejectedReasons,
			const FString& StagingDir,
			const FString& GeneratedDir,
			int32 ReloadBeforeCount,
			int32 ReloadAfterCount,
			const FString& TimestampUtc)
		{
			const FString SafeName = TaskAtlasServiceSanitizeToolIdPart(ToolName);
			FString SafeTimestamp = TimestampUtc;
			SafeTimestamp.ReplaceInline(TEXT(":"), TEXT(""));
			SafeTimestamp.ReplaceInline(TEXT("-"), TEXT(""));
			SafeTimestamp.ReplaceInline(TEXT("."), TEXT(""));
			const FString DiagnosticDir = FPaths::Combine(
				TaskAtlasServiceFailureRootDir(),
				FString::Printf(TEXT("%s-%s"), *SafeTimestamp, *SafeName));
			const FString DiagnosticPath = FPaths::Combine(DiagnosticDir, TEXT("diagnostic.json"));

			TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
			Diagnostic->SetStringField(TEXT("toolName"), ToolName);
			Diagnostic->SetStringField(TEXT("taskId"), TaskId);
			Diagnostic->SetStringField(TEXT("outcome"), TEXT("ReloadRejected"));
			Diagnostic->SetStringField(TEXT("eligibility"), Eligibility);
			Diagnostic->SetArrayField(TEXT("rejectedReasons"), TaskAtlasServiceMakeStringArray(RejectedReasons));
			Diagnostic->SetStringField(TEXT("stagingDir"), StagingDir);
			Diagnostic->SetStringField(TEXT("generatedDir"), GeneratedDir);
			Diagnostic->SetNumberField(TEXT("reloadBeforeCount"), ReloadBeforeCount);
			Diagnostic->SetNumberField(TEXT("reloadAfterCount"), ReloadAfterCount);
			Diagnostic->SetStringField(TEXT("timestampUtc"), TimestampUtc);

			return TaskAtlasServiceWriteJsonObject(Diagnostic, DiagnosticPath)
				? TaskAtlasServiceNormalizeAbsolutePath(DiagnosticPath)
				: FString();
		}
	}

	FEligibilityResult ClassifyTask(const FTaskAtlasModel& Task)
	{
		FEligibilityResult Result;

		if (Task.StepRefs.Num() > 0)
		{
			Result.Steps.Reserve(Task.StepRefs.Num());
			for (int32 Index = 0; Index < Task.StepRefs.Num(); ++Index)
			{
				const FTaskAtlasStepRef& Ref = Task.StepRefs[Index];
				FStepPolicy Step;
				Step.Ordinal = Index;
				Step.ToolName = Ref.ToolName.TrimStartAndEnd();
				Step.CaptureEventId = Ref.EventId.TrimStartAndEnd();
				Step.CaptureSummary = TaskAtlasServiceCaptureSummary(Ref.CaptureStatus, Ref.CaptureSummary);
				Step.bHasCapturedArgs = TaskAtlasServiceHasCapturedArgs(Ref.CaptureStatus);

				const FCallToolPolicyResult PolicyResult = ClassifyCallToolTarget_Pure(TaskAtlasServiceMakePolicyFacts(Step.ToolName));
				Step.PolicyDecision = TaskAtlasServiceDecisionToString(PolicyResult.Decision);
				Step.bForceDryRun = PolicyResult.Decision == ECallToolDecision::ForceDryRun;
				if (PolicyResult.Decision == ECallToolDecision::Deny)
				{
					Step.DenyReason = PolicyResult.Reason.IsEmpty() ? TEXT("deny") : PolicyResult.Reason;
				}
				Result.Steps.Add(MoveTemp(Step));
			}
		}
		else
		{
			Result.Steps.Reserve(Task.CriticalPath.Num());
			for (int32 Index = 0; Index < Task.CriticalPath.Num(); ++Index)
			{
				FStepPolicy Step;
				Step.Ordinal = Index;
				Step.ToolName = Task.CriticalPath[Index].TrimStartAndEnd();
				Step.CaptureSummary = TEXT("missing");

				const FCallToolPolicyResult PolicyResult = ClassifyCallToolTarget_Pure(TaskAtlasServiceMakePolicyFacts(Step.ToolName));
				Step.PolicyDecision = TaskAtlasServiceDecisionToString(PolicyResult.Decision);
				Step.bForceDryRun = PolicyResult.Decision == ECallToolDecision::ForceDryRun;
				if (PolicyResult.Decision == ECallToolDecision::Deny)
				{
					Step.DenyReason = PolicyResult.Reason.IsEmpty() ? TEXT("deny") : PolicyResult.Reason;
				}
				Result.Steps.Add(MoveTemp(Step));
			}
		}

		Result.TotalStepCount = Result.Steps.Num();
		for (const FStepPolicy& Step : Result.Steps)
		{
			if (Step.PolicyDecision == TEXT("allow"))
			{
				++Result.AllowCount;
			}
			else if (Step.PolicyDecision == TEXT("force_dry_run"))
			{
				++Result.ForceDryRunCount;
			}
			else if (Step.PolicyDecision == TEXT("deny"))
			{
				++Result.DenyCount;
				if (Result.BlockedFirstStep < 0)
				{
					Result.BlockedFirstStep = Step.Ordinal;
					Result.BlockedFirstReason = Step.DenyReason;
				}
			}

			if (Step.bHasCapturedArgs)
			{
				++Result.CapturedArgsCount;
			}
		}

		if (Result.TotalStepCount == 0)
		{
			Result.Eligibility = EEligibility::SkeletonPreCapture;
		}
		else if (Result.DenyCount > 0)
		{
			Result.Eligibility = EEligibility::Blocked;
		}
		else if (Result.CapturedArgsCount == Result.TotalStepCount)
		{
			Result.Eligibility = EEligibility::PreviewReady;
		}
		else if (Result.CapturedArgsCount > 0)
		{
			Result.Eligibility = EEligibility::Partial;
		}
		else
		{
			Result.Eligibility = EEligibility::SkeletonPreCapture;
		}
		return Result;
	}

	FMakeCompositeResult MakeComposite(const FMakeCompositeRequest& Req)
	{
		check(IsInGameThread());
		FMakeCompositeResult Result;

		FLoadedTask Task;
		FString FailureReason;
		if (!TaskAtlasServiceLoadTaskById(Req.TaskId, Task, FailureReason))
		{
			Result.Outcome = EMakeCompositeOutcome::Skipped;
			Result.ErrorCode = TEXT("task_not_found");
			Result.ErrorMessage = FailureReason;
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		Result.Eligibility = ClassifyTask(Task.Model);
		Result.ReplayStatus = TaskAtlasServiceEligibilityToString(Result.Eligibility.Eligibility);
		Result.CompositeKind = Result.ReplayStatus;

		if (Result.Eligibility.Eligibility == EEligibility::Blocked && !Req.bForceWriteEvenIfBlocked)
		{
			if (TaskAtlasServiceWriteDocumentDraft(Task, Result.Eligibility, TEXT("blocked"), Result.DocumentPath, FailureReason))
			{
				Result.Outcome = EMakeCompositeOutcome::Blocked;
				Result.CompositeKind = TEXT("blocked");
				Result.ReplayStatus = TEXT("blocked");
			}
			else
			{
				Result.Outcome = EMakeCompositeOutcome::StagingFailed;
				Result.ErrorCode = TEXT("document_write_failed");
				Result.ErrorMessage = FailureReason;
			}
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		if (Req.bPreferDocumentOnly
			|| Result.Eligibility.Eligibility == EEligibility::Partial
			|| Result.Eligibility.Eligibility == EEligibility::SkeletonPreCapture)
		{
			const FString Kind = TaskAtlasServiceEligibilityToString(Result.Eligibility.Eligibility);
			if (TaskAtlasServiceWriteDocumentDraft(Task, Result.Eligibility, Kind, Result.DocumentPath, FailureReason))
			{
				Result.Outcome = EMakeCompositeOutcome::DocumentOnly;
				Result.CompositeKind = Kind;
				Result.ReplayStatus = Kind;
			}
			else
			{
				Result.Outcome = EMakeCompositeOutcome::StagingFailed;
				Result.ErrorCode = TEXT("document_write_failed");
				Result.ErrorMessage = FailureReason;
			}
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		const FString ToolId = TaskAtlasServiceMakeAtlasToolId(Task.Label, Task.Model.TaskId);
		const FString NewToolName = FString::Printf(TEXT("user.%s"), *ToolId);
		Result.ToolName = NewToolName;
		Result.CompositeKind = TEXT("preview");
		Result.ReplayStatus = TEXT("preview_ready");

		FScopeLock MutationGuard(&GTaskAtlasServiceMutationLock);

		const FString PyToolsRoot = TaskAtlasServiceMadeToolsRootDir();
		const FString CanonicalPyToolsRoot = TaskAtlasServiceNormalizeAbsolutePath(PyToolsRoot);
		const FString TargetDir = TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(CanonicalPyToolsRoot, ToolId));
		if (!TaskAtlasServicePathEqualsOrChild(TargetDir, CanonicalPyToolsRoot))
		{
			Result.Outcome = EMakeCompositeOutcome::Skipped;
			Result.ErrorCode = TEXT("invalid_name");
			Result.ErrorMessage = TEXT("Refused to write composite outside the user-tool registry root.");
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		if (IFileManager::Get().DirectoryExists(*TargetDir))
		{
			Result.Outcome = EMakeCompositeOutcome::Skipped;
			Result.ErrorCode = TEXT("collision_existing_target");
			Result.ErrorMessage = FString::Printf(TEXT("Composite target directory already exists: %s"), *TargetDir);
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		const FString StagingDir = TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(
			CanonicalPyToolsRoot,
			FString::Printf(TEXT("__staging_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Short))));
		Result.StagingDir = StagingDir;

		FString BuiltToolName;
		FString MainPy;
		FString MainPySha256;
		FString ToolJson;
		TSharedPtr<FJsonObject> SmokeArgs;
		TArray<FString> StepTools;
		const FString CreatedUtc = FDateTime::UtcNow().ToIso8601();
		const TArray<TSharedPtr<FJsonValue>> StepRefValues = TaskAtlasServiceMakeStepRefValues(Task.Model, Result.Eligibility);
		if (!TaskAtlasComposite::BuildCompositeUserToolFiles(
			ToolId,
			Task.Label.IsEmpty() ? ToolId : Task.Label,
			Task.Description,
			Task.Model.TaskId,
			TEXT("preview_ready"),
			FString(),
			Task.Model.CriticalPath,
			StepRefValues,
			TaskAtlasServiceVisibleCoreToolNames(),
			BuiltToolName,
			MainPy,
			MainPySha256,
			ToolJson,
			SmokeArgs,
			StepTools,
			FailureReason))
		{
			Result.Outcome = EMakeCompositeOutcome::StagingFailed;
			Result.ErrorCode = TEXT("composite_build_failed");
			Result.ErrorMessage = FailureReason;
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		if (BuiltToolName != NewToolName)
		{
			Result.Outcome = EMakeCompositeOutcome::StagingFailed;
			Result.ErrorCode = TEXT("generated_name_mismatch");
			Result.ErrorMessage = TEXT("Generated composite tool name did not match the derived target name.");
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}
		(void)SmokeArgs;
		(void)StepTools;

		if (!TaskAtlasServiceNormalizeCompositeMetadata(NewToolName, MainPySha256, Task, CreatedUtc, ToolJson, FailureReason))
		{
			Result.Outcome = EMakeCompositeOutcome::StagingFailed;
			Result.ErrorCode = TEXT("metadata_normalization_failed");
			Result.ErrorMessage = FailureReason;
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		const FString Readme = TaskAtlasServiceBuildReadme(Task, NewToolName, Result.Eligibility);
		if (!TaskAtlasServiceWriteStagingFiles(StagingDir, MainPy, ToolJson, Readme, FailureReason))
		{
			IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
			Result.Outcome = EMakeCompositeOutcome::StagingFailed;
			Result.ErrorCode = TEXT("staging_write_failed");
			Result.ErrorMessage = FailureReason;
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

#if WITH_DEV_AUTOMATION_TESTS
		if (bTaskAtlasServiceCorruptNextStagingShaForTests)
		{
			bTaskAtlasServiceCorruptNextStagingShaForTests = false;
			TSharedPtr<FJsonObject> CorruptToolJson;
			if (TaskAtlasServiceLoadJsonObject(FPaths::Combine(StagingDir, TEXT("tool.json")), CorruptToolJson))
			{
				CorruptToolJson->SetStringField(TEXT("pythonHandlerSha256"), FString::ChrN(64, TCHAR('0')));
				TaskAtlasServiceWriteJsonObject(CorruptToolJson, FPaths::Combine(StagingDir, TEXT("tool.json")));
			}
		}
#endif

		TSharedPtr<FJsonObject> ValidationDiagnostic;
		FString ValidationErrorCode;
		FString ValidationErrorMessage;
		if (!TaskAtlasServiceValidateStaging(StagingDir, NewToolName, MainPySha256, ValidationDiagnostic, ValidationErrorCode, ValidationErrorMessage))
		{
			IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
			Result.Outcome = EMakeCompositeOutcome::StagingFailed;
			Result.ErrorCode = ValidationErrorCode.IsEmpty() ? FString(TEXT("staging_validation_failed")) : ValidationErrorCode;
			Result.ErrorMessage = ValidationErrorMessage;
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			if (Result.StructuredContent.IsValid())
			{
				Result.StructuredContent->SetObjectField(TEXT("diagnostic"), ValidationDiagnostic.IsValid() ? ValidationDiagnostic : MakeShared<FJsonObject>());
			}
			return Result;
		}

		if (!IFileManager::Get().Move(*TargetDir, *StagingDir, false, true))
		{
			IFileManager::Get().DeleteDirectory(*StagingDir, false, true);
			Result.Outcome = EMakeCompositeOutcome::StagingFailed;
			Result.ErrorCode = TEXT("staging_rename_failed");
			Result.ErrorMessage = FString::Printf(TEXT("Failed to rename staging directory '%s' to '%s'."), *StagingDir, *TargetDir);
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			return Result;
		}

		UserRegistry::FReloadResult ReloadResult = UserRegistry::ReloadUserToolRegistry(true);

#if WITH_DEV_AUTOMATION_TESTS
		if (bTaskAtlasServiceRejectNextTargetReloadForTests)
		{
			bTaskAtlasServiceRejectNextTargetReloadForTests = false;
			UserRegistry::FReloadResult::FRejection Rejection;
			Rejection.ToolName = NewToolName;
			Rejection.Reason = TEXT("test_forced_reload_rejection");
			ReloadResult.RejectedTools.Add(MoveTemp(Rejection));
		}
#endif

		bool bTargetRejected = false;
		for (const UserRegistry::FReloadResult::FRejection& Rejection : ReloadResult.RejectedTools)
		{
			if (Rejection.ToolName == NewToolName)
			{
				bTargetRejected = true;
				Result.RejectedReasons.Add(Rejection.Reason);
			}
			else
			{
				Result.RejectedReasons.Add(FString::Printf(TEXT("unrelated:%s:%s"), *Rejection.ToolName, *Rejection.Reason));
			}
		}

		if (bTargetRejected)
		{
			IFileManager::Get().DeleteDirectory(*TargetDir, false, true);
			Result.Outcome = EMakeCompositeOutcome::ReloadRejected;
			Result.GeneratedDir = TargetDir;
			Result.StagingDir = StagingDir;
			Result.ErrorCode = TEXT("reload_rejected");
			Result.ErrorMessage = TEXT("Generated composite was rejected by the user tool registry reload and has been rolled back.");
			Result.FailureDiagnosticPath = TaskAtlasServiceWriteReloadRejectedDiagnostic(
				NewToolName,
				Task.Model.TaskId,
				TEXT("preview_ready"),
				Result.RejectedReasons,
				StagingDir,
				TargetDir,
				ReloadResult.BeforeCount,
				ReloadResult.AfterCount,
				CreatedUtc);
			Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
			if (Result.StructuredContent.IsValid())
			{
				Result.StructuredContent->SetNumberField(TEXT("reloadBeforeCount"), ReloadResult.BeforeCount);
				Result.StructuredContent->SetNumberField(TEXT("reloadAfterCount"), ReloadResult.AfterCount);
			}
			return Result;
		}

		Result.Outcome = EMakeCompositeOutcome::CompositeWritten;
		Result.GeneratedDir = TargetDir;
		Result.StagingDir.Reset();
		Result.StructuredContent = TaskAtlasServiceMakeResultContent(Result);
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->SetNumberField(TEXT("reloadBeforeCount"), ReloadResult.BeforeCount);
			Result.StructuredContent->SetNumberField(TEXT("reloadAfterCount"), ReloadResult.AfterCount);
		}
		return Result;
	}

	FDeleteMadeToolResult DeleteMadeTool(const FString& ToolName)
	{
		(void)ToolName;

		FDeleteMadeToolResult Result;
		Result.Outcome = EDeleteMadeToolOutcome::Refused;
		Result.ErrorCode = TEXT("not_implemented_chunk1_stub");
		Result.ErrorMessage = TEXT("UnrealMcpTaskAtlasService::DeleteMadeTool is a chunk-1 stub.");
		return Result;
	}

	TArray<FMadeToolEntry> ListMadeTools()
	{
		TArray<FMadeToolEntry> Entries;
		const FString RootDir = TaskAtlasServiceMadeToolsRootDir();
		if (!IFileManager::Get().DirectoryExists(*RootDir))
		{
			return Entries;
		}

		const FString CanonicalRootDir = TaskAtlasServiceCanonicalExistingPath(RootDir);
		TArray<FString> DirectoryNames;
		IFileManager::Get().FindFiles(DirectoryNames, *FPaths::Combine(RootDir, TEXT("*")), false, true);
		for (const FString& DirectoryName : DirectoryNames)
		{
			if (!TaskAtlasServiceIsSafeDirectoryName(DirectoryName))
			{
				continue;
			}

			const FString CandidateDir = TaskAtlasServiceNormalizeAbsolutePath(FPaths::Combine(RootDir, DirectoryName));
			if (TaskAtlasServiceIsSymlink(CandidateDir))
			{
				continue;
			}

			const FString CanonicalCandidateDir = TaskAtlasServiceCanonicalExistingPath(CandidateDir);
			if (!TaskAtlasServicePathEqualsOrChild(CanonicalCandidateDir, CanonicalRootDir))
			{
				continue;
			}

			const FString ToolJsonPath = FPaths::Combine(CanonicalCandidateDir, TEXT("tool.json"));
			if (!FPaths::FileExists(ToolJsonPath) || TaskAtlasServiceIsSymlink(ToolJsonPath))
			{
				continue;
			}

			TSharedPtr<FJsonObject> ToolJson;
			if (!TaskAtlasServiceLoadJsonObject(ToolJsonPath, ToolJson))
			{
				continue;
			}

			FString Generator;
			if (!TaskAtlasServiceReadRequiredString(ToolJson, TEXT("generator"), Generator)
				|| Generator != TEXT("task_atlas_make_composite"))
			{
				continue;
			}

			FMadeToolEntry Entry;
			if (!TaskAtlasServiceReadRequiredString(ToolJson, TEXT("name"), Entry.ToolName)
				|| !Entry.ToolName.StartsWith(TEXT("user."), ESearchCase::CaseSensitive)
				|| !TaskAtlasServiceReadRequiredString(ToolJson, TEXT("compositeKind"), Entry.CompositeKind)
				|| !TaskAtlasServiceReadRequiredString(ToolJson, TEXT("replayStatus"), Entry.ReplayStatus)
				|| !TaskAtlasServiceReadRequiredString(ToolJson, TEXT("sourceTaskId"), Entry.SourceTaskId)
				|| !TaskAtlasServiceReadRequiredString(ToolJson, TEXT("pythonHandlerSha256"), Entry.PythonHandlerSha256))
			{
				continue;
			}

			Entry.ScaffoldDir = CanonicalCandidateDir;
			Entry.Generator = Generator;
			Entry.CreatedUtc = TaskAtlasServiceReadCreatedUtc(ToolJson, ToolJsonPath);
			Entry.bHasFailureMarker = TaskAtlasServiceHasFailureMarker(CanonicalCandidateDir);
			Entry.FailureDiagnosticPath = TaskAtlasServiceReadFailureDiagnosticPath(ToolJson);
			{
				UserToolLock::FSharedGuard Guard;
				Entry.bLoadedInUserRegistry = UserRegistry::FindUserTool(Entry.ToolName) != nullptr;
			}
			Entries.Add(MoveTemp(Entry));
		}

		Entries.Sort(
			[](const FMadeToolEntry& Left, const FMadeToolEntry& Right)
			{
				if (Left.CreatedUtc == Right.CreatedUtc)
				{
					return Left.ToolName < Right.ToolName;
				}
				return Left.CreatedUtc > Right.CreatedUtc;
			});
		return Entries;
	}

#if WITH_DEV_AUTOMATION_TESTS
	void SetMadeToolsRootDirForTests(const FString& RootDir)
	{
		GTaskAtlasServiceMadeToolsRootForTests = RootDir;
	}

	void ClearMadeToolsRootDirForTests()
	{
		GTaskAtlasServiceMadeToolsRootForTests.Reset();
	}

	void SetSavedRootDirForTests(const FString& RootDir)
	{
		GTaskAtlasServiceSavedRootForTests = RootDir;
	}

	void ClearSavedRootDirForTests()
	{
		GTaskAtlasServiceSavedRootForTests.Reset();
	}

	void CorruptNextMakeCompositeStagingShaForTests()
	{
		bTaskAtlasServiceCorruptNextStagingShaForTests = true;
	}

	void RejectNextMakeCompositeReloadForTests()
	{
		bTaskAtlasServiceRejectNextTargetReloadForTests = true;
	}
#endif

	FPromoteToRagResult PromoteToRag(const FString& TaskId)
	{
		(void)TaskId;

		FPromoteToRagResult Result;
		Result.Outcome = EPromoteToRagOutcome::Failed;
		Result.ErrorCode = TEXT("not_implemented_chunk1_stub");
		Result.ErrorMessage = TEXT("UnrealMcpTaskAtlasService::PromoteToRag is a chunk-1 stub.");
		return Result;
	}

	FSmokeResult SmokeMadeTool(const FString& ToolName)
	{
		(void)ToolName;

		FSmokeResult Result;
		Result.Outcome = ESmokeOutcome::Refused;
		Result.ErrorCode = TEXT("not_implemented_chunk1_stub");
		Result.ErrorMessage = TEXT("UnrealMcpTaskAtlasService::SmokeMadeTool is a chunk-1 stub.");
		return Result;
	}

	TArray<FUserToolView> IntrospectUserRegistry()
	{
		return TArray<FUserToolView>();
	}
}
