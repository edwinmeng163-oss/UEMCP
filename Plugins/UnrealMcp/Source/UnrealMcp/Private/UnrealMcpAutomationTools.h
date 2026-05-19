#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FUnrealMcpExecutionResult;

namespace UnrealMcp
{
	enum class EUnrealMcpAutomationRunType : uint8
	{
		Automation,
		PieSmoke
	};

	struct FUnrealMcpAutomationResultEvent
	{
		FString EventType;
		FString Message;
		FString Location;
		int32 Frame = INDEX_NONE;
	};

	struct FUnrealMcpAutomationRunState
	{
		FString RunId;
		FString Status = TEXT("queued");
		FString Reason;
		EUnrealMcpAutomationRunType RunType = EUnrealMcpAutomationRunType::Automation;
		FString FullName;
		FString DisplayName;
		FString PrettyName;
		TArray<FString> Flags;
		TArray<FString> Tags;
		FDateTime AcceptedAtUtc;
		FDateTime StartedAtUtc;
		FDateTime EndedAtUtc;
		FDateTime LastHeartbeatUtc;
		FString StaleReason;
		int32 TimeoutSeconds = 120;
		TArray<FUnrealMcpAutomationResultEvent> Results;
		TSharedPtr<FJsonObject> PieReport;
		bool bBestEffortCancelAttempted = false;
	};

	FString LexToString(EUnrealMcpAutomationRunType RunType);
	EUnrealMcpAutomationRunType ParseAutomationRunType(const FString& RunTypeText);
	FString MakeAutomationRunId();
	FString MakeAutomationReportRelativePath(const FString& RunId);
	FString MakeAutomationReportPath(const FString& RunId);
	bool IsAutomationRunActiveStatus(const FString& Status);
	FString GetAutomationRunStaleReason(const FUnrealMcpAutomationRunState& State, const FDateTime& NowUtc);
	bool SaveAutomationRunStateFile(const FUnrealMcpAutomationRunState& State);
	bool LoadAutomationRunStateFromFile(const FString& RunId, FUnrealMcpAutomationRunState& OutState);
	bool TryGetActiveAutomationRunState(FUnrealMcpAutomationRunState& OutState);
	void SetActiveAutomationRunState(const FUnrealMcpAutomationRunState& State);
	FUnrealMcpAutomationRunState GetActiveAutomationRunStateCopy();
	void ClearActiveAutomationRunIfMatching(const FString& RunId);

	bool TryExecuteAutomationTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult);
	void MarkActiveAutomationRunStaleOnShutdown();

#if WITH_DEV_AUTOMATION_TESTS
	void ResetAutomationToolStateForTests();
	void SetAutomationFrameworkStartSuppressedForTests(bool bSuppress);
	void SetActiveAutomationRunForTests(
		const FString& RunId,
		EUnrealMcpAutomationRunType RunType,
		const FString& Status,
		const FDateTime& StartedAtUtc,
		int32 TimeoutSeconds);
	FString WriteAutomationRunStateForTests(
		const FString& RunId,
		const FString& Status,
		const FString& FullName,
		const FString& DisplayName,
		const FDateTime& StartedAtUtc,
		int32 TimeoutSeconds);
	FString WriteLegacyAutomationRunStateForTests(
		const FString& RunId,
		const FString& Status,
		const FString& FullName,
		const FString& DisplayName,
		const FDateTime& StartedAtUtc,
		int32 TimeoutSeconds);
	bool DeleteAutomationRunStateForTests(const FString& RunId);
#endif
}
