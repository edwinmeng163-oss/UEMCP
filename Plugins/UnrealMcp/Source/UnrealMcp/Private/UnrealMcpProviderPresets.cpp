#include "UnrealMcpProviderPresets.h"

namespace UnrealMcp
{
namespace ProviderPresets
{
	namespace
	{
		bool IsEmptyField(const FString& Value)
		{
			return Value.TrimStartAndEnd().IsEmpty();
		}

		FString DefaultProviderIdForPreset(const FProviderPreset& Preset)
		{
			if (Preset.Id == TEXT("custom-openai-chat"))
			{
				return TEXT("openai-compat");
			}
			if (Preset.Id == TEXT("openai-responses"))
			{
				return TEXT("openai-default");
			}
			if (Preset.Id == TEXT("anthropic-claude"))
			{
				return TEXT("anthropic-default");
			}
			if (Preset.Id == TEXT("codex-desktop"))
			{
				return TEXT("codex-app-server");
			}
			return Preset.Id.IsEmpty() ? FString() : Preset.Id + TEXT("-default");
		}

		bool AssignIfChanged(FString& Target, const FString& Value)
		{
			if (Target.Equals(Value, ESearchCase::CaseSensitive))
			{
				return false;
			}
			Target = Value;
			return true;
		}

		bool FillIfEmpty(FString& Target, const FString& Value)
		{
			if (!IsEmptyField(Target) || Value.IsEmpty())
			{
				return false;
			}
			Target = Value;
			return true;
		}
	}

	const TArray<FProviderPreset>& GetAllProviderPresets()
	{
		static const TArray<FProviderPreset> Presets =
		{
			FProviderPreset{ TEXT("custom-openai-chat"), TEXT("Custom OpenAI-compatible"), EAiProviderKind::OpenAiChatCompat, TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT("") },
			FProviderPreset{ TEXT("openai-responses"), TEXT("OpenAI Responses"), EAiProviderKind::OpenAiResponses, TEXT("https://api.openai.com/v1/responses"), TEXT("gpt-5.1"), TEXT("medium"), TEXT(""), TEXT("") },
			FProviderPreset{ TEXT("anthropic-claude"), TEXT("Anthropic Claude"), EAiProviderKind::AnthropicMessages, TEXT("https://api.anthropic.com/v1/messages"), TEXT("claude-sonnet-4-6"), TEXT("medium"), TEXT(""), TEXT("") },
			FProviderPreset{ TEXT("kimi"), TEXT("Kimi (Moonshot)"), EAiProviderKind::OpenAiChatCompat, TEXT("https://api.moonshot.cn/v1/chat/completions"), TEXT("moonshot-v1-8k"), TEXT(""), TEXT(""), TEXT("") },
			FProviderPreset{ TEXT("glm"), TEXT("GLM (Zhipu)"), EAiProviderKind::OpenAiChatCompat, TEXT("https://open.bigmodel.cn/api/paas/v4/chat/completions"), TEXT("glm-4"), TEXT(""), TEXT(""), TEXT("") },
			FProviderPreset{ TEXT("deepseek"), TEXT("DeepSeek"), EAiProviderKind::OpenAiChatCompat, TEXT("https://api.deepseek.com/v1/chat/completions"), TEXT("deepseek-chat"), TEXT(""), TEXT(""), TEXT("") },
			FProviderPreset{ TEXT("qwen"), TEXT("Qwen (Tongyi)"), EAiProviderKind::OpenAiChatCompat, TEXT("https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"), TEXT("qwen-plus"), TEXT(""), TEXT(""), TEXT("") },
			FProviderPreset{ TEXT("ollama-local"), TEXT("Ollama (local)"), EAiProviderKind::OpenAiChatCompat, TEXT("http://127.0.0.1:11434/v1/chat/completions"), TEXT("llama3.1"), TEXT(""), TEXT(""), TEXT("") },
			FProviderPreset{ TEXT("codex-cli"), TEXT("Codex CLI"), EAiProviderKind::Codex, TEXT(""), TEXT(""), TEXT(""), TEXT("~/codex-orchestrator/bin/codex-agent"), TEXT("-m gpt-5.5 -r xhigh") },
			FProviderPreset{ TEXT("codex-desktop"), TEXT("Codex Desktop / App Server"), EAiProviderKind::CodexAppServer, TEXT("ws://127.0.0.1:8766/uevolve"), TEXT(""), TEXT(""), TEXT(""), TEXT("") },
		};
		return Presets;
	}

	const FProviderPreset* FindProviderPresetById(const FString& PresetId)
	{
		const FString TrimmedPresetId = PresetId.TrimStartAndEnd();
		if (TrimmedPresetId.IsEmpty())
		{
			return nullptr;
		}
		for (const FProviderPreset& Preset : GetAllProviderPresets())
		{
			if (Preset.Id.Equals(TrimmedPresetId, ESearchCase::CaseSensitive))
			{
				return &Preset;
			}
		}
		return nullptr;
	}

	const FProviderPreset* FindDefaultProviderPresetForKind(EAiProviderKind Kind)
	{
		for (const FProviderPreset& Preset : GetAllProviderPresets())
		{
			if (Preset.Kind == Kind)
			{
				return &Preset;
			}
		}
		return nullptr;
	}

	TArray<FString> GetProviderPresetOptionIds()
	{
		TArray<FString> OptionIds;
		OptionIds.Reserve(GetAllProviderPresets().Num());
		for (const FProviderPreset& Preset : GetAllProviderPresets())
		{
			OptionIds.Add(Preset.Id);
		}
		return OptionIds;
	}

	bool ApplyProviderPreset(
		FAiProviderConfig& Config,
		const FProviderPreset& Preset,
		EProviderPresetApplyMode ApplyMode)
	{
		bool bChanged = false;

		if (Config.Id.TrimStartAndEnd().IsEmpty())
		{
			bChanged |= AssignIfChanged(Config.Id, DefaultProviderIdForPreset(Preset));
		}

		if (ApplyMode == EProviderPresetApplyMode::ExplicitPresetSelection)
		{
			bChanged |= AssignIfChanged(Config.PresetId, Preset.Id);
			if (Config.Kind != Preset.Kind)
			{
				Config.Kind = Preset.Kind;
				bChanged = true;
			}
			bChanged |= AssignIfChanged(Config.DisplayName, Preset.DisplayName);
			bChanged |= AssignIfChanged(Config.BaseUrl, Preset.BaseUrl);
			bChanged |= AssignIfChanged(Config.Model, Preset.DefaultModel);
			bChanged |= AssignIfChanged(Config.ReasoningEffort, Preset.DefaultReasoningEffort);
			bChanged |= AssignIfChanged(Config.CodexBinaryPath, Preset.CodexBinaryPath);
			bChanged |= AssignIfChanged(Config.CodexExtraArgs, Preset.CodexExtraArgs);
			return bChanged;
		}

		bChanged |= FillIfEmpty(Config.DisplayName, Preset.DisplayName);
		bChanged |= FillIfEmpty(Config.BaseUrl, Preset.BaseUrl);
		bChanged |= FillIfEmpty(Config.Model, Preset.DefaultModel);
		bChanged |= FillIfEmpty(Config.ReasoningEffort, Preset.DefaultReasoningEffort);
		bChanged |= FillIfEmpty(Config.CodexBinaryPath, Preset.CodexBinaryPath);
		bChanged |= FillIfEmpty(Config.CodexExtraArgs, Preset.CodexExtraArgs);
		return bChanged;
	}
}
}
