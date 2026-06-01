#include "UnrealMcpTaskAtlasService.h"

#include "UnrealMcpTaskAtlasTools.h"

namespace UnrealMcp::TaskAtlasService
{
	// Service functions return result structs. They do not throw or use checkf for user/data errors.
	// Game-thread checks are intentionally deferred until write entrypoints gain real implementations.
	// Report errors through Outcome, ErrorCode, ErrorMessage, RejectedReasons, FailureDiagnosticPath, and structured JSON fields.

	FEligibilityResult ClassifyTask(const FTaskAtlasModel& Task)
	{
		(void)Task;

		FEligibilityResult Result;
		Result.Eligibility = EEligibility::SkeletonPreCapture;
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
		return TArray<FMadeToolEntry>();
	}

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
