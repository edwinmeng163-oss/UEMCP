#pragma once

#include "CoreMinimal.h"
#include "UnrealMcpModule.h"

namespace UnrealMcp
{
	struct FUnrealMcpAssistantSettingsCache
	{
		bool bEnableAiAssistant = false;
		FString OpenAIResponsesUrl;
		FString OpenAIApiKey;
		FString OpenAIModel;
		FString OpenAIReasoningEffort;
		int32 AiMaxToolRounds = 0;
		int32 AiMaxOutputTokens = 0;
		float AiRequestTimeoutSeconds = 0.0f;
		float AiRequestActivityTimeoutSeconds = 0.0f;
		FString AssistantSystemPrompt;
	};

	TSharedRef<IUnrealMcpAssistantHandle, ESPMode::ThreadSafe> CreateAssistantRun(
		const FUnrealMcpModule* Module,
		const FString& UserPrompt,
		const FString& ConversationContext,
		const FString& PreviousResponseId,
		TFunction<void(const FUnrealMcpAssistantEvent&)> OnEvent,
		TFunction<void(const FUnrealMcpAssistantTurnResult&)> OnComplete);
}
