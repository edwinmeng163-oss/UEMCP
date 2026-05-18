#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FUnrealMcpExecutionResult;

namespace UnrealMcp
{
	static constexpr double TaskAtlasClusterGapSeconds = 600.0;

	struct FTaskAtlasEventRecord
	{
		FString SessionId;
		FString TimestampUtc;
		FDateTime Timestamp;
		FString EventKind;
		FString ToolName;
		bool bIsError = false;
		FString Content;
		bool bCompletionMarker = false;
	};

	struct FTaskAtlasTaskRecord
	{
		FString TaskId;
		FString Label;
		FString LabelSource;
		double LabelConfidence = 0.0;
		bool bHasLabelConfidence = false;
		FString SessionId;
		TArray<FString> CriticalPath;
		FString Rating = TEXT("unrated");
		bool bPinned = false;
		FString TStartUtc;
		FString TEndUtc;
		int32 EventCount = 0;
		FString UserIntentText;
		FString AiSummaryText;
		bool bSawCompletionMarker = false;
	};

	TArray<FTaskAtlasTaskRecord> ClusterTaskAtlasEventsForTests(const TArray<FTaskAtlasEventRecord>& Events, double GapSeconds = TaskAtlasClusterGapSeconds);
	bool TryExecuteTaskAtlasTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult);
}
