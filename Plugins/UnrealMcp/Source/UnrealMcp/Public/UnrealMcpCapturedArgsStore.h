#pragma once

#include "CoreMinimal.h"
#include "UnrealMcpCaptureRedaction.h"

class FJsonObject;

namespace UnrealMcp::CapturedArgsStore
{
	constexpr int32 kDefaultRetentionMaxAgeDays = 30;
	constexpr int64 kDefaultRetentionMaxTotalBytes = 50LL * 1024LL * 1024LL;

	UNREALMCP_API FString GetCapturedArgsRoot();

	UNREALMCP_API bool WriteCapturedArgs(
		const FString& SessionId,
		const FString& EventId,
		const FString& ToolName,
		const FString& TimestampUtc,
		const UnrealMcp::CaptureRedaction::FRedactionResult& Redacted,
		FString& OutCaptureRef,
		FString& OutSha256);

	UNREALMCP_API bool ReadCapturedArgs(
		const FString& CaptureRef,
		TSharedPtr<FJsonObject>& OutContent,
		FString& OutError);

	UNREALMCP_API bool PruneCapturedArgs(
		int32 MaxAgeDays = kDefaultRetentionMaxAgeDays,
		int64 MaxTotalBytes = kDefaultRetentionMaxTotalBytes,
		FString* OutError = nullptr);
}
