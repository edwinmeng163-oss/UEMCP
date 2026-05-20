#include "Providers/ChatCompatQuirks/IChatCompatQuirkHandler.h"

#include "Dom/JsonObject.h"
#include "UnrealMcpSettings.h"

namespace UnrealMcp::ChatCompat
{
	namespace ChatCompatQuirksPrivate
	{
		FString NormalizeChatCompatPresetId(const FString& PresetId)
		{
			return PresetId.TrimStartAndEnd().ToLower();
		}

		class FNoOpChatCompatQuirks final : public IChatCompatQuirkHandler
		{
		};

		class FKimiQuirks final : public IChatCompatQuirkHandler
		{
		public:
			virtual void OnDelta(const TSharedPtr<FJsonObject>& Delta, FString& InOutAccumulatedReasoningContent) const override
			{
				if (!Delta.IsValid())
				{
					return;
				}

				FString ReasoningContent;
				if (Delta->TryGetStringField(TEXT("reasoning_content"), ReasoningContent) && !ReasoningContent.IsEmpty())
				{
					InOutAccumulatedReasoningContent += ReasoningContent;
				}
			}

			virtual void OnBuildAssistantToolCallsMessage(
				const TSharedRef<FJsonObject>& AssistantMessage,
				const FString& AccumulatedReasoningContent) const override
			{
				if (!AccumulatedReasoningContent.IsEmpty())
				{
					AssistantMessage->SetStringField(TEXT("reasoning_content"), AccumulatedReasoningContent);
				}
			}
		};
	}

	TUniquePtr<IChatCompatQuirkHandler> MakeChatCompatQuirkHandler(const FAiProviderConfig& Config)
	{
		const FString NormalizedPresetId = ChatCompatQuirksPrivate::NormalizeChatCompatPresetId(Config.PresetId);
		if (NormalizedPresetId == TEXT("kimi"))
		{
			return MakeUnique<ChatCompatQuirksPrivate::FKimiQuirks>();
		}

		return MakeUnique<ChatCompatQuirksPrivate::FNoOpChatCompatQuirks>();
	}
}
