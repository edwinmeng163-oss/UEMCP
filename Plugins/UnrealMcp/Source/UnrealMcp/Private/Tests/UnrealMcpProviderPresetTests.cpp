#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealMcpProviderPresets.h"
#include "UnrealMcpSettings.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace UnrealMcp::Providers
{
	const FString& GetCodexSubprocessPathPrefix();
}

namespace UnrealMcpProviderPresetTests
{
	class FProviderBackupFileGuard
	{
	public:
		FProviderBackupFileGuard()
			: BackupPath(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/providers.backup.json")))
			, BackupDirectory(FPaths::GetPath(BackupPath))
			, bHadOriginalFile(FPaths::FileExists(BackupPath))
			, bHadOriginalDirectory(IFileManager::Get().DirectoryExists(*BackupDirectory))
		{
			if (bHadOriginalFile)
			{
				FFileHelper::LoadFileToString(OriginalContent, *BackupPath);
			}
		}

		~FProviderBackupFileGuard()
		{
			if (bHadOriginalFile)
			{
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(BackupPath), true);
				FFileHelper::SaveStringToFile(OriginalContent, *BackupPath);
			}
			else
			{
				IFileManager::Get().Delete(*BackupPath, false, true, true);
				if (!bHadOriginalDirectory)
				{
					IFileManager::Get().DeleteDirectory(*BackupDirectory, false, false);
				}
			}
		}

	private:
		FString BackupPath;
		FString BackupDirectory;
		FString OriginalContent;
		bool bHadOriginalFile = false;
		bool bHadOriginalDirectory = false;
	};

	class FConfigFileGuard
	{
	public:
		explicit FConfigFileGuard(const FString& InConfigPath)
			: ConfigPath(InConfigPath)
			, ConfigDirectory(FPaths::GetPath(ConfigPath))
			, bHadOriginalFile(!ConfigPath.IsEmpty() && FPaths::FileExists(ConfigPath))
			, bHadOriginalDirectory(!ConfigDirectory.IsEmpty() && IFileManager::Get().DirectoryExists(*ConfigDirectory))
		{
			if (bHadOriginalFile)
			{
				FFileHelper::LoadFileToString(OriginalContent, *ConfigPath);
			}
		}

