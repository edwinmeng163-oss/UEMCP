#include "UnrealMcpModule.h"

#include "Async/Async.h"
#include "HAL/CriticalSection.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "UnrealMcpChatPanel.h"
#include "UnrealMcpPythonToolBridge.h"
#include "UnrealMcpActorTools.h"
#include "UnrealMcpAutomationTools.h"
#include "UnrealMcpBlueprintTools.h"
#include "UnrealMcpCodeTools.h"
#include "UnrealMcpDiagnosticsTools.h"
#include "UnrealMcpEditorTools.h"
#include "UnrealMcpMaterialInstanceTools.h"
#include "UnrealMcpMemoryTools.h"
#include "UnrealMcpPieSmokeTools.h"
#include "UnrealMcpScaffoldTools.h"
#include "UnrealMcpSelfExtensionTools.h"
#include "UnrealMcpSkillTools.h"
#include "UnrealMcpTaskAtlasService.h"
#include "UnrealMcpTaskAtlasTools.h"
#include "UnrealMcpToolExecutionGuard.h"
#include "UnrealMcpToolHandlerRegistry.h"
#include "UnrealMcpToolRegistry.h"
#include "UnrealMcpSession.h"
#include "UnrealMcpUserToolListVersion.h"
#include "UnrealMcpUserToolRegistry.h"
#include "UnrealMcpWidgetTools.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealMcp
{
	FUnrealMcpExecutionResult MakeExecutionResult(
		const FString& Text,
		const TSharedPtr<FJsonObject>& StructuredContent = nullptr,
		bool bIsError = false);
	FString GetMcpExtensionLockPath();
	bool TryAcquireExtensionSessionLock(
		const FString& Owner,
		const FString& Reason,
		int32 TtlSeconds,
		bool bForce,
		FString& OutSessionId,
		TSharedPtr<FJsonObject>& OutLockObject,
		FString& OutFailureReason);
	bool ReleaseExtensionSessionLock(const FString& SessionId, bool bForce, FString& OutFailureReason);

	namespace TaskAtlasMcpWrapper
	{
		FString GetStringArgument(const FJsonObject& Arguments, const FString& FieldName, const FString& DefaultValue = FString())
		{
			FString Value = DefaultValue;
			Arguments.TryGetStringField(FieldName, Value);
			return Value.TrimStartAndEnd();
		}

		bool GetBoolArgument(const FJsonObject& Arguments, const FString& FieldName, bool bDefaultValue)
		{
			bool bValue = bDefaultValue;
			Arguments.TryGetBoolField(FieldName, bValue);
			return bValue;
		}

		TArray<TSharedPtr<FJsonValue>> MakeStringArray(const TArray<FString>& Values)
		{
			TArray<TSharedPtr<FJsonValue>> JsonValues;
			JsonValues.Reserve(Values.Num());
			for (const FString& Value : Values)
			{
				JsonValues.Add(MakeShared<FJsonValueString>(Value));
			}
			return JsonValues;
		}

		TSharedPtr<FJsonObject> MakeDeleteContent(
			const FString& ToolName,
			const FString& Outcome,
			const FString& RemovedDir,
			bool bWasStaleEntry,
			int32 ReloadRemovedCount,
			int32 ReloadBeforeCount,
			int32 ReloadAfterCount,
			const TArray<FString>& RejectedReasons,
			const FString& FailureDiagnosticPath,
			const FString& ErrorCode,
			const FString& ErrorMessage)
		{
			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetStringField(TEXT("action"), TEXT("task_atlas_delete_made_tool"));
			Content->SetStringField(TEXT("toolName"), ToolName);
			Content->SetStringField(TEXT("outcome"), Outcome);
			Content->SetStringField(TEXT("removedDir"), RemovedDir);
			Content->SetBoolField(TEXT("wasStaleEntry"), bWasStaleEntry);
			Content->SetNumberField(TEXT("reloadRemovedCount"), ReloadRemovedCount);
			Content->SetNumberField(TEXT("reloadBeforeCount"), ReloadBeforeCount);
			Content->SetNumberField(TEXT("reloadAfterCount"), ReloadAfterCount);
			Content->SetArrayField(TEXT("rejectedReasons"), MakeStringArray(RejectedReasons));
			Content->SetStringField(TEXT("failureDiagnosticPath"), FailureDiagnosticPath);
			Content->SetStringField(TEXT("errorCode"), ErrorCode);
			Content->SetStringField(TEXT("errorMessage"), ErrorMessage);
			return Content;
		}

		bool TryParseOverrideStepArgs(
			const FJsonObject& Arguments,
			UnrealMcp::TaskAtlasService::FMakeCompositeRequest& Request,
			FUnrealMcpExecutionResult& OutResult)
		{
			const TArray<TSharedPtr<FJsonValue>>* OverrideValues = nullptr;
			if (!Arguments.TryGetArrayField(TEXT("overrideStepArgs"), OverrideValues) || !OverrideValues)
			{
				return true;
			}

			for (const TSharedPtr<FJsonValue>& Value : *OverrideValues)
			{
				const TSharedPtr<FJsonObject> OverrideObject = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
				if (!OverrideObject.IsValid())
				{
					OutResult = MakeExecutionResult(TEXT("overrideStepArgs entries must be JSON objects."), nullptr, true);
					return false;
				}

				double OrdinalDouble = -1.0;
				FString ArgumentsJson;
				if (!OverrideObject->TryGetNumberField(TEXT("ordinal"), OrdinalDouble)
					|| OrdinalDouble < 0.0
					|| !OverrideObject->TryGetStringField(TEXT("argumentsJson"), ArgumentsJson)
					|| ArgumentsJson.TrimStartAndEnd().IsEmpty())
				{
					OutResult = MakeExecutionResult(TEXT("overrideStepArgs entries require ordinal >= 0 and non-empty argumentsJson."), nullptr, true);
					return false;
				}

				TSharedPtr<FJsonObject> ParsedArguments;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgumentsJson);
				if (!FJsonSerializer::Deserialize(Reader, ParsedArguments) || !ParsedArguments.IsValid())
				{
					OutResult = MakeExecutionResult(TEXT("overrideStepArgs argumentsJson must parse as a JSON object."), nullptr, true);
					return false;
				}

				Request.OverrideStepArgs.Add(FString::FromInt(static_cast<int32>(OrdinalDouble)), ParsedArguments);
			}
			return true;
		}

		const TCHAR* DeleteOutcomeToString(UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome Outcome)
		{
			using UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome;
			switch (Outcome)
			{
			case EDeleteMadeToolOutcome::Deleted: return TEXT("Deleted");
			case EDeleteMadeToolOutcome::DryRun: return TEXT("DryRun");
			case EDeleteMadeToolOutcome::NotFound: return TEXT("NotFound");
			case EDeleteMadeToolOutcome::ReloadRejected: return TEXT("ReloadRejected");
			case EDeleteMadeToolOutcome::Failed: return TEXT("Failed");
			case EDeleteMadeToolOutcome::Refused:
			default: return TEXT("Refused");
			}
		}

		bool IsMakeError(UnrealMcp::TaskAtlasService::EMakeCompositeOutcome Outcome)
		{
			using UnrealMcp::TaskAtlasService::EMakeCompositeOutcome;
			return Outcome == EMakeCompositeOutcome::StagingFailed || Outcome == EMakeCompositeOutcome::ReloadRejected;
		}

		bool IsDeleteError(UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome Outcome)
		{
			using UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome;
			return Outcome == EDeleteMadeToolOutcome::ReloadRejected || Outcome == EDeleteMadeToolOutcome::Failed;
		}

		bool IsPromoteError(UnrealMcp::TaskAtlasService::EPromoteToRagOutcome Outcome)
		{
			using UnrealMcp::TaskAtlasService::EPromoteToRagOutcome;
			return Outcome == EPromoteToRagOutcome::RefreshFailed || Outcome == EPromoteToRagOutcome::Failed;
		}

		bool IsSmokeError(UnrealMcp::TaskAtlasService::ESmokeOutcome Outcome)
		{
			using UnrealMcp::TaskAtlasService::ESmokeOutcome;
			return Outcome == ESmokeOutcome::Failed || Outcome == ESmokeOutcome::ReloadRejected || Outcome == ESmokeOutcome::FailedToExecute;
		}

		TSharedPtr<FJsonObject> MadeToolEntryToJson(
			const UnrealMcp::TaskAtlasService::FMadeToolEntry& Entry,
			bool bIncludeFailureMarkers)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("toolName"), Entry.ToolName);
			Object->SetStringField(TEXT("scaffoldDir"), Entry.ScaffoldDir);
			Object->SetStringField(TEXT("compositeKind"), Entry.CompositeKind);
			Object->SetStringField(TEXT("replayStatus"), Entry.ReplayStatus);
			Object->SetStringField(TEXT("createdUtc"), Entry.CreatedUtc.ToIso8601());
			Object->SetStringField(TEXT("sourceTaskId"), Entry.SourceTaskId);
			Object->SetStringField(TEXT("generator"), Entry.Generator);
			Object->SetStringField(TEXT("pythonHandlerSha256"), Entry.PythonHandlerSha256);
			Object->SetBoolField(TEXT("loadedInUserRegistry"), Entry.bLoadedInUserRegistry);
			Object->SetBoolField(TEXT("hasFailureMarker"), bIncludeFailureMarkers && Entry.bHasFailureMarker);
			Object->SetStringField(TEXT("failureDiagnosticPath"), bIncludeFailureMarkers ? Entry.FailureDiagnosticPath : FString());
			return Object;
		}

		TSharedPtr<FJsonObject> UserToolViewToJson(
			const UnrealMcp::TaskAtlasService::FUserToolView& View,
			bool bIncludePythonSha,
			bool bIncludeToolJson)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("toolName"), View.ToolName);
			Object->SetStringField(TEXT("scaffoldDir"), View.ScaffoldDir);
			Object->SetStringField(TEXT("generator"), View.Generator);
			Object->SetStringField(TEXT("pythonSha"), bIncludePythonSha ? View.PythonSha : FString());
			Object->SetStringField(TEXT("lifecycleState"), View.LifecycleState);
			Object->SetStringField(TEXT("sourceKind"), View.SourceKind);
			Object->SetStringField(TEXT("sourceTaskId"), View.SourceTaskId);
			Object->SetStringField(TEXT("toolJsonPath"), View.ToolJsonPath);
			Object->SetStringField(TEXT("pythonPath"), View.PythonPath);
			Object->SetBoolField(TEXT("loaded"), View.bLoaded);
			Object->SetBoolField(TEXT("rejected"), View.bRejected);
			Object->SetStringField(TEXT("rejectionReason"), View.RejectionReason);
			if (bIncludeToolJson)
			{
				TSharedPtr<FJsonObject> SanitizedToolJson = MakeShared<FJsonObject>();
				SanitizedToolJson->SetStringField(TEXT("generator"), View.Generator);
				SanitizedToolJson->SetStringField(TEXT("sourceKind"), View.SourceKind);
				SanitizedToolJson->SetStringField(TEXT("sourceTaskId"), View.SourceTaskId);
				SanitizedToolJson->SetStringField(TEXT("pythonHandlerSha256"), bIncludePythonSha ? View.PythonSha : FString());
				Object->SetObjectField(TEXT("toolJson"), SanitizedToolJson);
			}
			return Object;
		}

		struct FChatInjectGameThreadState
		{
			FCriticalSection Mutex;
			FEvent* Event = nullptr;
			bool bPanelFound = false;
			bool bMessageQueued = false;
		};

		int32 GetCountArgument(const FJsonObject& Arguments)
		{
			double CountDouble = 20.0;
			Arguments.TryGetNumberField(TEXT("count"), CountDouble);
			return FMath::Clamp(static_cast<int32>(CountDouble), 1, 100);
		}

		FString SanitizeChatSyncSessionId(FString Value)
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

		FString GetChatHistoryPath()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp"), TEXT("ChatHistory.json")));
		}

		FString GetActivityLogRoot()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp"), TEXT("ActivityLog")));
		}

		FString GetActivityLogPathForSession(const FString& SessionId)
		{
			return FPaths::Combine(GetActivityLogRoot(), SanitizeChatSyncSessionId(SessionId) + TEXT(".jsonl"));
		}

		bool TryResolveActivityLogSession(FString RequestedSessionId, FString& OutSessionId, FString& OutPath)
		{
			RequestedSessionId = RequestedSessionId.TrimStartAndEnd();
			if (!RequestedSessionId.IsEmpty())
			{
				OutSessionId = SanitizeChatSyncSessionId(RequestedSessionId);
				OutPath = GetActivityLogPathForSession(OutSessionId);
				return FPaths::FileExists(OutPath);
			}

			const FString LaunchSessionId = UnrealMcp::GetLaunchSessionId().TrimStartAndEnd();
			if (!LaunchSessionId.IsEmpty())
			{
				const FString LaunchPath = GetActivityLogPathForSession(LaunchSessionId);
				if (FPaths::FileExists(LaunchPath))
				{
					OutSessionId = SanitizeChatSyncSessionId(LaunchSessionId);
					OutPath = LaunchPath;
					return true;
				}
			}

			TArray<FString> ActivityFiles;
			IFileManager::Get().FindFilesRecursive(ActivityFiles, *GetActivityLogRoot(), TEXT("*.jsonl"), true, false);
			ActivityFiles.Sort([](const FString& Left, const FString& Right)
			{
				return IFileManager::Get().GetTimeStamp(*Left) > IFileManager::Get().GetTimeStamp(*Right);
			});
			if (ActivityFiles.Num() == 0)
			{
				OutSessionId.Reset();
				OutPath.Reset();
				return false;
			}

			OutPath = ActivityFiles[0];
			OutSessionId = FPaths::GetBaseFilename(OutPath);
			return true;
		}

		TArray<TSharedPtr<FJsonValue>> CopyStringArrayField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
		{
			TArray<TSharedPtr<FJsonValue>> Output;
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, Values) || !Values)
			{
				return Output;
			}

			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				if (Value.IsValid() && Value->Type == EJson::String)
				{
					Output.Add(MakeShared<FJsonValueString>(Value->AsString()));
				}
			}
			return Output;
		}

		bool InjectChatMessageOnGameThread(const FString& Text, bool& bOutPanelFound, bool& bOutMessageQueued)
		{
			if (IsInGameThread())
			{
				SUnrealMcpChatPanel* Panel = UnrealMcp::ChatPanelRegistry::TryGetActivePanel();
				bOutPanelFound = Panel != nullptr;
				bOutMessageQueued = Panel ? Panel->InjectUserMessage(Text) : false;
				return true;
			}

			FEvent* Event = FPlatformProcess::GetSynchEventFromPool(true);
			TSharedRef<FChatInjectGameThreadState, ESPMode::ThreadSafe> State = MakeShared<FChatInjectGameThreadState, ESPMode::ThreadSafe>();
			State->Event = Event;
			AsyncTask(ENamedThreads::GameThread, [State, Text]()
			{
				SUnrealMcpChatPanel* Panel = UnrealMcp::ChatPanelRegistry::TryGetActivePanel();
				const bool bPanelFound = Panel != nullptr;
				const bool bMessageQueued = Panel ? Panel->InjectUserMessage(Text) : false;

				FScopeLock Lock(&State->Mutex);
				State->bPanelFound = bPanelFound;
				State->bMessageQueued = bMessageQueued;
				if (State->Event)
				{
					State->Event->Trigger();
				}
			});

			if (!Event->Wait(5000))
			{
				FScopeLock Lock(&State->Mutex);
				State->Event = nullptr;
				FPlatformProcess::ReturnSynchEventToPool(Event);
				return false;
			}

			{
				FScopeLock Lock(&State->Mutex);
				bOutPanelFound = State->bPanelFound;
				bOutMessageQueued = State->bMessageQueued;
				State->Event = nullptr;
			}
			FPlatformProcess::ReturnSynchEventToPool(Event);
			return true;
		}

		TSharedPtr<FJsonObject> MakeChatInjectContent(
			bool bOk,
			bool bPanelFound,
			bool bMessageQueued,
			const FString& SessionId,
			bool bDryRun)
		{
			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetStringField(TEXT("action"), TEXT("chat_inject_user_input"));
			Content->SetBoolField(TEXT("ok"), bOk);
			Content->SetBoolField(TEXT("panelFound"), bPanelFound);
			Content->SetBoolField(TEXT("messageQueued"), bMessageQueued);
			Content->SetStringField(TEXT("sessionId"), SessionId);
			Content->SetBoolField(TEXT("dryRun"), bDryRun);
			return Content;
		}

		FUnrealMcpExecutionResult ExecuteChatInjectUserInput(const FJsonObject& Arguments)
		{
			FString Text;
			Arguments.TryGetStringField(TEXT("text"), Text);
			FString SessionId;
			Arguments.TryGetStringField(TEXT("sessionId"), SessionId);
			const bool bDryRun = GetBoolArgument(Arguments, TEXT("dryRun"), false);

			if (Text.TrimStartAndEnd().IsEmpty())
			{
				TSharedPtr<FJsonObject> Content = MakeChatInjectContent(false, false, false, SessionId, bDryRun);
				return MakeExecutionResult(TEXT("chat_inject_user_input requires non-empty text."), Content, true);
			}

			if (bDryRun)
			{
				const bool bPanelFound = UnrealMcp::ChatPanelRegistry::TryGetActivePanel() != nullptr;
				TSharedPtr<FJsonObject> Content = MakeChatInjectContent(bPanelFound, bPanelFound, false, SessionId, true);
				if (!bPanelFound)
				{
					Content->SetStringField(TEXT("errorCode"), TEXT("chat_panel_not_open"));
					Content->SetStringField(TEXT("errorMessage"), TEXT("Editor Chat Panel is not open."));
				}
				return MakeExecutionResult(
					bPanelFound ? TEXT("Dry-run: chat panel available for injection.") : TEXT("Editor Chat Panel is not open."),
					Content,
					false);
			}

			bool bPanelFound = false;
			bool bMessageQueued = false;
			if (!InjectChatMessageOnGameThread(Text, bPanelFound, bMessageQueued))
			{
				TSharedPtr<FJsonObject> Content = MakeChatInjectContent(false, bPanelFound, bMessageQueued, SessionId, false);
				Content->SetStringField(TEXT("errorCode"), TEXT("game_thread_timeout"));
				Content->SetStringField(TEXT("errorMessage"), TEXT("Game-thread injection timed out after 5s."));
				return MakeExecutionResult(TEXT("Game-thread injection timed out after 5s."), Content, true);
			}

			TSharedPtr<FJsonObject> Content = MakeChatInjectContent(bPanelFound && bMessageQueued, bPanelFound, bMessageQueued, SessionId, false);
			if (!bPanelFound)
			{
				Content->SetStringField(TEXT("errorCode"), TEXT("chat_panel_not_open"));
				Content->SetStringField(TEXT("errorMessage"), TEXT("Editor Chat Panel is not open."));
				return MakeExecutionResult(TEXT("Editor Chat Panel is not open."), Content, false);
			}
			if (!bMessageQueued)
			{
				Content->SetStringField(TEXT("errorCode"), TEXT("injection_refused"));
				Content->SetStringField(TEXT("errorMessage"), TEXT("Chat panel refused injection."));
				return MakeExecutionResult(TEXT("Chat panel refused injection."), Content, false);
			}

			return MakeExecutionResult(TEXT("Queued user prompt into editor Chat Panel."), Content, false);
		}

		FUnrealMcpExecutionResult ExecuteChatHistoryTail(const FJsonObject& Arguments)
		{
			const int32 Count = GetCountArgument(Arguments);
			FString SessionId;
			Arguments.TryGetStringField(TEXT("sessionId"), SessionId);

			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetStringField(TEXT("action"), TEXT("chat_history_tail"));
			Content->SetStringField(TEXT("sessionId"), SessionId);

			const FString HistoryPath = GetChatHistoryPath();
			FString JsonText;
			if (!FFileHelper::LoadFileToString(JsonText, *HistoryPath))
			{
				Content->SetNumberField(TEXT("count"), 0);
				Content->SetNumberField(TEXT("totalAvailable"), 0);
				Content->SetArrayField(TEXT("entries"), TArray<TSharedPtr<FJsonValue>>());
				Content->SetStringField(TEXT("note"), TEXT("Chat history file does not exist yet."));
				return MakeExecutionResult(TEXT("Chat history file does not exist yet."), Content, false);
			}

			TSharedPtr<FJsonObject> RootObject;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
			if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
			{
				Content->SetNumberField(TEXT("count"), 0);
				Content->SetNumberField(TEXT("totalAvailable"), 0);
				Content->SetArrayField(TEXT("entries"), TArray<TSharedPtr<FJsonValue>>());
				Content->SetStringField(TEXT("note"), TEXT("Chat history file could not be parsed."));
				return MakeExecutionResult(TEXT("Chat history file could not be parsed."), Content, false);
			}

			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if (!RootObject->TryGetArrayField(TEXT("entries"), Entries) || !Entries)
			{
				Content->SetNumberField(TEXT("count"), 0);
				Content->SetNumberField(TEXT("totalAvailable"), 0);
				Content->SetArrayField(TEXT("entries"), TArray<TSharedPtr<FJsonValue>>());
				Content->SetStringField(TEXT("note"), TEXT("Chat history file has no entries array."));
				return MakeExecutionResult(TEXT("Chat history file has no entries array."), Content, false);
			}

			TArray<TSharedPtr<FJsonValue>> TailEntries;
			const int32 StartIndex = FMath::Max(0, Entries->Num() - Count);
			for (int32 Index = StartIndex; Index < Entries->Num(); ++Index)
			{
				const TSharedPtr<FJsonObject> EntryObject = (*Entries)[Index].IsValid() && (*Entries)[Index]->Type == EJson::Object ? (*Entries)[Index]->AsObject() : nullptr;
				if (!EntryObject.IsValid())
				{
					continue;
				}

				FString Type;
				FString Speaker;
				FString Title;
				FString Body;
				FString ToolCallId;
				bool bIsError = false;
				EntryObject->TryGetStringField(TEXT("type"), Type);
				EntryObject->TryGetStringField(TEXT("speaker"), Speaker);
				EntryObject->TryGetStringField(TEXT("title"), Title);
				EntryObject->TryGetStringField(TEXT("body"), Body);
				EntryObject->TryGetStringField(TEXT("tool_call_id"), ToolCallId);
				EntryObject->TryGetBoolField(TEXT("is_error"), bIsError);

				TSharedPtr<FJsonObject> OutputEntry = MakeShared<FJsonObject>();
				OutputEntry->SetStringField(TEXT("type"), Type);
				OutputEntry->SetStringField(TEXT("speaker"), Speaker);
				OutputEntry->SetStringField(TEXT("title"), Title);
				OutputEntry->SetStringField(TEXT("body"), Body.Left(2000));
				OutputEntry->SetStringField(TEXT("tool_call_id"), ToolCallId);
				OutputEntry->SetBoolField(TEXT("is_error"), bIsError);
				TailEntries.Add(MakeShared<FJsonValueObject>(OutputEntry));
			}

			Content->SetNumberField(TEXT("count"), TailEntries.Num());
			Content->SetNumberField(TEXT("totalAvailable"), Entries->Num());
			Content->SetArrayField(TEXT("entries"), TailEntries);
			return MakeExecutionResult(FString::Printf(TEXT("Read %d chat history entries."), TailEntries.Num()), Content, false);
		}

		FUnrealMcpExecutionResult ExecuteChatToolLogTail(const FJsonObject& Arguments)
		{
			const int32 Count = GetCountArgument(Arguments);
			FString RequestedSessionId;
			Arguments.TryGetStringField(TEXT("sessionId"), RequestedSessionId);

			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetStringField(TEXT("action"), TEXT("chat_tool_log_tail"));
			Content->SetNumberField(TEXT("count"), 0);
			Content->SetNumberField(TEXT("totalToolCallsInSession"), 0);
			Content->SetArrayField(TEXT("toolCalls"), TArray<TSharedPtr<FJsonValue>>());

			FString SessionId;
			FString ActivityLogPath;
			if (!TryResolveActivityLogSession(RequestedSessionId, SessionId, ActivityLogPath))
			{
				Content->SetStringField(TEXT("sessionId"), RequestedSessionId.TrimStartAndEnd());
				Content->SetStringField(TEXT("note"), TEXT("ActivityLog session file does not exist."));
				return MakeExecutionResult(TEXT("ActivityLog session file does not exist."), Content, false);
			}
			Content->SetStringField(TEXT("sessionId"), SessionId);

			TArray<FString> Lines;
			if (!FFileHelper::LoadFileToStringArray(Lines, *ActivityLogPath))
			{
				Content->SetStringField(TEXT("note"), TEXT("ActivityLog session file could not be read."));
				return MakeExecutionResult(TEXT("ActivityLog session file could not be read."), Content, false);
			}

			TArray<TSharedPtr<FJsonObject>> ToolCallObjects;
			for (const FString& Line : Lines)
			{
				const FString TrimmedLine = Line.TrimStartAndEnd();
				if (TrimmedLine.IsEmpty())
				{
					continue;
				}

				TSharedPtr<FJsonObject> EventObject;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TrimmedLine);
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

				const TSharedPtr<FJsonObject>* PayloadPtr = nullptr;
				TSharedPtr<FJsonObject> Payload;
				if (EventObject->TryGetObjectField(TEXT("payload"), PayloadPtr) && PayloadPtr)
				{
					Payload = *PayloadPtr;
				}
				if (!Payload.IsValid())
				{
					continue;
				}

				FString TimestampUtc;
				if (!EventObject->TryGetStringField(TEXT("ts"), TimestampUtc))
				{
					EventObject->TryGetStringField(TEXT("timestampUtc"), TimestampUtc);
				}
				FString ToolName;
				FString Summary;
				bool bIsError = false;
				Payload->TryGetStringField(TEXT("toolName"), ToolName);
				EventObject->TryGetStringField(TEXT("summary"), Summary);
				Payload->TryGetBoolField(TEXT("isError"), bIsError);

				TSharedPtr<FJsonObject> OutputCall = MakeShared<FJsonObject>();
				OutputCall->SetStringField(TEXT("ts"), TimestampUtc);
				OutputCall->SetStringField(TEXT("tool"), ToolName);
				OutputCall->SetArrayField(TEXT("argumentKeys"), CopyStringArrayField(Payload, TEXT("argumentKeys")));
				OutputCall->SetBoolField(TEXT("isError"), bIsError);
				OutputCall->SetStringField(TEXT("summary"), Summary.Left(200));
				ToolCallObjects.Add(OutputCall);
			}

			TArray<TSharedPtr<FJsonValue>> TailCalls;
			const int32 StartIndex = FMath::Max(0, ToolCallObjects.Num() - Count);
			for (int32 Index = StartIndex; Index < ToolCallObjects.Num(); ++Index)
			{
				TailCalls.Add(MakeShared<FJsonValueObject>(ToolCallObjects[Index]));
			}

			Content->SetNumberField(TEXT("count"), TailCalls.Num());
			Content->SetNumberField(TEXT("totalToolCallsInSession"), ToolCallObjects.Num());
			Content->SetArrayField(TEXT("toolCalls"), TailCalls);
			return MakeExecutionResult(FString::Printf(TEXT("Read %d chat tool log events."), TailCalls.Num()), Content, false);
		}

		bool TryExecute(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult)
		{
			if (ToolName == TEXT("unreal.task_atlas_make_composite"))
			{
				UnrealMcp::TaskAtlasService::FMakeCompositeRequest Request;
				Request.TaskId = GetStringArgument(Arguments, TEXT("taskId"));
				Request.bPreferDocumentOnly = GetBoolArgument(Arguments, TEXT("preferDocumentOnly"), false);
				Request.bForceWriteEvenIfBlocked = GetBoolArgument(Arguments, TEXT("forceWriteEvenIfBlocked"), false);
				if (!TryParseOverrideStepArgs(Arguments, Request, OutResult))
				{
					return true;
				}

				const UnrealMcp::TaskAtlasService::FMakeCompositeResult Result = UnrealMcp::TaskAtlasService::MakeComposite(Request);
				FString OutcomeText = TEXT("unknown");
				if (Result.StructuredContent.IsValid())
				{
					Result.StructuredContent->TryGetStringField(TEXT("outcome"), OutcomeText);
				}
				const FString Text = Result.ErrorMessage.IsEmpty()
					? FString::Printf(TEXT("Task Atlas make composite outcome: %s"), *OutcomeText)
					: Result.ErrorMessage;
				OutResult = MakeExecutionResult(Text, Result.StructuredContent, IsMakeError(Result.Outcome));
				return true;
			}

			if (ToolName == TEXT("unreal.task_atlas_delete_made_tool"))
			{
				const FString TargetToolName = GetStringArgument(Arguments, TEXT("toolName"));
				const bool bConfirm = GetBoolArgument(Arguments, TEXT("confirm"), false);
				if (!bConfirm)
				{
					TSharedPtr<FJsonObject> Content = MakeDeleteContent(
						TargetToolName,
						TEXT("Refused"),
						FString(),
						false,
						0,
						0,
						0,
						TArray<FString>(),
						FString(),
						TEXT("confirm_required"),
						TEXT("Set confirm=true to delete a Task Atlas generated user tool."));
					OutResult = MakeExecutionResult(TEXT("Refused to delete generated user tool: confirm=true is required."), Content, false);
					return true;
				}

				const bool bDryRun = GetBoolArgument(Arguments, TEXT("dryRun"), false);
				if (bDryRun)
				{
					FString TargetDir;
					bool bLoaded = false;
					for (const UnrealMcp::TaskAtlasService::FMadeToolEntry& Entry : UnrealMcp::TaskAtlasService::ListMadeTools())
					{
						if (Entry.ToolName == TargetToolName)
						{
							TargetDir = Entry.ScaffoldDir;
							bLoaded = Entry.bLoadedInUserRegistry;
							break;
						}
					}
					const FString Outcome = TargetDir.IsEmpty() ? FString(TEXT("NotFound")) : FString(TEXT("DryRun"));
					TSharedPtr<FJsonObject> Content = MakeDeleteContent(
						TargetToolName,
						Outcome,
						TargetDir,
						!TargetDir.IsEmpty() && !bLoaded,
						0,
						0,
						0,
						TArray<FString>(),
						FString(),
						TargetDir.IsEmpty() ? FString(TEXT("not_found")) : FString(),
						TargetDir.IsEmpty() ? FString(TEXT("Task Atlas generated user tool was not found.")) : FString());
					OutResult = MakeExecutionResult(
						TargetDir.IsEmpty() ? TEXT("Task Atlas generated user tool was not found.") : TEXT("Dry-run: generated user tool would be deleted."),
						Content,
						false);
					return true;
				}

				const UnrealMcp::TaskAtlasService::FDeleteMadeToolResult Result = UnrealMcp::TaskAtlasService::DeleteMadeTool(TargetToolName);
				const FString Text = Result.ErrorMessage.IsEmpty()
					? FString::Printf(TEXT("Task Atlas delete made tool outcome: %s"), DeleteOutcomeToString(Result.Outcome))
					: Result.ErrorMessage;
				OutResult = MakeExecutionResult(Text, Result.StructuredContent, IsDeleteError(Result.Outcome));
				return true;
			}

			if (ToolName == TEXT("unreal.task_atlas_list_made_tools"))
			{
				const bool bIncludeStale = GetBoolArgument(Arguments, TEXT("includeStale"), true);
				const bool bIncludeFailureMarkers = GetBoolArgument(Arguments, TEXT("includeFailureMarkers"), true);
				const FString SourceTaskId = GetStringArgument(Arguments, TEXT("sourceTaskId"));

				TArray<TSharedPtr<FJsonValue>> ToolValues;
				for (const UnrealMcp::TaskAtlasService::FMadeToolEntry& Entry : UnrealMcp::TaskAtlasService::ListMadeTools())
				{
					if (!bIncludeStale && !Entry.bLoadedInUserRegistry)
					{
						continue;
					}
					if (!SourceTaskId.IsEmpty() && Entry.SourceTaskId != SourceTaskId)
					{
						continue;
					}
					ToolValues.Add(MakeShared<FJsonValueObject>(MadeToolEntryToJson(Entry, bIncludeFailureMarkers)));
				}

				TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
				Content->SetStringField(TEXT("action"), TEXT("task_atlas_list_made_tools"));
				Content->SetNumberField(TEXT("count"), ToolValues.Num());
				Content->SetNumberField(TEXT("toolsListVersion"), static_cast<double>(UnrealMcp::GetUserToolListVersion()));
				Content->SetArrayField(TEXT("madeTools"), ToolValues);
				OutResult = MakeExecutionResult(FString::Printf(TEXT("Listed %d Task Atlas generated user tools."), ToolValues.Num()), Content, false);
				return true;
			}

			if (ToolName == TEXT("unreal.task_atlas_promote_to_rag"))
			{
				UnrealMcp::TaskAtlasService::FPromoteToRagRequest Request;
				Request.TaskId = GetStringArgument(Arguments, TEXT("taskId"));
				Request.bDryRun = GetBoolArgument(Arguments, TEXT("dryRun"), false);

				const bool bRefreshIndex = GetBoolArgument(Arguments, TEXT("refreshIndex"), true);
				if (!Request.bDryRun && !bRefreshIndex)
				{
					TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
					Content->SetStringField(TEXT("action"), TEXT("task_atlas_promote_to_rag"));
					Content->SetStringField(TEXT("taskId"), Request.TaskId);
					Content->SetStringField(TEXT("outcome"), TEXT("Refused"));
					Content->SetBoolField(TEXT("dryRun"), false);
					Content->SetBoolField(TEXT("refreshIndex"), false);
					Content->SetStringField(TEXT("errorCode"), TEXT("refresh_index_false_unsupported"));
					Content->SetStringField(TEXT("errorMessage"), TEXT("TaskAtlasService real promotion always refreshes the knowledge index in this chunk."));
					OutResult = MakeExecutionResult(TEXT("Refused: refreshIndex=false is not supported for real promotion in this wrapper."), Content, false);
					return true;
				}

				const UnrealMcp::TaskAtlasService::FPromoteToRagResult Result = UnrealMcp::TaskAtlasService::PromoteToRag(Request);
				if (Result.StructuredContent.IsValid())
				{
					Result.StructuredContent->SetBoolField(TEXT("refreshIndex"), bRefreshIndex);
				}
				const FString Text = Result.ErrorMessage.IsEmpty()
					? TEXT("Task Atlas RAG promotion completed.")
					: Result.ErrorMessage;
				OutResult = MakeExecutionResult(Text, Result.StructuredContent, IsPromoteError(Result.Outcome));
				return true;
			}

			if (ToolName == TEXT("unreal.task_atlas_smoke_made_tool"))
			{
				UnrealMcp::TaskAtlasService::FSmokeRequest Request;
				Request.ToolName = GetStringArgument(Arguments, TEXT("toolName"));
				Request.bDryRun = GetBoolArgument(Arguments, TEXT("dryRun"), true);

				const UnrealMcp::TaskAtlasService::FSmokeResult Result = UnrealMcp::TaskAtlasService::SmokeMadeTool(Request);
				const FString Text = Result.SmokeText.IsEmpty()
					? Result.ErrorMessage
					: Result.SmokeText;
				OutResult = MakeExecutionResult(Text, Result.StructuredContent, IsSmokeError(Result.Outcome));
				return true;
			}

			if (ToolName == TEXT("unreal.user_registry_introspect"))
			{
				const bool bIncludeToolJson = GetBoolArgument(Arguments, TEXT("includeToolJson"), false);
				const bool bIncludePythonSha = GetBoolArgument(Arguments, TEXT("includePythonSha"), true);
				const FString FilterToolName = GetStringArgument(Arguments, TEXT("toolName"));

				TArray<TSharedPtr<FJsonValue>> ToolValues;
				TArray<TSharedPtr<FJsonValue>> RejectedValues;
				for (const UnrealMcp::TaskAtlasService::FUserToolView& View : UnrealMcp::TaskAtlasService::IntrospectUserRegistry())
				{
					if (!FilterToolName.IsEmpty() && View.ToolName != FilterToolName)
					{
						continue;
					}

					TSharedPtr<FJsonObject> Object = UserToolViewToJson(View, bIncludePythonSha, bIncludeToolJson);
					if (View.bRejected)
					{
						RejectedValues.Add(MakeShared<FJsonValueObject>(Object));
					}
					else
					{
						ToolValues.Add(MakeShared<FJsonValueObject>(Object));
					}
				}

				TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
				Content->SetStringField(TEXT("action"), TEXT("user_registry_introspect"));
				Content->SetNumberField(TEXT("count"), ToolValues.Num());
				UnrealMcp::UserRegistry::InitializeUserToolRegistry();
				Content->SetNumberField(TEXT("toolsListVersion"), static_cast<double>(UnrealMcp::GetUserToolListVersion()));
				Content->SetStringField(TEXT("userToolsRootDir"), UnrealMcp::UserRegistry::GetUserToolsRootDir());
				Content->SetArrayField(TEXT("tools"), ToolValues);
				Content->SetArrayField(TEXT("rejected"), RejectedValues);
				OutResult = MakeExecutionResult(FString::Printf(TEXT("Inspected %d loaded user registry tools."), ToolValues.Num()), Content, false);
				return true;
			}

			if (ToolName == TEXT("unreal.chat_inject_user_input"))
			{
				OutResult = ExecuteChatInjectUserInput(Arguments);
				return true;
			}

			if (ToolName == TEXT("unreal.chat_history_tail"))
			{
				OutResult = ExecuteChatHistoryTail(Arguments);
				return true;
			}

			if (ToolName == TEXT("unreal.chat_tool_log_tail"))
			{
				OutResult = ExecuteChatToolLogTail(Arguments);
				return true;
			}

			return false;
		}
	}

	namespace
	{
		class FScopedSkillPromotionLock
		{
		public:
			FScopedSkillPromotionLock(const FString& ToolName, const FJsonObject& Arguments)
			{
				bool bSkipLock = false;
				bool bForceLock = false;
				double TtlSecondsDouble = 900.0;
				FString Owner = TEXT("Unreal MCP Chat");
				Arguments.TryGetBoolField(TEXT("skipLock"), bSkipLock);
				Arguments.TryGetBoolField(TEXT("forceLock"), bForceLock);
				Arguments.TryGetNumberField(TEXT("lockTtlSeconds"), TtlSecondsDouble);
				Arguments.TryGetStringField(TEXT("lockOwner"), Owner);

				if (bSkipLock)
				{
					bAcquired = true;
					bOwnsLock = false;
					return;
				}

				const int32 TtlSeconds = FMath::Clamp(static_cast<int32>(TtlSecondsDouble), 30, 86400);
				const FString Reason = FString::Printf(TEXT("Executing %s"), *ToolName);
				bAcquired = TryAcquireExtensionSessionLock(Owner, Reason, TtlSeconds, bForceLock, SessionId, LockObject, FailureReason);
				bOwnsLock = bAcquired;
			}

			~FScopedSkillPromotionLock()
			{
				if (bOwnsLock && !SessionId.IsEmpty())
				{
					FString ReleaseFailure;
					ReleaseExtensionSessionLock(SessionId, false, ReleaseFailure);
				}
			}

			bool IsAcquired() const
			{
				return bAcquired;
			}

			FString GetFailureReason() const
			{
				return FailureReason;
			}

			TSharedPtr<FJsonObject> MakeStructuredContent(const FString& Action) const
			{
				TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
				StructuredContent->SetStringField(TEXT("action"), Action);
				StructuredContent->SetBoolField(TEXT("locked"), bAcquired);
				StructuredContent->SetStringField(TEXT("lockPath"), GetMcpExtensionLockPath());
				StructuredContent->SetStringField(TEXT("sessionId"), SessionId);
				if (LockObject.IsValid())
				{
					StructuredContent->SetObjectField(TEXT("lock"), LockObject);
				}
				return StructuredContent;
			}

		private:
			bool bAcquired = false;
			bool bOwnsLock = false;
			FString SessionId;
			FString FailureReason;
			TSharedPtr<FJsonObject> LockObject;
		};
	}
}

