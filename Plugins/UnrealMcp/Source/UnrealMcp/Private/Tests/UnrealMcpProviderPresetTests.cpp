#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UnrealMcpProviderPresets.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpProviderPresetRegistryTest,
	"UnrealMcp.ProviderPresets.RegistryAndApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpProviderPresetRegistryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace UnrealMcp::ProviderPresets;

	const TArray<FProviderPreset>& Presets = GetAllProviderPresets();
	TestEqual(TEXT("Registry contains exactly 10 presets."), Presets.Num(), 10);

	const TArray<FString> ExpectedIds =
	{
		TEXT("custom-openai-chat"),
		TEXT("openai-responses"),
		TEXT("anthropic-claude"),
		TEXT("kimi"),
		TEXT("glm"),
		TEXT("deepseek"),
		TEXT("qwen"),
		TEXT("ollama-local"),
		TEXT("codex-cli"),
		TEXT("codex-desktop"),
	};

	TSet<FString> SeenIds;
	for (const FString& ExpectedId : ExpectedIds)
	{
		TestTrue(FString::Printf(TEXT("Registry includes preset id '%s'."), *ExpectedId), FindProviderPresetById(ExpectedId) != nullptr);
	}

	TSet<FString> ChatCompatIds;
	for (const FProviderPreset& Preset : Presets)
	{
		TestFalse(TEXT("Preset id is non-empty."), Preset.Id.IsEmpty());
		TestFalse(FString::Printf(TEXT("Preset id '%s' is unique."), *Preset.Id), SeenIds.Contains(Preset.Id));
		SeenIds.Add(Preset.Id);

		const uint8 KindValue = static_cast<uint8>(Preset.Kind);
		TestTrue(TEXT("Preset kind is one of the existing provider kinds."), KindValue <= static_cast<uint8>(EAiProviderKind::CodexAppServer));

		if (Preset.Kind == EAiProviderKind::OpenAiChatCompat)
		{
			ChatCompatIds.Add(Preset.Id);
		}

		const TArray<FString> PresetStrings =
		{
			Preset.Id,
			Preset.DisplayName,
			Preset.BaseUrl,
			Preset.DefaultModel,
			Preset.DefaultReasoningEffort,
			Preset.CodexBinaryPath,
			Preset.CodexExtraArgs,
		};
		for (const FString& Value : PresetStrings)
		{
			TestFalse(TEXT("Preset strings do not contain bearer tokens."), Value.Contains(TEXT("bearer"), ESearchCase::IgnoreCase));
			TestFalse(TEXT("Preset strings do not contain secret markers."), Value.Contains(TEXT("secret"), ESearchCase::IgnoreCase));
			TestFalse(TEXT("Preset strings do not contain token markers."), Value.Contains(TEXT("token"), ESearchCase::IgnoreCase));
			TestFalse(TEXT("Preset strings do not contain OpenAI key prefixes."), Value.Contains(TEXT("sk-"), ESearchCase::IgnoreCase));
		}
	}
	TestEqual(TEXT("All preset ids are unique."), SeenIds.Num(), Presets.Num());

	const TArray<FString> ExpectedChatCompatIds =
	{
		TEXT("custom-openai-chat"),
		TEXT("kimi"),
		TEXT("glm"),
		TEXT("deepseek"),
		TEXT("qwen"),
		TEXT("ollama-local"),
	};
	TestEqual(TEXT("OpenAiChatCompat preset count matches expected vendors."), ChatCompatIds.Num(), ExpectedChatCompatIds.Num());
	for (const FString& ExpectedId : ExpectedChatCompatIds)
	{
		TestTrue(FString::Printf(TEXT("OpenAiChatCompat includes '%s'."), *ExpectedId), ChatCompatIds.Contains(ExpectedId));
	}

	const FProviderPreset* CodexDesktopPreset = FindProviderPresetById(TEXT("codex-desktop"));
	TestTrue(TEXT("Codex Desktop preset exists."), CodexDesktopPreset != nullptr);
	if (CodexDesktopPreset)
	{
		TestEqual(TEXT("Codex Desktop uses UE bridge WebSocket URL."), CodexDesktopPreset->BaseUrl, FString(TEXT("ws://127.0.0.1:8766/uevolve")));
		TestTrue(TEXT("Codex Desktop URL starts with ws://."), CodexDesktopPreset->BaseUrl.StartsWith(TEXT("ws://")));
	}

	const FProviderPreset* CodexCliPreset = FindProviderPresetById(TEXT("codex-cli"));
	TestTrue(TEXT("Codex CLI preset exists."), CodexCliPreset != nullptr);
	if (CodexCliPreset)
	{
		TestTrue(TEXT("Codex CLI preset leaves Model empty."), CodexCliPreset->DefaultModel.IsEmpty());
		TestTrue(TEXT("Codex CLI preset carries model arg in CodexExtraArgs."), CodexCliPreset->CodexExtraArgs.Contains(TEXT("-m gpt-5.5")));
		TestTrue(TEXT("Codex CLI preset carries reasoning arg in CodexExtraArgs."), CodexCliPreset->CodexExtraArgs.Contains(TEXT("-r xhigh")));
	}

	const FProviderPreset* KimiPreset = FindProviderPresetById(TEXT("kimi"));
	TestTrue(TEXT("Kimi preset exists."), KimiPreset != nullptr);
	if (KimiPreset)
	{
		FAiProviderConfig Config;
		Config.Id = TEXT("stable-id");
		Config.ApiKey = TEXT("keep-this-key");
		Config.DisplayName = TEXT("Old name");
		Config.BaseUrl = TEXT("https://old.example/v1/chat/completions");
		Config.Model = TEXT("old-model");
		Config.ReasoningEffort = TEXT("old-effort");

		ApplyProviderPreset(Config, *KimiPreset, EProviderPresetApplyMode::ExplicitPresetSelection);

		TestEqual(TEXT("Explicit preset preserves ApiKey."), Config.ApiKey, FString(TEXT("keep-this-key")));
		TestEqual(TEXT("Explicit preset preserves non-empty provider Id."), Config.Id, FString(TEXT("stable-id")));
		TestEqual(TEXT("Explicit preset sets PresetId."), Config.PresetId, FString(TEXT("kimi")));
		TestEqual(TEXT("Explicit preset sets Kind."), static_cast<uint8>(Config.Kind), static_cast<uint8>(EAiProviderKind::OpenAiChatCompat));
		TestEqual(TEXT("Explicit preset overwrites DisplayName."), Config.DisplayName, KimiPreset->DisplayName);
		TestEqual(TEXT("Explicit preset overwrites BaseUrl."), Config.BaseUrl, KimiPreset->BaseUrl);
		TestEqual(TEXT("Explicit preset overwrites Model."), Config.Model, KimiPreset->DefaultModel);
		TestEqual(TEXT("Explicit preset overwrites ReasoningEffort."), Config.ReasoningEffort, KimiPreset->DefaultReasoningEffort);
	}

	TestTrue(TEXT("Empty PresetId is a no-match."), FindProviderPresetById(TEXT("")) == nullptr);
	TestTrue(TEXT("Unknown PresetId is a no-match."), FindProviderPresetById(TEXT("unknown-provider")) == nullptr);

	FAiProviderConfig ManualConfig;
	ManualConfig.Id = TEXT("manual-provider");
	ManualConfig.PresetId = TEXT("");
	ManualConfig.Kind = EAiProviderKind::OpenAiChatCompat;
	ManualConfig.BaseUrl = TEXT("https://manual.example/v1/chat/completions");
	ManualConfig.Model = TEXT("manual-model");
	ManualConfig.ApiKey = TEXT("manual-key");

	const FAiProviderConfig BeforeNoPreset = ManualConfig;
	if (const FProviderPreset* NoPreset = FindProviderPresetById(ManualConfig.PresetId))
	{
		ApplyProviderPreset(ManualConfig, *NoPreset, EProviderPresetApplyMode::ExplicitPresetSelection);
	}
	TestEqual(TEXT("Applying no preset preserves Id."), ManualConfig.Id, BeforeNoPreset.Id);
	TestEqual(TEXT("Applying no preset preserves empty PresetId."), ManualConfig.PresetId, BeforeNoPreset.PresetId);
	TestEqual(TEXT("Applying no preset preserves BaseUrl."), ManualConfig.BaseUrl, BeforeNoPreset.BaseUrl);
	TestEqual(TEXT("Applying no preset preserves Model."), ManualConfig.Model, BeforeNoPreset.Model);
	TestEqual(TEXT("Applying no preset preserves ApiKey."), ManualConfig.ApiKey, BeforeNoPreset.ApiKey);

	return true;
}

#endif
