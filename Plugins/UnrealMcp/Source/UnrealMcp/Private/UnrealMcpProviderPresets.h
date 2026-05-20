#pragma once

#include "CoreMinimal.h"
#include "UnrealMcpSettings.h"

namespace UnrealMcp
{
namespace ProviderPresets
{
	struct FProviderPreset
	{
		FString Id;
		FString DisplayName;
		EAiProviderKind Kind = EAiProviderKind::OpenAiResponses;
		FString BaseUrl;
		FString DefaultModel;
		FString DefaultReasoningEffort;
		FString CodexBinaryPath;
		FString CodexExtraArgs;
	};

	enum class EProviderPresetApplyMode : uint8
	{
		FillEmpty,
		ExplicitPresetSelection,
	};

	const TArray<FProviderPreset>& GetAllProviderPresets();
	const FProviderPreset* FindProviderPresetById(const FString& PresetId);
	const FProviderPreset* FindDefaultProviderPresetForKind(EAiProviderKind Kind);
	TArray<FString> GetProviderPresetOptionIds();

	bool ApplyProviderPreset(
		FAiProviderConfig& Config,
		const FProviderPreset& Preset,
		EProviderPresetApplyMode ApplyMode);
}
}