FUnrealMcpExecutionResult FUnrealMcpModule::ExecuteToolFromEditorUI(const FString& ToolName, const FJsonObject& Arguments) const
{
	return ExecuteTool(ToolName, Arguments);
}

FUnrealMcpExecutionResult FUnrealMcpModule::ExecuteTool(const FString& ToolName, const FJsonObject& Arguments) const
{
	const FString RegisteredHandlerName = UnrealMcp::ResolveToolHandlerName(ToolName);

	TSharedPtr<FJsonObject> PreflightBeforeExecution = UnrealMcp::BuildToolExecutionPreflight(ToolName, Arguments);
	FUnrealMcpExecutionResult Result = ExecuteToolInternal(RegisteredHandlerName, Arguments);
	UnrealMcp::AttachToolExecutionCheck(ToolName, Arguments, PreflightBeforeExecution, Result);
	return Result;
}

FUnrealMcpExecutionResult FUnrealMcpModule::ExecuteToolInternal(const FString& ToolName, const FJsonObject& Arguments) const
{
	const UnrealMcp::FToolHandlerRegistryEntry* HandlerEntry = UnrealMcp::FindToolHandlerRegistryEntry(ToolName);
	if (!HandlerEntry)
	{
		TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
		StructuredContent->SetStringField(TEXT("action"), TEXT("handler_dispatch_failed"));
		StructuredContent->SetStringField(TEXT("handlerName"), ToolName);
		StructuredContent->SetStringField(TEXT("reason"), TEXT("handler_not_registered"));
		return UnrealMcp::MakeExecutionResult(
			FString::Printf(TEXT("Handler '%s' is not registered in the explicit MCP handler registry."), *ToolName),
			StructuredContent,
			true);
	}

	if (HandlerEntry->ImplementationTrack == UnrealMcp::EToolImplementationTrack::Python)
	{
		return UnrealMcp::UnrealMcpPythonToolBridge::ExecutePythonRegisteredTool(*HandlerEntry, Arguments);
	}

	FUnrealMcpExecutionResult CategoryResult;
	if (UnrealMcp::TaskAtlasMcpWrapper::TryExecute(ToolName, Arguments, CategoryResult))
	{
		return CategoryResult;
	}

	const FString& Category = HandlerEntry->Category;
	if (Category == TEXT("editor"))
	{
		if (UnrealMcp::TryExecuteEditorTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("actors"))
	{
		if (UnrealMcp::TryExecuteActorTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("blueprint"))
	{
		if (UnrealMcp::TryExecuteBlueprintTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("code"))
	{
		if (UnrealMcp::TryExecuteCodeTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("widget"))
	{
		if (UnrealMcp::TryExecuteWidgetTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("material"))
	{
		if (UnrealMcp::TryExecuteMaterialInstanceTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("scaffold"))
	{
		if (UnrealMcp::TryExecuteScaffoldTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("memory"))
	{
		if (UnrealMcp::TryExecuteMemoryTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("skills"))
	{
		if (UnrealMcp::TryExecuteSkillTool(
			ToolName,
			Arguments,
			[&ToolName](const FJsonObject& ToolArguments)
			{
				UnrealMcp::FScopedSkillPromotionLock ScopedLock(ToolName, ToolArguments);
				if (!ScopedLock.IsAcquired())
				{
					return UnrealMcp::MakeExecutionResult(ScopedLock.GetFailureReason(), ScopedLock.MakeStructuredContent(TEXT("mcp_extension_lock_failed")), true);
				}
				return UnrealMcp::SkillPromoteDraft(ToolArguments);
			},
			CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("self-extension"))
	{
		TArray<TSharedPtr<FJsonValue>> ToolDefinitions;
		AppendToolDefinitions(ToolDefinitions);
		if (UnrealMcp::TryExecuteSelfExtensionTool(
				ToolName,
				Arguments,
				ToolDefinitions,
				[this](const FJsonObject& ToolArguments) { return RunMcpToolTest(ToolArguments); },
				[this](const FJsonObject& ToolArguments) { return RunMcpTestSuite(ToolArguments); },
				[this](const FJsonObject& ToolArguments) { return RunMcpExtensionPipeline(ToolArguments); },
				[this](const FJsonObject& ToolArguments) { return RunWorkflow(ToolArguments); },
				CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("task-atlas"))
	{
		if (UnrealMcp::TryExecuteTaskAtlasTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("verification"))
	{
		// Verification is split across diagnostics and automation handlers; first matching tool branch wins.
		if (UnrealMcp::TryExecuteDiagnosticsTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
		if (UnrealMcp::TryExecuteAutomationTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
		if (UnrealMcp::TryExecutePieSmokeTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}

	TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
	StructuredContent->SetStringField(TEXT("action"), TEXT("handler_dispatch_failed"));
	StructuredContent->SetStringField(TEXT("handlerName"), ToolName);
	StructuredContent->SetStringField(TEXT("category"), Category);
	StructuredContent->SetStringField(TEXT("sourceFile"), HandlerEntry->SourceFile);
	StructuredContent->SetStringField(TEXT("reason"), TEXT("category_dispatcher_did_not_handle_registered_handler"));
	return UnrealMcp::MakeExecutionResult(
		FString::Printf(TEXT("Handler '%s' is registered under category '%s', but that category dispatcher did not handle it."), *ToolName, *Category),
		StructuredContent,
		true);
}
