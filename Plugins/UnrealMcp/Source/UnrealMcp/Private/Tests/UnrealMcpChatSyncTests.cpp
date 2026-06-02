#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HttpServerResponse.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMcpActivityLog.h"
#include "UnrealMcpChatPanel.h"
#include "UnrealMcpSession.h"
#include "UnrealMcpTaskAtlasService.h"

#define private public
#include "UnrealMcpModule.h"
#undef private

namespace UnrealMcpChatSyncTests
{
	FString SanitizeSessionId(FString Value)
	{
		Value = Value.TrimStartAndEnd().ToLower();
		FString Result;
		for (TCHAR Character : Value)
		{
			if (FChar::IsAlnum(Character))
			{
				Result.AppendChar(Character);
			}
			else if (Character == TEXT('-') || Character == TEXT('_') || Character == TEXT('.'))
			{
				Result.AppendChar(Character);
			}
			else if (FChar::IsWhitespace(Character))
			{
				Result.AppendChar(TEXT('-'));
			}
		}
		while (Result.Contains(TEXT("--")))
		{
			Result = Result.Replace(TEXT("--"), TEXT("-"));
		}
		Result.RemoveFromStart(TEXT("-"));
		Result.RemoveFromEnd(TEXT("-"));
		return Result.IsEmpty() ? TEXT("activity-session") : Result.Left(80);
	}

