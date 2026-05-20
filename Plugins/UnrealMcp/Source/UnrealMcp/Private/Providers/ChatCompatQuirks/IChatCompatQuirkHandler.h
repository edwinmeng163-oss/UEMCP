#pragma once

#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

class FJsonObject;
struct FAiProviderConfig;

namespace UnrealMcp::ChatCompat
{
	class IChatCompatQuirkHandler
	{
	public:
		virtual ~IChatCompatQuirkHandler() = default;

		virtual void OnBuildRequestPayload(const TSharedRef<FJsonObject>& RequestPayload) const
		{
		}

		virtual void OnDelta(const TSharedPtr<FJsonObject>& Delta, FString& InOutAccumulatedReasoningContent) const
		{
		}

		virtual void OnBuildAssistantToolCallsMessage(
			const TSharedRef<FJsonObject>& AssistantMessage,
			const FString& AccumulatedReasoningContent) const
		{
		}
	};

	TUniquePtr<IChatCompatQuirkHandler> MakeChatCompatQuirkHandler(const FAiProviderConfig& Config);
}