		~FConfigFileGuard()
		{
			if (ConfigPath.IsEmpty())
			{
				return;
			}
			if (bHadOriginalFile)
			{
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath), true);
				FFileHelper::SaveStringToFile(OriginalContent, *ConfigPath);
			}
			else
			{
				IFileManager::Get().Delete(*ConfigPath, false, true, true);
				if (!bHadOriginalDirectory && !ConfigDirectory.IsEmpty())
				{
					IFileManager::Get().DeleteDirectory(*ConfigDirectory, false, false);
				}
			}
		}

	private:
		FString ConfigPath;
		FString ConfigDirectory;
		FString OriginalContent;
		bool bHadOriginalFile = false;
		bool bHadOriginalDirectory = false;
	};

	class FSettingsStateGuard
	{
	public:
		explicit FSettingsStateGuard(UUnrealMcpSettings* InSettings)
			: Settings(InSettings)
		{
			if (Settings)
			{
				Providers = Settings->Providers;
				ActiveProviderId = Settings->ActiveProviderId;
				OpenAIResponsesUrl = Settings->OpenAIResponsesUrl;
				OpenAIApiKey = Settings->OpenAIApiKey;
				OpenAIModel = Settings->OpenAIModel;
				OpenAIReasoningEffort = Settings->OpenAIReasoningEffort;
				AiMaxOutputTokens = Settings->AiMaxOutputTokens;
			}
		}

		~FSettingsStateGuard()
		{
			if (!Settings)
			{
				return;
			}
			Settings->Providers = Providers;
			Settings->ActiveProviderId = ActiveProviderId;
			Settings->OpenAIResponsesUrl = OpenAIResponsesUrl;
			Settings->OpenAIApiKey = OpenAIApiKey;
			Settings->OpenAIModel = OpenAIModel;
			Settings->OpenAIReasoningEffort = OpenAIReasoningEffort;
			Settings->AiMaxOutputTokens = AiMaxOutputTokens;
		}

	private:
		UUnrealMcpSettings* Settings = nullptr;
		TArray<FAiProviderConfig> Providers;
		FString ActiveProviderId;
		FString OpenAIResponsesUrl;
		FString OpenAIApiKey;
		FString OpenAIModel;
		FString OpenAIReasoningEffort;
		int32 AiMaxOutputTokens = 4096;
	};

	bool DispatchProviderChainEvent(
		FAutomationTestBase& Test,
		UUnrealMcpSettings* Settings,
		const FName LeafPropertyName,
		const bool bSetProviderArrayIndex)
	{
		FProperty* ProvidersProperty = FindFProperty<FProperty>(
			UUnrealMcpSettings::StaticClass(),
			GET_MEMBER_NAME_CHECKED(UUnrealMcpSettings, Providers));
		if (!ProvidersProperty)
		{
			Test.AddError(TEXT("Providers property exists."));
			return false;
		}

		FProperty* LeafProperty = FindFProperty<FProperty>(
			FAiProviderConfig::StaticStruct(),
			LeafPropertyName);
		if (!LeafProperty)
		{
			Test.AddError(FString::Printf(TEXT("Leaf property '%s' exists."), *LeafPropertyName.ToString()));
			return false;
		}

		FEditPropertyChain Chain;
		Chain.AddTail(ProvidersProperty);
		Chain.AddTail(LeafProperty);
		if (!Chain.SetActiveMemberPropertyNode(ProvidersProperty))
		{
			Test.AddError(TEXT("Providers property is the active member property."));
			return false;
		}
		if (!Chain.SetActivePropertyNode(LeafProperty))
		{
			Test.AddError(TEXT("Leaf property is the active property."));
			return false;
		}

		FPropertyChangedEvent Event(LeafProperty, EPropertyChangeType::ValueSet);
		Event.SetActiveMemberProperty(ProvidersProperty);
		Event.ObjectIteratorIndex = 0;

		TArray<TMap<FString, int32>> ArrayIndexPerObject;
		ArrayIndexPerObject.AddDefaulted();
		if (bSetProviderArrayIndex)
		{
			ArrayIndexPerObject[0].Add(TEXT("Providers"), 0);
		}
		Event.SetArrayIndexPerObject(ArrayIndexPerObject);

		FPropertyChangedChainEvent ChainEvent(Chain, Event);
		ChainEvent.ObjectIteratorIndex = 0;
		Settings->PostEditChangeChainProperty(ChainEvent);
		return true;
	}
}

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
			TestFalse(TEXT("Preset strings do not contain OpenAI key prefixes."), Value.Contains(FString(TEXT("sk")) + TEXT("-"), ESearchCase::IgnoreCase));
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpProviderPresetsIntlPresetURLsTest,
	"UnrealMcp.ProviderPresets.IntlPresetURLs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpProviderPresetsIntlPresetURLsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	using namespace UnrealMcp::ProviderPresets;

	const FProviderPreset* KimiPreset = FindProviderPresetById(TEXT("kimi"));
	TestNotNull(TEXT("Kimi preset exists."), KimiPreset);
	if (KimiPreset)
	{
		TestEqual(
			TEXT("Kimi preset uses international BaseUrl."),
			KimiPreset->BaseUrl,
			FString(TEXT("https://api.moonshot.ai/v1/chat/completions")));
	}

	const FProviderPreset* GlmPreset = FindProviderPresetById(TEXT("glm"));
	TestNotNull(TEXT("GLM preset exists."), GlmPreset);
	if (GlmPreset)
	{
		TestEqual(
			TEXT("GLM preset uses Z.ai international BaseUrl."),
			GlmPreset->BaseUrl,
			FString(TEXT("https://api.z.ai/api/paas/v4/chat/completions")));
	}

	const FProviderPreset* QwenPreset = FindProviderPresetById(TEXT("qwen"));
	TestNotNull(TEXT("Qwen preset exists."), QwenPreset);
	if (QwenPreset)
	{
		TestEqual(
			TEXT("Qwen preset uses international DashScope BaseUrl."),
			QwenPreset->BaseUrl,
			FString(TEXT("https://dashscope-intl.aliyuncs.com/compatible-mode/v1/chat/completions")));
	}

	for (const FProviderPreset& Preset : GetAllProviderPresets())
	{
		if (!Preset.BaseUrl.IsEmpty())
		{
			TestFalse(
				FString::Printf(TEXT("Preset '%s' BaseUrl does not use a .cn endpoint."), *Preset.Id),
				Preset.BaseUrl.Contains(TEXT(".cn"), ESearchCase::IgnoreCase));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpCodexProviderSubprocessPathPrefixTest,
	"UnrealMcp.CodexProvider.SubprocessPathPrefix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpCodexProviderSubprocessPathPrefixTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FString& Prefix = UnrealMcp::Providers::GetCodexSubprocessPathPrefix();

	TestFalse(TEXT("Prefix is not empty."), Prefix.IsEmpty());
	TestTrue(TEXT("Prefix contains $HOME/.bun/bin."), Prefix.Contains(TEXT("$HOME/.bun/bin")));
	TestTrue(TEXT("Prefix contains $HOME/.local/bin."), Prefix.Contains(TEXT("$HOME/.local/bin")));
	TestTrue(TEXT("Prefix contains $HOME/.cargo/bin."), Prefix.Contains(TEXT("$HOME/.cargo/bin")));
	TestTrue(TEXT("Prefix contains /opt/homebrew/bin."), Prefix.Contains(TEXT("/opt/homebrew/bin")));
	TestTrue(TEXT("Prefix contains /usr/local/bin."), Prefix.Contains(TEXT("/usr/local/bin")));
	TestTrue(TEXT("Prefix ends with :$PATH\"&&${IFS}."), Prefix.EndsWith(TEXT(":$PATH\"&&${IFS}")));
	TestFalse(TEXT("Prefix has no literal ASCII space."), Prefix.Contains(TEXT(" ")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpProviderPresetsLegacyOpenAIMigrationTest,
	"UnrealMcp.ProviderPresets.LegacyOpenAIMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpProviderPresetsLegacyOpenAIMigrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UnrealMcpProviderPresetTests::FProviderBackupFileGuard BackupGuard;
	UUnrealMcpSettings* Settings = GetMutableDefault<UUnrealMcpSettings>();
	TestNotNull(TEXT("Mutable default settings object is available."), Settings);
	if (!Settings)
	{
		return false;
	}

	UnrealMcpProviderPresetTests::FConfigFileGuard ConfigGuard(Settings->GetDefaultConfigFilename());
	UnrealMcpProviderPresetTests::FSettingsStateGuard SettingsGuard(Settings);

	Settings->Providers.Reset();
	Settings->ActiveProviderId.Reset();
	Settings->OpenAIApiKey = TEXT("legacy-test-api-key");
	Settings->OpenAIResponsesUrl = TEXT("https://test.example/v1/responses");
	Settings->OpenAIModel = TEXT("gpt-test");
	Settings->OpenAIReasoningEffort = TEXT("high");

	TestEqual(TEXT("Legacy migration starts with no providers."), Settings->Providers.Num(), 0);

	Settings->ApplyLegacyOpenAIMigration_IfNeeded();

	TestEqual(TEXT("Legacy migration creates one provider."), Settings->Providers.Num(), 1);
	if (Settings->Providers.Num() == 1)
	{
		const FAiProviderConfig& Provider = Settings->Providers[0];
		TestEqual(TEXT("Migrated provider id."), Provider.Id, FString(TEXT("openai-default")));
		TestEqual(TEXT("Migrated provider display name."), Provider.DisplayName, FString(TEXT("OpenAI (migrated)")));
		TestEqual(TEXT("Migrated provider kind."), static_cast<uint8>(Provider.Kind), static_cast<uint8>(EAiProviderKind::OpenAiResponses));
		TestEqual(TEXT("Migrated provider preset id."), Provider.PresetId, FString(TEXT("openai-responses")));
		TestEqual(TEXT("Migrated provider API key."), Provider.ApiKey, FString(TEXT("legacy-test-api-key")));
		TestEqual(TEXT("Migrated provider BaseUrl."), Provider.BaseUrl, FString(TEXT("https://test.example/v1/responses")));
		TestEqual(TEXT("Migrated provider model."), Provider.Model, FString(TEXT("gpt-test")));
		TestEqual(TEXT("Migrated provider reasoning effort."), Provider.ReasoningEffort, FString(TEXT("high")));
	}
	TestEqual(TEXT("Legacy migration selects migrated provider."), Settings->ActiveProviderId, FString(TEXT("openai-default")));

	Settings->ApplyLegacyOpenAIMigration_IfNeeded();

	TestEqual(TEXT("Legacy migration is idempotent."), Settings->Providers.Num(), 1);
	if (Settings->Providers.Num() == 1)
	{
		TestEqual(TEXT("Legacy migration keeps migrated provider id."), Settings->Providers[0].Id, FString(TEXT("openai-default")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpProviderPresetNestedChainEventTest,
	"UnrealMcp.ProviderPresets.NestedChainEvent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpProviderPresetNestedChainEventTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UnrealMcpProviderPresetTests::FProviderBackupFileGuard BackupGuard;
	UUnrealMcpSettings* Settings = NewObject<UUnrealMcpSettings>(GetTransientPackage());
	TestNotNull(TEXT("Transient settings object is created."), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->Providers.Reset();
	FAiProviderConfig& PresetProvider = Settings->Providers.AddDefaulted_GetRef();
	PresetProvider.PresetId = TEXT("kimi");
	PresetProvider.BaseUrl.Empty();
	PresetProvider.Model.Empty();
	PresetProvider.DisplayName.Empty();
	PresetProvider.ApiKey = TEXT("test-key-sentinel-001");

	TestTrue(
		TEXT("PresetId chain event dispatches."),
		UnrealMcpProviderPresetTests::DispatchProviderChainEvent(
			*this,
			Settings,
			GET_MEMBER_NAME_CHECKED(FAiProviderConfig, PresetId),
			true));
	TestEqual(
		TEXT("PresetId chain event fills Kimi BaseUrl."),
		Settings->Providers[0].BaseUrl,
		FString(TEXT("https://api.moonshot.ai/v1/chat/completions")));
	TestEqual(
		TEXT("PresetId chain event fills Kimi Model."),
		Settings->Providers[0].Model,
		FString(TEXT("moonshot-v1-8k")));
	TestEqual(
		TEXT("PresetId chain event fills Kimi DisplayName."),
		Settings->Providers[0].DisplayName,
		FString(TEXT("Kimi (Moonshot)")));
	TestEqual(
		TEXT("PresetId chain event preserves ApiKey."),
		Settings->Providers[0].ApiKey,
		FString(TEXT("test-key-sentinel-001")));

	Settings->Providers.Reset();
	FAiProviderConfig& KindProvider = Settings->Providers.AddDefaulted_GetRef();
	KindProvider.Kind = EAiProviderKind::OpenAiChatCompat;
	KindProvider.Id.Empty();
	KindProvider.DisplayName.Empty();
	KindProvider.BaseUrl.Empty();
	KindProvider.Model.Empty();

	TestTrue(
		TEXT("Kind chain event dispatches."),
		UnrealMcpProviderPresetTests::DispatchProviderChainEvent(
			*this,
			Settings,
			GET_MEMBER_NAME_CHECKED(FAiProviderConfig, Kind),
			true));
	TestEqual(
		TEXT("Kind chain event fills OpenAI-compatible placeholder BaseUrl."),
		Settings->Providers[0].BaseUrl,
		FString(TEXT("https://<vendor-host>/v1/chat/completions")));
	TestEqual(
		TEXT("Kind chain event fills OpenAI-compatible placeholder DisplayName."),
		Settings->Providers[0].DisplayName,
		FString(TEXT("OpenAI-Compatible (edit BaseUrl + Model)")));
	TestEqual(
		TEXT("Kind chain event fills OpenAI-compatible placeholder Id."),
		Settings->Providers[0].Id,
		FString(TEXT("openai-compat")));

	Settings->Providers.Reset();
	FAiProviderConfig& InvalidIndexProvider = Settings->Providers.AddDefaulted_GetRef();
	InvalidIndexProvider.PresetId = TEXT("kimi");
	InvalidIndexProvider.Id = TEXT("invalid-index-id");
	InvalidIndexProvider.DisplayName = TEXT("invalid-index-display");
	InvalidIndexProvider.BaseUrl = TEXT("https://invalid-index.example/v1/chat/completions");
	InvalidIndexProvider.Model = TEXT("invalid-index-model");
	InvalidIndexProvider.ApiKey = TEXT("invalid-index-key");

	TestTrue(
		TEXT("Invalid-index chain event dispatches."),
		UnrealMcpProviderPresetTests::DispatchProviderChainEvent(
			*this,
			Settings,
			GET_MEMBER_NAME_CHECKED(FAiProviderConfig, PresetId),
			false));
	TestEqual(
		TEXT("Invalid-index chain event preserves Id."),
		Settings->Providers[0].Id,
		FString(TEXT("invalid-index-id")));
	TestEqual(
		TEXT("Invalid-index chain event preserves DisplayName."),
		Settings->Providers[0].DisplayName,
		FString(TEXT("invalid-index-display")));
	TestEqual(
		TEXT("Invalid-index chain event preserves BaseUrl."),
		Settings->Providers[0].BaseUrl,
		FString(TEXT("https://invalid-index.example/v1/chat/completions")));
	TestEqual(
		TEXT("Invalid-index chain event preserves Model."),
		Settings->Providers[0].Model,
		FString(TEXT("invalid-index-model")));
	TestEqual(
		TEXT("Invalid-index chain event preserves ApiKey."),
		Settings->Providers[0].ApiKey,
		FString(TEXT("invalid-index-key")));

	Settings->Providers.Reset();
	FAiProviderConfig& ModelProvider = Settings->Providers.AddDefaulted_GetRef();
	ModelProvider.PresetId = TEXT("kimi");
	ModelProvider.Kind = EAiProviderKind::OpenAiChatCompat;
	ModelProvider.Id = TEXT("model-change-id");
	ModelProvider.DisplayName = TEXT("model-change-display");
	ModelProvider.BaseUrl = TEXT("https://model-change.example/v1/chat/completions");
	ModelProvider.Model = TEXT("model-change-model");

	TestTrue(
		TEXT("Model chain event dispatches."),
		UnrealMcpProviderPresetTests::DispatchProviderChainEvent(
			*this,
			Settings,
			GET_MEMBER_NAME_CHECKED(FAiProviderConfig, Model),
			true));
	TestEqual(
		TEXT("Model chain event does not overwrite Id."),
		Settings->Providers[0].Id,
		FString(TEXT("model-change-id")));
	TestEqual(
		TEXT("Model chain event does not overwrite DisplayName."),
		Settings->Providers[0].DisplayName,
		FString(TEXT("model-change-display")));
	TestEqual(
		TEXT("Model chain event does not overwrite BaseUrl."),
		Settings->Providers[0].BaseUrl,
		FString(TEXT("https://model-change.example/v1/chat/completions")));
	TestEqual(
		TEXT("Model chain event does not overwrite Model."),
		Settings->Providers[0].Model,
		FString(TEXT("model-change-model")));

	return true;
}

#endif