	FString ChatHistoryPath()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp"), TEXT("ChatHistory.json")));
	}

	FString ActivityLogPath(const FString& SessionId)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/ActivityLog"), SanitizeSessionId(SessionId) + TEXT(".jsonl")));
	}

	FUnrealMcpExecutionResult ExecuteMcpTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments)
	{
		FUnrealMcpModule& Module = FModuleManager::LoadModuleChecked<FUnrealMcpModule>(TEXT("UnrealMcp"));
		return Module.ExecuteToolFromEditorUI(ToolName, *Arguments);
	}

	FString JsonToPrettyString(const TSharedPtr<FJsonObject>& Object)
	{
		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Output;
	}

	TSharedPtr<FJsonObject> MakeArgs()
	{
		return MakeShared<FJsonObject>();
	}

	int32 StructuredInt(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		double Value = 0.0;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetNumberField(FieldName, Value);
		}
		return static_cast<int32>(Value);
	}

	bool StructuredBool(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		bool bValue = false;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetBoolField(FieldName, bValue);
		}
		return bValue;
	}

	FString StructuredString(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		FString Value;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetStringField(FieldName, Value);
		}
		return Value;
	}

	const TArray<TSharedPtr<FJsonValue>>* StructuredArray(const FUnrealMcpExecutionResult& Result, const FString& FieldName)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Result.StructuredContent.IsValid())
		{
			Result.StructuredContent->TryGetArrayField(FieldName, Values);
		}
		return Values;
	}

	TSharedPtr<FJsonObject> ObjectAt(const TArray<TSharedPtr<FJsonValue>>* Values, int32 Index)
	{
		return Values && Values->IsValidIndex(Index) && (*Values)[Index].IsValid() && (*Values)[Index]->Type == EJson::Object
			? (*Values)[Index]->AsObject()
			: nullptr;
	}

	FString ObjectString(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		FString Value;
		if (Object.IsValid())
		{
			Object->TryGetStringField(FieldName, Value);
		}
		return Value;
	}

	bool ObjectHasField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		return Object.IsValid() && Object->HasField(FieldName);
	}

	struct FScopedFileBackup
	{
		explicit FScopedFileBackup(const FString& InPath)
			: Path(InPath)
		{
			bHadOriginal = FFileHelper::LoadFileToArray(OriginalBytes, *Path);
		}

		~FScopedFileBackup()
		{
			if (bHadOriginal)
			{
				IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
				FFileHelper::SaveArrayToFile(OriginalBytes, *Path);
			}
			else
			{
				IFileManager::Get().Delete(*Path, false, true, true);
			}
		}

		FString Path;
		TArray<uint8> OriginalBytes;
		bool bHadOriginal = false;
	};

	TSharedPtr<FJsonObject> MakeHistoryEntry(const FString& Body)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("type"), TEXT("user"));
		Entry->SetStringField(TEXT("speaker"), TEXT("You"));
		Entry->SetStringField(TEXT("title"), TEXT(""));
		Entry->SetStringField(TEXT("body"), Body);
		Entry->SetStringField(TEXT("details"), TEXT("secret123"));
		Entry->SetStringField(TEXT("tool_summary"), TEXT("internal tool summary"));
		Entry->SetStringField(TEXT("tool_call_id"), TEXT(""));
		Entry->SetBoolField(TEXT("is_error"), false);
		return Entry;
	}

	bool WriteHistoryFixture(const TArray<FString>& Bodies)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("last_response_id"), TEXT(""));
		Root->SetStringField(TEXT("last_log_text"), TEXT(""));
		TArray<TSharedPtr<FJsonValue>> Entries;
		for (const FString& Body : Bodies)
		{
			Entries.Add(MakeShared<FJsonValueObject>(MakeHistoryEntry(Body)));
		}
		Root->SetArrayField(TEXT("entries"), Entries);
		const FString Path = ChatHistoryPath();
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		return FFileHelper::SaveStringToFile(JsonToPrettyString(Root), *Path);
	}

	void WriteToolLogEvent(const FString& SessionId, const FString& EventKind, const FString& ToolName, int32 Index, bool bWithLegacyArgumentValues = false)
	{
		UnrealMcp::FActivityLogEvent Event;
		Event.EventKind = EventKind;
		Event.Summary = FString::Printf(TEXT("event %d for %s"), Index, *ToolName);
		Event.Payload = MakeShared<FJsonObject>();
		if (!ToolName.IsEmpty())
		{
			Event.Payload->SetStringField(TEXT("toolName"), ToolName);
		}
		Event.Payload->SetArrayField(TEXT("argumentKeys"), TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueString>(TEXT("foo")) });
		Event.Payload->SetBoolField(TEXT("isError"), false);
		if (bWithLegacyArgumentValues)
		{
			Event.Payload->SetStringField(TEXT("argumentValues"), TEXT("secret-value"));
			Event.Payload->SetStringField(TEXT("arguments"), TEXT("{\"foo\":\"secret-value\"}"));
		}
		FString FailureReason;
		UnrealMcp::TryWriteActivityEventForSession(SessionId, Event, FailureReason);
	}

	int32 CountToolCallEventsInLaunchSession(const FString& ToolNameFilter = FString())
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *ActivityLogPath(UnrealMcp::GetLaunchSessionId())))
		{
			return 0;
		}

		int32 Count = 0;
		for (const FString& Line : Lines)
		{
			TSharedPtr<FJsonObject> EventObject;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);
			if (!FJsonSerializer::Deserialize(Reader, EventObject) || !EventObject.IsValid())
			{
				continue;
			}
			FString EventKind;
			EventObject->TryGetStringField(TEXT("eventKind"), EventKind);
			if (EventKind != TEXT("tool_call"))
			{
				continue;
			}
			if (!ToolNameFilter.IsEmpty())
			{
				const TSharedPtr<FJsonObject>* Payload = nullptr;
				FString ToolName;
				if (!EventObject->TryGetObjectField(TEXT("payload"), Payload) || !Payload || !(*Payload).IsValid() || !(*Payload)->TryGetStringField(TEXT("toolName"), ToolName) || ToolName != ToolNameFilter)
				{
					continue;
				}
			}
			++Count;
		}
		return Count;
	}

	TUniquePtr<FHttpServerResponse> CallToolViaProtocol(const FString& ToolName)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("name"), ToolName);
		Params->SetObjectField(TEXT("arguments"), MakeShared<FJsonObject>());
		FUnrealMcpModule& Module = FModuleManager::LoadModuleChecked<FUnrealMcpModule>(TEXT("UnrealMcp"));
		return Module.HandleToolsCall(MakeShared<FJsonValueNumber>(9.0), *Params);
	}

	bool ProtocolResponseIsError(const FHttpServerResponse& Response)
	{
		const FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Response.Body.GetData()), Response.Body.Num());
		const FString Body(Converter.Length(), Converter.Get());
		TSharedPtr<FJsonObject> Payload;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
		if (!FJsonSerializer::Deserialize(Reader, Payload) || !Payload.IsValid())
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* Result = nullptr;
		bool bIsError = false;
		return Payload->TryGetObjectField(TEXT("result"), Result) && Result && (*Result).IsValid() && (*Result)->TryGetBoolField(TEXT("isError"), bIsError) && bIsError;
	}

	UnrealMcp::FTaskAtlasStepRef MakeStepRef(const FString& ToolName, const FString& CaptureStatus)
	{
		UnrealMcp::FTaskAtlasStepRef Step;
		Step.ToolName = ToolName;
		Step.EventId = FString::Printf(TEXT("chunk9-%s"), *ToolName.Replace(TEXT("."), TEXT("_")));
		Step.CaptureStatus = CaptureStatus;
		return Step;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpChatSyncInjectPanelNotFoundTest,
	"UnrealMcp.ChatSync.InjectUserInput.PanelNotFound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpChatSyncInjectPanelNotFoundTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// Editor automation may have registered the live ChatPanel during startup.
	// Temporarily unregister any active panel so we can exercise the "panel
	// not open" path, then restore on scope exit.
	SUnrealMcpChatPanel* const PreExisting = UnrealMcp::ChatPanelRegistry::TryGetActivePanel();
	if (PreExisting)
	{
		UnrealMcp::ChatPanelRegistry::UnregisterActivePanel(PreExisting);
	}
	ON_SCOPE_EXIT
	{
		if (PreExisting)
		{
			UnrealMcp::ChatPanelRegistry::RegisterActivePanel(PreExisting);
		}
	};

	TSharedPtr<FJsonObject> Args = UnrealMcpChatSyncTests::MakeArgs();
	Args->SetStringField(TEXT("text"), TEXT("hello"));
	const FUnrealMcpExecutionResult Result = UnrealMcpChatSyncTests::ExecuteMcpTool(TEXT("unreal.chat_inject_user_input"), Args);
	TestFalse(TEXT("panel not found is graceful"), Result.bIsError);
	TestFalse(TEXT("panelFound false"), UnrealMcpChatSyncTests::StructuredBool(Result, TEXT("panelFound")));
	TestFalse(TEXT("ok false"), UnrealMcpChatSyncTests::StructuredBool(Result, TEXT("ok")));
	TestEqual(TEXT("errorCode"), UnrealMcpChatSyncTests::StructuredString(Result, TEXT("errorCode")), TEXT("chat_panel_not_open"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpChatSyncInjectDryRunWithPanelTest,
	"UnrealMcp.ChatSync.InjectUserInput.DryRunWithPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpChatSyncInjectDryRunWithPanelTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	SUnrealMcpChatPanel* Sentinel = reinterpret_cast<SUnrealMcpChatPanel*>(0xDEAD);
	UnrealMcp::ChatPanelRegistry::RegisterActivePanel(Sentinel);
	ON_SCOPE_EXIT
	{
		UnrealMcp::ChatPanelRegistry::UnregisterActivePanel(Sentinel);
	};
	TSharedPtr<FJsonObject> Args = UnrealMcpChatSyncTests::MakeArgs();
	Args->SetStringField(TEXT("text"), TEXT("hello"));
	Args->SetBoolField(TEXT("dryRun"), true);
	const FUnrealMcpExecutionResult Result = UnrealMcpChatSyncTests::ExecuteMcpTool(TEXT("unreal.chat_inject_user_input"), Args);
	TestFalse(TEXT("dry-run with panel is not error"), Result.bIsError);
	TestTrue(TEXT("panelFound true"), UnrealMcpChatSyncTests::StructuredBool(Result, TEXT("panelFound")));
	TestFalse(TEXT("messageQueued false"), UnrealMcpChatSyncTests::StructuredBool(Result, TEXT("messageQueued")));
	TestTrue(TEXT("ok true"), UnrealMcpChatSyncTests::StructuredBool(Result, TEXT("ok")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpChatSyncInjectEmptyTextRejectedTest,
	"UnrealMcp.ChatSync.InjectUserInput.EmptyTextRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpChatSyncInjectEmptyTextRejectedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TSharedPtr<FJsonObject> Args = UnrealMcpChatSyncTests::MakeArgs();
	Args->SetStringField(TEXT("text"), TEXT(""));
	const FUnrealMcpExecutionResult Result = UnrealMcpChatSyncTests::ExecuteMcpTool(TEXT("unreal.chat_inject_user_input"), Args);
	TestTrue(TEXT("empty text is server error"), Result.bIsError);
	TestFalse(TEXT("no panel touched"), UnrealMcpChatSyncTests::StructuredBool(Result, TEXT("panelFound")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpChatSyncHistoryMissingFileTest,
	"UnrealMcp.ChatSync.HistoryTail.MissingFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpChatSyncHistoryMissingFileTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcpChatSyncTests::FScopedFileBackup Backup(UnrealMcpChatSyncTests::ChatHistoryPath());
	IFileManager::Get().Delete(*UnrealMcpChatSyncTests::ChatHistoryPath(), false, true, true);
	const FUnrealMcpExecutionResult Result = UnrealMcpChatSyncTests::ExecuteMcpTool(TEXT("unreal.chat_history_tail"), UnrealMcpChatSyncTests::MakeArgs());
	TestFalse(TEXT("missing history is graceful"), Result.bIsError);
	TestEqual(TEXT("count"), UnrealMcpChatSyncTests::StructuredInt(Result, TEXT("count")), 0);
	TestTrue(TEXT("entries array exists"), UnrealMcpChatSyncTests::StructuredArray(Result, TEXT("entries")) != nullptr);
	TestFalse(TEXT("note present"), UnrealMcpChatSyncTests::StructuredString(Result, TEXT("note")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpChatSyncHistoryReadsLatestTest,
	"UnrealMcp.ChatSync.HistoryTail.ReadsLatestNEntries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpChatSyncHistoryReadsLatestTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcpChatSyncTests::FScopedFileBackup Backup(UnrealMcpChatSyncTests::ChatHistoryPath());
	TestTrue(TEXT("write history"), UnrealMcpChatSyncTests::WriteHistoryFixture({ TEXT("body-1"), TEXT("body-2"), TEXT("body-3"), TEXT("body-4"), TEXT("body-5") }));
	TSharedPtr<FJsonObject> Args = UnrealMcpChatSyncTests::MakeArgs();
	Args->SetNumberField(TEXT("count"), 3.0);
	const FUnrealMcpExecutionResult Result = UnrealMcpChatSyncTests::ExecuteMcpTool(TEXT("unreal.chat_history_tail"), Args);
	const TArray<TSharedPtr<FJsonValue>>* Entries = UnrealMcpChatSyncTests::StructuredArray(Result, TEXT("entries"));
	TestFalse(TEXT("history tail is not error"), Result.bIsError);
	TestEqual(TEXT("tail count"), Entries ? Entries->Num() : 0, 3);
	TestEqual(TEXT("first tail body"), UnrealMcpChatSyncTests::ObjectString(UnrealMcpChatSyncTests::ObjectAt(Entries, 0), TEXT("body")), TEXT("body-3"));
	TestEqual(TEXT("last tail body"), UnrealMcpChatSyncTests::ObjectString(UnrealMcpChatSyncTests::ObjectAt(Entries, 2), TEXT("body")), TEXT("body-5"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpChatSyncHistoryTruncatesLongBodiesTest,
	"UnrealMcp.ChatSync.HistoryTail.TruncatesLongBodies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpChatSyncHistoryTruncatesLongBodiesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcpChatSyncTests::FScopedFileBackup Backup(UnrealMcpChatSyncTests::ChatHistoryPath());
	TestTrue(TEXT("write history"), UnrealMcpChatSyncTests::WriteHistoryFixture({ FString::ChrN(3000, TEXT('x')) }));
	const FUnrealMcpExecutionResult Result = UnrealMcpChatSyncTests::ExecuteMcpTool(TEXT("unreal.chat_history_tail"), UnrealMcpChatSyncTests::MakeArgs());
	const TArray<TSharedPtr<FJsonValue>>* Entries = UnrealMcpChatSyncTests::StructuredArray(Result, TEXT("entries"));
	TestEqual(TEXT("truncated length"), UnrealMcpChatSyncTests::ObjectString(UnrealMcpChatSyncTests::ObjectAt(Entries, 0), TEXT("body")).Len(), 2000);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpChatSyncHistoryDropsDetailsTest,
	"UnrealMcp.ChatSync.HistoryTail.DropsDetailsAndSecrets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpChatSyncHistoryDropsDetailsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcpChatSyncTests::FScopedFileBackup Backup(UnrealMcpChatSyncTests::ChatHistoryPath());
	TestTrue(TEXT("write history"), UnrealMcpChatSyncTests::WriteHistoryFixture({ TEXT("safe body") }));
	const FUnrealMcpExecutionResult Result = UnrealMcpChatSyncTests::ExecuteMcpTool(TEXT("unreal.chat_history_tail"), UnrealMcpChatSyncTests::MakeArgs());
	const TArray<TSharedPtr<FJsonValue>>* Entries = UnrealMcpChatSyncTests::StructuredArray(Result, TEXT("entries"));
	const TSharedPtr<FJsonObject> Entry = UnrealMcpChatSyncTests::ObjectAt(Entries, 0);
	TestFalse(TEXT("details dropped"), UnrealMcpChatSyncTests::ObjectHasField(Entry, TEXT("details")));
	TestFalse(TEXT("tool_summary dropped"), UnrealMcpChatSyncTests::ObjectHasField(Entry, TEXT("tool_summary")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpChatSyncToolLogMissingSessionTest,
	"UnrealMcp.ChatSync.ToolLogTail.MissingSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpChatSyncToolLogMissingSessionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString SessionId = TEXT("chunk9-missing-session");
	IFileManager::Get().Delete(*UnrealMcpChatSyncTests::ActivityLogPath(SessionId), false, true, true);
	TSharedPtr<FJsonObject> Args = UnrealMcpChatSyncTests::MakeArgs();
	Args->SetStringField(TEXT("sessionId"), SessionId);
	const FUnrealMcpExecutionResult Result = UnrealMcpChatSyncTests::ExecuteMcpTool(TEXT("unreal.chat_tool_log_tail"), Args);
	TestFalse(TEXT("missing session is graceful"), Result.bIsError);
	TestEqual(TEXT("count"), UnrealMcpChatSyncTests::StructuredInt(Result, TEXT("count")), 0);
	TestFalse(TEXT("note present"), UnrealMcpChatSyncTests::StructuredString(Result, TEXT("note")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpChatSyncToolLogReadsLatestTest,
	"UnrealMcp.ChatSync.ToolLogTail.ReadsLatestNEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpChatSyncToolLogReadsLatestTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString SessionId = TEXT("chunk9-tool-log-tail");
	IFileManager::Get().Delete(*UnrealMcpChatSyncTests::ActivityLogPath(SessionId), false, true, true);
	for (int32 Index = 1; Index <= 5; ++Index)
	{
		UnrealMcpChatSyncTests::WriteToolLogEvent(SessionId, TEXT("tool_call"), FString::Printf(TEXT("unreal.tool_%d"), Index), Index);
	}
	UnrealMcpChatSyncTests::WriteToolLogEvent(SessionId, TEXT("user_intent"), TEXT(""), 6);
	UnrealMcpChatSyncTests::WriteToolLogEvent(SessionId, TEXT("user_intent"), TEXT(""), 7);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*UnrealMcpChatSyncTests::ActivityLogPath(SessionId), false, true, true);
	};
	TSharedPtr<FJsonObject> Args = UnrealMcpChatSyncTests::MakeArgs();
	Args->SetStringField(TEXT("sessionId"), SessionId);
	Args->SetNumberField(TEXT("count"), 3.0);
	const FUnrealMcpExecutionResult Result = UnrealMcpChatSyncTests::ExecuteMcpTool(TEXT("unreal.chat_tool_log_tail"), Args);
	const TArray<TSharedPtr<FJsonValue>>* Calls = UnrealMcpChatSyncTests::StructuredArray(Result, TEXT("toolCalls"));
	TestFalse(TEXT("tool log tail is not error"), Result.bIsError);
	TestEqual(TEXT("tail count"), Calls ? Calls->Num() : 0, 3);
	TestEqual(TEXT("total tool calls"), UnrealMcpChatSyncTests::StructuredInt(Result, TEXT("totalToolCallsInSession")), 5);
	TestEqual(TEXT("first latest tool"), UnrealMcpChatSyncTests::ObjectString(UnrealMcpChatSyncTests::ObjectAt(Calls, 0), TEXT("tool")), TEXT("unreal.tool_3"));
	TestEqual(TEXT("last latest tool"), UnrealMcpChatSyncTests::ObjectString(UnrealMcpChatSyncTests::ObjectAt(Calls, 2), TEXT("tool")), TEXT("unreal.tool_5"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpChatSyncToolLogRedactsArgumentValuesTest,
	"UnrealMcp.ChatSync.ToolLogTail.RedactsArgumentValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpChatSyncToolLogRedactsArgumentValuesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FString SessionId = TEXT("chunk9-tool-log-redaction");
	IFileManager::Get().Delete(*UnrealMcpChatSyncTests::ActivityLogPath(SessionId), false, true, true);
	UnrealMcpChatSyncTests::WriteToolLogEvent(SessionId, TEXT("tool_call"), TEXT("unreal.editor_status"), 1, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().Delete(*UnrealMcpChatSyncTests::ActivityLogPath(SessionId), false, true, true);
	};
	TSharedPtr<FJsonObject> Args = UnrealMcpChatSyncTests::MakeArgs();
	Args->SetStringField(TEXT("sessionId"), SessionId);
	const FUnrealMcpExecutionResult Result = UnrealMcpChatSyncTests::ExecuteMcpTool(TEXT("unreal.chat_tool_log_tail"), Args);
	const TArray<TSharedPtr<FJsonValue>>* Calls = UnrealMcpChatSyncTests::StructuredArray(Result, TEXT("toolCalls"));
	const TSharedPtr<FJsonObject> Call = UnrealMcpChatSyncTests::ObjectAt(Calls, 0);
	TestTrue(TEXT("argumentKeys present"), UnrealMcpChatSyncTests::ObjectHasField(Call, TEXT("argumentKeys")));
	TestFalse(TEXT("argumentValues omitted"), UnrealMcpChatSyncTests::ObjectHasField(Call, TEXT("argumentValues")));
	TestFalse(TEXT("arguments omitted"), UnrealMcpChatSyncTests::ObjectHasField(Call, TEXT("arguments")));
	TestFalse(TEXT("payload omitted"), UnrealMcpChatSyncTests::ObjectHasField(Call, TEXT("payload")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpChatSyncProtocolUnknownToolSkipsLogTest,
	"UnrealMcp.ChatSync.ProtocolFixB.UnknownToolSkipsActivityLog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpChatSyncProtocolUnknownToolSkipsLogTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const int32 Before = UnrealMcpChatSyncTests::CountToolCallEventsInLaunchSession();
	TUniquePtr<FHttpServerResponse> Response = UnrealMcpChatSyncTests::CallToolViaProtocol(TEXT("bogus_no_prefix"));
	const int32 After = UnrealMcpChatSyncTests::CountToolCallEventsInLaunchSession();
	TestTrue(TEXT("response valid"), Response.IsValid());
	TestTrue(TEXT("unknown tool response is error"), Response.IsValid() && UnrealMcpChatSyncTests::ProtocolResponseIsError(*Response));
	TestEqual(TEXT("unknown tool does not append tool_call"), After, Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpChatSyncProtocolKnownToolStillLogsTest,
	"UnrealMcp.ChatSync.ProtocolFixB.KnownToolStillLogs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpChatSyncProtocolKnownToolStillLogsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const int32 Before = UnrealMcpChatSyncTests::CountToolCallEventsInLaunchSession(TEXT("unreal.editor_status"));
	TUniquePtr<FHttpServerResponse> Response = UnrealMcpChatSyncTests::CallToolViaProtocol(TEXT("unreal.editor_status"));
	const int32 After = UnrealMcpChatSyncTests::CountToolCallEventsInLaunchSession(TEXT("unreal.editor_status"));
	TestTrue(TEXT("response valid"), Response.IsValid());
	TestFalse(TEXT("known tool response is not error"), Response.IsValid() && UnrealMcpChatSyncTests::ProtocolResponseIsError(*Response));
	TestEqual(TEXT("known tool appends one tool_call"), After, Before + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpChatSyncTaskAtlasNotVisibleNotCountedTest,
	"UnrealMcp.ChatSync.TaskAtlasFixC.NotVisibleNotCounted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpChatSyncTaskAtlasNotVisibleNotCountedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcp::FTaskAtlasModel Task;
	Task.StepRefs.Add(UnrealMcpChatSyncTests::MakeStepRef(TEXT("unreal.editor_status"), TEXT("captured")));
	Task.StepRefs.Add(UnrealMcpChatSyncTests::MakeStepRef(TEXT("task_list"), TEXT("captured")));
	Task.StepRefs.Add(UnrealMcpChatSyncTests::MakeStepRef(TEXT("user.chunk9_forbidden"), TEXT("captured")));
	const UnrealMcp::TaskAtlasService::FEligibilityResult Result = UnrealMcp::TaskAtlasService::ClassifyTask(Task);
	TestEqual(TEXT("only user-meaningful deny counts"), Result.DenyCount, 1);
	TestTrue(TEXT("eligibility blocked"), Result.Eligibility == UnrealMcp::TaskAtlasService::EEligibility::Blocked);
	TestEqual(TEXT("blocked reason"), Result.BlockedFirstReason, TEXT("user_tool_forbidden"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FUnrealMcpChatSyncTaskAtlasOnlyNotVisibleNotBlockedTest,
	"UnrealMcp.ChatSync.TaskAtlasFixC.OnlyNotVisibleNotBlocked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FUnrealMcpChatSyncTaskAtlasOnlyNotVisibleNotBlockedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UnrealMcp::FTaskAtlasModel Task;
	Task.StepRefs.Add(UnrealMcpChatSyncTests::MakeStepRef(TEXT("task_list"), TEXT("captured")));
	Task.StepRefs.Add(UnrealMcpChatSyncTests::MakeStepRef(TEXT("bare_missing_tool"), TEXT("captured")));
	const UnrealMcp::TaskAtlasService::FEligibilityResult Result = UnrealMcp::TaskAtlasService::ClassifyTask(Task);
	TestEqual(TEXT("not_visible deny count ignored"), Result.DenyCount, 0);
	TestTrue(TEXT("eligibility preview ready"), Result.Eligibility == UnrealMcp::TaskAtlasService::EEligibility::PreviewReady);
	TestEqual(TEXT("blocked first step remains unset"), Result.BlockedFirstStep, -1);
	return true;
}

#endif
