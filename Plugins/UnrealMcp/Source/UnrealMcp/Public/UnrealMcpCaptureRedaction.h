#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealMcp::CaptureRedaction
{
	constexpr int32 kCaptureSchemaVersion = 1;
	constexpr int32 kDefaultMaxValueChars = 4096;
	constexpr int32 kDefaultMaxTotalChars = 65536;

	struct FRedactionResult
	{
		TSharedPtr<FJsonObject> SanitizedArguments;
		FString CaptureStatus;
		TSharedPtr<FJsonObject> RedactionSummaryPublic;
		int32 OriginalSize = 0;
		int32 StoredSize = 0;
	};

	FRedactionResult SanitizeToolArguments_Pure(
		const FString& ToolName,
		const TSharedPtr<FJsonObject>& Arguments,
		int32 MaxValueChars,
		int32 MaxTotalChars);

	void AttachCaptureMetadata(
		const TSharedPtr<FJsonObject>& Payload,
		const FString& ToolName,
		const FJsonObject& Arguments,
		const FString& EventId,
		int32 MaxValueChars = kDefaultMaxValueChars,
		int32 MaxTotalChars = kDefaultMaxTotalChars);
}
