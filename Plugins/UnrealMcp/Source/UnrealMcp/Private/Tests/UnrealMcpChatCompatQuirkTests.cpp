#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Providers/ChatCompatQuirks/IChatCompatQuirkHandler.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UnrealMcpSettings.h"

namespace
{
	TUniquePtr<UnrealMcp::ChatCompat::IChatCompatQuirkHandler> MakeQuirkHandlerForPreset(const FString& PresetId)
	{
		FAiProviderConfig Config;
		Config.PresetId = PresetId;
		return UnrealMcp::ChatCompat::MakeChatCompatQuirkHandler(Config);
	}

	TSharedPtr<FJsonObject> MakeDeltaWithStringField(const TCHAR* FieldName, const FString& Value)
	{
		TSharedPtr<FJsonObject> Delta = MakeShared<FJsonObject>();
		Delta->SetStringField(FieldName, Value);
		return Delta;
	}

	TSharedRef<FJsonObject> MakeAssistantToolCallsMessage()
	{
		TSharedRef<FJsonObject> AssistantMessage = MakeShared<FJsonObject>();
		AssistantMessage->SetStringField(TEXT("role"), TEXT("assistant"));
		AssistantMessage->SetStringField(TEXT("content"), TEXT("visible assistant text"));

		TSharedPtr<FJsonObject> Function = MakeShared<FJsonObject>();
		Function->SetStringField(TEXT("name"), TEXT("unreal_tool"));
		Function->SetStringField(TEXT("arguments"), TEXT("{}"));

		TSharedPtr<FJsonObject> ToolCall = MakeShared<FJsonObject>();
		ToolCall->SetStringField(TEXT("id"), TEXT("call_0"));
		ToolCall->SetStringField(TEXT("type"), TEXT("function"));
		ToolCall->SetObjectField(TEXT("function"), Function);

		TArray<TSharedPtr<FJsonValue>> ToolCalls;
		ToolCalls.Add(MakeShared<FJsonValueObject>(ToolCall));
		AssistantMessage->SetArrayField(TEXT("tool_calls"), ToolCalls);
		return AssistantMessage;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpChatCompatQuirkHandlersTest,
	"UnrealMcp.ChatCompat.QuirkHandlers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpChatCompatQuirkHandlersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const TArray<FString> NoOpPresetIds =
	{
		TEXT(""),
		TEXT("unknown-provider"),
		TEXT("custom-openai-chat"),
	};

	for (const FString& PresetId : NoOpPresetIds)
	{
		TUniquePtr<UnrealMcp::ChatCompat::IChatCompatQuirkHandler> Handler = MakeQuirkHandlerForPreset(PresetId);
		TestTrue(FString::Printf(TEXT("Preset '%s' returns a handler."), *PresetId), Handler.IsValid());

		FString AccumulatedReasoning = TEXT("existing reasoning");
		Handler->OnDelta(MakeDeltaWithStringField(TEXT("reasoning_content"), TEXT(" ignored")), AccumulatedReasoning);
		TestEqual(FString::Printf(TEXT("Preset '%s' leaves reasoning accumulation unchanged."), *PresetId), AccumulatedReasoning, FString(TEXT("existing reasoning")));

		TSharedRef<FJsonObject> AssistantMessage = MakeAssistantToolCallsMessage();
		Handler->OnBuildAssistantToolCallsMessage(AssistantMessage, TEXT("existing reasoning"));
		TestFalse(FString::Printf(TEXT("Preset '%s' does not inject reasoning_content."), *PresetId), AssistantMessage->HasField(TEXT("reasoning_content")));
	}

	TUniquePtr<UnrealMcp::ChatCompat::IChatCompatQuirkHandler> KimiHandler = MakeQuirkHandlerForPreset(TEXT("kimi"));
	TestTrue(TEXT("Kimi preset returns a handler."), KimiHandler.IsValid());
	if (!KimiHandler.IsValid())
	{
		return false;
	}

	FString KimiReasoning;
	KimiHandler->OnDelta(MakeDeltaWithStringField(TEXT("reasoning_content"), TEXT("first ")), KimiReasoning);
	KimiHandler->OnDelta(MakeDeltaWithStringField(TEXT("content"), TEXT("visible content")), KimiReasoning);
	KimiHandler->OnDelta(MakeDeltaWithStringField(TEXT("reasoning_content"), TEXT("second")), KimiReasoning);
	TestEqual(TEXT("Kimi accumulates exact streamed reasoning_content only."), KimiReasoning, FString(TEXT("first second")));

	TSharedRef<FJsonObject> KimiAssistantMessage = MakeAssistantToolCallsMessage();
	KimiHandler->OnBuildAssistantToolCallsMessage(KimiAssistantMessage, KimiReasoning);

	FString Content;
	TestTrue(TEXT("Kimi assistant message keeps content field."), KimiAssistantMessage->TryGetStringField(TEXT("content"), Content));
	TestEqual(TEXT("Kimi assistant message preserves content."), Content, FString(TEXT("visible assistant text")));

	FString InjectedReasoning;
	TestTrue(TEXT("Kimi assistant message gets reasoning_content."), KimiAssistantMessage->TryGetStringField(TEXT("reasoning_content"), InjectedReasoning));
	TestEqual(TEXT("Kimi assistant message injects exact accumulated reasoning_content."), InjectedReasoning, FString(TEXT("first second")));

	const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
	TestTrue(TEXT("Kimi assistant message keeps tool_calls."), KimiAssistantMessage->TryGetArrayField(TEXT("tool_calls"), ToolCalls));
	TestTrue(TEXT("Kimi assistant message keeps a non-empty tool_calls array."), ToolCalls != nullptr && ToolCalls->Num() == 1);

	TSharedRef<FJsonObject> KimiEmptyReasoningMessage = MakeAssistantToolCallsMessage();
	KimiHandler->OnBuildAssistantToolCallsMessage(KimiEmptyReasoningMessage, TEXT(""));
	TestFalse(TEXT("Kimi does not inject empty reasoning_content."), KimiEmptyReasoningMessage->HasField(TEXT("reasoning_content")));

	return true;
}

#endif
