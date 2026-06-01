#include "UnrealMcpTaskAtlasService.h"

#include "UnrealMcpCallToolPolicy.h"
#include "UnrealMcpToolRegistry.h"
#include "UnrealMcpTaskAtlasTools.h"
#include "UnrealMcpUserToolLock.h"
#include "UnrealMcpUserToolRegistry.h"

#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if PLATFORM_MAC || PLATFORM_LINUX
#include <limits.h>
#include <stdlib.h>
#endif

namespace UnrealMcp::TaskAtlasService
{
	// Service functions return result structs. They do not throw or use checkf for user/data errors.
	// Game-thread checks are intentionally deferred until write entrypoints gain real implementations.
	// Report errors through Outcome, ErrorCode, ErrorMessage, RejectedReasons, FailureDiagnosticPath, and structured JSON fields.

	namespace
	{
#if WITH_DEV_AUTOMATION_TESTS
		FString GTaskAtlasServiceMadeToolsRootForTests;
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
		(void)Req;

		FMakeCompositeResult Result;
		Result.Outcome = EMakeCompositeOutcome::Skipped;
		Result.ErrorCode = TEXT("not_implemented_chunk1_stub");
		Result.ErrorMessage = TEXT("UnrealMcpTaskAtlasService::MakeComposite is a chunk-1 stub.");
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
