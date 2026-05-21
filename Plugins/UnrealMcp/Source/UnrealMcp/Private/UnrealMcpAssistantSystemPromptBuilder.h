#pragma once

#include "CoreMinimal.h"

class UUnrealMcpSettings;

namespace UnrealMcp
{
	// Provider transport hint: output is currently identical for every transport, but
	// the hint keeps future provider-specific prompt tweaks centralized here.
	enum class EAssistantSystemPromptTransport : uint8
	{
		OpenAiResponses,
		OpenAiChatCompat,
		AnthropicMessages,
	};

	struct FAssistantSystemPromptInput
	{
		// User-set optional extra prompt (UUnrealMcpSettings::AssistantSystemPrompt).
		FString UserAssistantSystemPrompt;

		// Optional provider-owned steering instructions. Providers that deliver
		// steering separately should leave this empty.
		TArray<FString> SteerInstructions;

		EAssistantSystemPromptTransport Transport = EAssistantSystemPromptTransport::OpenAiResponses;
	};

	// Returns the full non-empty assistant system prompt: base identity, Reform C
	// safety rules, optional user prompt, and optional turn steering.
	FString BuildAssistantSystemPrompt(const FAssistantSystemPromptInput& Input);

	// Returns only the Reform C safety block embedded by BuildAssistantSystemPrompt.
	FString GetAssistantSafetyRulesBlock();
}
