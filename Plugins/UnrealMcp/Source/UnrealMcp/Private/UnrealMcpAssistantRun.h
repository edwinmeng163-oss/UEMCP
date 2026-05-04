#pragma once

#include "CoreMinimal.h"
#include "UnrealMcpModule.h"

namespace UnrealMcp
{
	TSharedRef<IUnrealMcpAssistantHandle, ESPMode::ThreadSafe> CreateAssistantRun(
		const FUnrealMcpModule* Module,
		const FString& UserPrompt,
		const FString& ConversationContext,
		const FString& PreviousResponseId,
		TFunction<void(const FUnrealMcpAssistantEvent&)> OnEvent,
		TFunction<void(const FUnrealMcpAssistantTurnResult&)> OnComplete);
}
