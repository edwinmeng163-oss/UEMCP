#include "UnrealMcpTaskAtlasTools.h"

#include "UnrealMcpActivityLog.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpSession.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealMcp
{
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);
	TArray<TSharedPtr<FJsonValue>> MakeJsonStringArray(const TArray<FString>& Values);

	namespace
	{
		constexpr int32 MaxTaskEventRefs = 250;

		struct FTaskAtlasCluster
		{
			FTaskAtlasTaskRecord Record;
			TArray<FTaskAtlasEventRecord> Events;
		};

		struct FTaskAtlasPersistedChoices
		{
			FString Rating = TEXT("unrated");
			bool bPinned = false;
		};

		FString GetTaskRoot()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/Tasks")));
		}

		FString GetActivityLogRoot()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/ActivityLog")));
		}

		FString NormalizePathForJson(const FString& Path)
		{
			FString Normalized = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(Normalized);
			return Normalized;
		}

		FString SanitizeTaskToken(const FString& Value)
		{
			FString Result;
			for (const TCHAR Character : Value.TrimStartAndEnd().ToLower())
			{
				if (FChar::IsAlnum(Character) || Character == TEXT('-') || Character == TEXT('_') || Character == TEXT('.'))
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
				Result.ReplaceInline(TEXT("--"), TEXT("-"));
			}
			Result.RemoveFromStart(TEXT("-"));
			Result.RemoveFromEnd(TEXT("-"));
			return Result.IsEmpty() ? TEXT("task") : Result.Left(120);
		}

		bool IsSafeTaskId(const FString& TaskId)
		{
			const FString Trimmed = TaskId.TrimStartAndEnd();
			if (Trimmed.IsEmpty() || Trimmed.Len() > 180)
			{
				return false;
			}
			for (const TCHAR Character : Trimmed)
			{
				if (!FChar::IsAlnum(Character) && Character != TEXT('-') && Character != TEXT('_') && Character != TEXT('.'))
				{
					return false;
				}
			}
			return true;
		}

		FString MakeTimestampCompact(const FDateTime& Timestamp)
		{
			return Timestamp.ToString(TEXT("%Y%m%dT%H%M%SZ"));
		}

		FString MakeTaskId(const FString& SessionId, const FDateTime& Timestamp)
		{
			return FString::Printf(TEXT("%s-%s"), *SanitizeTaskToken(SessionId), *MakeTimestampCompact(Timestamp));
		}

		FString MakeTaskPath(const FString& TaskId)
		{
			return FPaths::Combine(GetTaskRoot(), TaskId + TEXT(".json"));
		}

		bool IsValidRating(const FString& Rating)
		{
			return Rating == TEXT("success") || Rating == TEXT("failed") || Rating == TEXT("unrated");
		}

		bool IsValidTaskListFilter(const FString& Filter)
		{
			return Filter == TEXT("all")
				|| Filter == TEXT("success")
				|| Filter == TEXT("failed")
				|| Filter == TEXT("unrated")
				|| Filter == TEXT("pinned");
		}

		bool IsValidAnnotateKind(const FString& Kind)
		{
			return Kind == TEXT("user_intent") || Kind == TEXT("ai_summary");
		}

		FString GetJsonStringField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
		{
			FString Value;
			if (Object.IsValid())
			{
				Object->TryGetStringField(FieldName, Value);
			}
			return Value;
		}

		bool GetJsonBoolField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, bool bDefault = false)
		{
			bool bValue = bDefault;
			if (Object.IsValid())
			{
				Object->TryGetBoolField(FieldName, bValue);
			}
			return bValue;
		}

		TSharedPtr<FJsonObject> GetObjectField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
		{
			const TSharedPtr<FJsonObject>* Child = nullptr;
			return Object.IsValid() && Object->TryGetObjectField(FieldName, Child) && Child && Child->IsValid() ? *Child : nullptr;
		}

		TSharedPtr<FJsonObject> GetEventPayload(const TSharedPtr<FJsonObject>& Event)
		{
			TSharedPtr<FJsonObject> Payload = GetObjectField(Event, TEXT("payload"));
			return Payload.IsValid() ? Payload : GetObjectField(Event, TEXT("details"));
		}

		FString GetEventKind(const TSharedPtr<FJsonObject>& Event)
		{
			FString EventKind = GetJsonStringField(Event, TEXT("eventKind"));
			if (EventKind.TrimStartAndEnd().IsEmpty())
			{
				EventKind = GetJsonStringField(Event, TEXT("eventType"));
			}
			if (EventKind == TEXT("mcp_tool_call") || EventKind == TEXT("mcp_tool_result"))
			{
				return TEXT("tool_call");
			}
			return EventKind.TrimStartAndEnd();
		}

		FString GetEventTimestamp(const TSharedPtr<FJsonObject>& Event)
		{
			FString Timestamp = GetJsonStringField(Event, TEXT("ts"));
			if (Timestamp.TrimStartAndEnd().IsEmpty())
			{
				Timestamp = GetJsonStringField(Event, TEXT("timestampUtc"));
			}
			return Timestamp.TrimStartAndEnd();
		}

		FString FirstSentenceLabel(const FString& Text)
		{
			const FString Trimmed = Text.TrimStartAndEnd();
			if (Trimmed.IsEmpty())
			{
				return FString();
			}

			int32 EndIndex = INDEX_NONE;
			for (int32 Index = 0; Index < Trimmed.Len(); ++Index)
			{
				const TCHAR Character = Trimmed[Index];
				if (Character == TEXT('.') || Character == TEXT('!') || Character == TEXT('?') || Character == TEXT('\n') || Character == TEXT('\r'))
				{
					EndIndex = Index + 1;
					break;
				}
			}

			const FString Sentence = EndIndex == INDEX_NONE ? Trimmed : Trimmed.Left(EndIndex).TrimStartAndEnd();
			return Sentence.Left(120);
		}

		FString MakeFallbackTaskLabel(const FDateTime& Timestamp)
		{
			return FString::Printf(TEXT("Session %s"), *Timestamp.ToString(TEXT("%Y-%m-%d %H:%M")));
		}

		TSharedPtr<FJsonObject> MakeErrorObject(const FString& Action, const FString& ErrorKind, const FString& Message)
		{
			TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
			ErrorObject->SetStringField(TEXT("action"), Action);
			ErrorObject->SetBoolField(TEXT("success"), false);
			ErrorObject->SetStringField(TEXT("errorKind"), ErrorKind);
			ErrorObject->SetStringField(TEXT("message"), Message);
			return ErrorObject;
		}

		FUnrealMcpExecutionResult MakeTaskAtlasError(const FString& Action, const FString& ErrorKind, const FString& Message)
		{
			return MakeExecutionResult(Message, MakeErrorObject(Action, ErrorKind, Message), true);
		}

		bool TryReadRequiredString(const FJsonObject& Arguments, const FString& FieldName, FString& OutValue, FUnrealMcpExecutionResult& OutErrorResult, const FString& Action)
		{
			const TSharedPtr<FJsonValue> FieldValue = Arguments.TryGetField(FieldName);
			if (!FieldValue.IsValid())
			{
				const FString Message = FString::Printf(TEXT("Missing required field '%s'."), *FieldName);
				OutErrorResult = MakeTaskAtlasError(Action, TEXT("MissingField"), Message);
				return false;
			}
			if (FieldValue->Type != EJson::String)
			{
				const FString Message = FString::Printf(TEXT("Field '%s' must be a string."), *FieldName);
				OutErrorResult = MakeTaskAtlasError(Action, TEXT("TypeMismatch"), Message);
				return false;
			}
			OutValue = FieldValue->AsString().TrimStartAndEnd();
			if (OutValue.IsEmpty())
			{
				const FString Message = FString::Printf(TEXT("Missing required field '%s'."), *FieldName);
				OutErrorResult = MakeTaskAtlasError(Action, TEXT("MissingField"), Message);
				return false;
			}
			return true;
		}

		bool TryReadOptionalString(const FJsonObject& Arguments, const FString& FieldName, FString& OutValue, FUnrealMcpExecutionResult& OutErrorResult, const FString& Action)
		{
			const TSharedPtr<FJsonValue> FieldValue = Arguments.TryGetField(FieldName);
			if (!FieldValue.IsValid() || FieldValue->Type == EJson::Null)
			{
				OutValue.Reset();
				return true;
			}
			if (FieldValue->Type != EJson::String)
			{
				const FString Message = FString::Printf(TEXT("Field '%s' must be a string."), *FieldName);
				OutErrorResult = MakeTaskAtlasError(Action, TEXT("TypeMismatch"), Message);
				return false;
			}
			OutValue = FieldValue->AsString().TrimStartAndEnd();
			return true;
		}

		bool TryReadRequiredBool(const FJsonObject& Arguments, const FString& FieldName, bool& bOutValue, FUnrealMcpExecutionResult& OutErrorResult, const FString& Action)
		{
			const TSharedPtr<FJsonValue> FieldValue = Arguments.TryGetField(FieldName);
			if (!FieldValue.IsValid())
			{
				const FString Message = FString::Printf(TEXT("Missing required field '%s'."), *FieldName);
				OutErrorResult = MakeTaskAtlasError(Action, TEXT("MissingField"), Message);
				return false;
			}
			if (FieldValue->Type != EJson::Boolean)
			{
				const FString Message = FString::Printf(TEXT("Field '%s' must be a boolean."), *FieldName);
				OutErrorResult = MakeTaskAtlasError(Action, TEXT("TypeMismatch"), Message);
				return false;
			}
			bOutValue = FieldValue->AsBool();
			return true;
		}

		bool TryReadOptionalLimit(const FJsonObject& Arguments, int32& OutLimit, FUnrealMcpExecutionResult& OutErrorResult, const FString& Action)
		{
			const TSharedPtr<FJsonValue> FieldValue = Arguments.TryGetField(TEXT("limit"));
			if (!FieldValue.IsValid() || FieldValue->Type == EJson::Null)
			{
				OutLimit = 50;
				return true;
			}
			if (FieldValue->Type != EJson::Number)
			{
				OutErrorResult = MakeTaskAtlasError(Action, TEXT("TypeMismatch"), TEXT("Field 'limit' must be a number."));
				return false;
			}
			OutLimit = FMath::Clamp(static_cast<int32>(FieldValue->AsNumber()), 1, 500);
			return true;
		}

		bool LoadJsonObjectFromString(const FString& JsonText, TSharedPtr<FJsonObject>& OutObject)
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
			return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
		}

		bool LoadJsonObjectFromFile(const FString& Path, TSharedPtr<FJsonObject>& OutObject)
		{
			FString JsonText;
			return FFileHelper::LoadFileToString(JsonText, *Path) && LoadJsonObjectFromString(JsonText, OutObject);
		}

		bool WriteJsonObjectToFile(const TSharedPtr<FJsonObject>& Object, const FString& Path, FString& OutFailureReason)
		{
			if (!Object.IsValid())
			{
				OutFailureReason = TEXT("Cannot write an invalid JSON object.");
				return false;
			}

			FString JsonText;
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonText);
			if (!FJsonSerializer::Serialize(Object.ToSharedRef(), Writer))
			{
				OutFailureReason = TEXT("Failed to serialize task JSON.");
				return false;
			}

			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			if (!FFileHelper::SaveStringToFile(JsonText, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to write '%s'."), *Path);
				return false;
			}
			return true;
		}

		TArray<TSharedPtr<FJsonValue>> MakeEventRefArray(const TArray<FTaskAtlasEventRecord>& Events)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			const int32 Count = FMath::Min(Events.Num(), MaxTaskEventRefs);
			Values.Reserve(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FTaskAtlasEventRecord& Event = Events[Index];
				TSharedPtr<FJsonObject> RefObject = MakeShared<FJsonObject>();
				RefObject->SetStringField(TEXT("ts"), Event.TimestampUtc);
				RefObject->SetStringField(TEXT("tool"), Event.ToolName);
				RefObject->SetBoolField(TEXT("isError"), Event.bIsError);
				Values.Add(MakeShared<FJsonValueObject>(RefObject));
			}
			return Values;
		}

		TSharedPtr<FJsonObject> TaskRecordToJson(const FTaskAtlasTaskRecord& Record, const TArray<FTaskAtlasEventRecord>& Events)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetNumberField(TEXT("schemaVersion"), 1.0);
			Object->SetStringField(TEXT("taskId"), Record.TaskId);
			Object->SetStringField(TEXT("label"), Record.Label);
			Object->SetStringField(TEXT("sessionId"), Record.SessionId);
			Object->SetArrayField(TEXT("criticalPath"), MakeJsonStringArray(Record.CriticalPath));
			Object->SetStringField(TEXT("rating"), Record.Rating);
			Object->SetBoolField(TEXT("pinned"), Record.bPinned);
			Object->SetStringField(TEXT("tStartUtc"), Record.TStartUtc);
			Object->SetStringField(TEXT("tEndUtc"), Record.TEndUtc);
			Object->SetNumberField(TEXT("eventCount"), Record.EventCount);
			Object->SetArrayField(TEXT("eventRefs"), MakeEventRefArray(Events));
			if (Record.UserIntentText.IsEmpty())
			{
				Object->SetField(TEXT("userIntentText"), MakeShared<FJsonValueNull>());
			}
			else
			{
				Object->SetStringField(TEXT("userIntentText"), Record.UserIntentText);
			}
			if (Record.AiSummaryText.IsEmpty())
			{
				Object->SetField(TEXT("aiSummaryText"), MakeShared<FJsonValueNull>());
			}
			else
			{
				Object->SetStringField(TEXT("aiSummaryText"), Record.AiSummaryText);
			}
			return Object;
		}

		void ApplyEventToCluster(FTaskAtlasCluster& Cluster, const FTaskAtlasEventRecord& Event)
		{
			Cluster.Events.Add(Event);
			Cluster.Record.TEndUtc = Event.TimestampUtc;
			Cluster.Record.EventCount = Cluster.Events.Num();
			if (!Event.ToolName.IsEmpty())
			{
				Cluster.Record.CriticalPath.AddUnique(Event.ToolName);
			}
			if (Event.EventKind == TEXT("user_intent") && Cluster.Record.UserIntentText.IsEmpty())
			{
				Cluster.Record.UserIntentText = Event.Content.Left(4000);
			}
			if (Event.EventKind == TEXT("ai_summary") && !Event.Content.IsEmpty())
			{
				Cluster.Record.AiSummaryText = Event.Content.Left(4000);
			}
			if (Event.EventKind == TEXT("ai_summary") && Event.bCompletionMarker)
			{
				Cluster.Record.bSawCompletionMarker = true;
			}
		}

		TArray<FTaskAtlasCluster> BuildTaskClusters(const TArray<FTaskAtlasEventRecord>& InputEvents, double GapSeconds)
		{
			TArray<FTaskAtlasEventRecord> SortedEvents = InputEvents;
			SortedEvents.Sort([](const FTaskAtlasEventRecord& Left, const FTaskAtlasEventRecord& Right)
			{
				if (!Left.SessionId.Equals(Right.SessionId, ESearchCase::CaseSensitive))
				{
					return Left.SessionId < Right.SessionId;
				}
				return Left.Timestamp < Right.Timestamp;
			});

			TArray<FTaskAtlasCluster> Clusters;
			for (const FTaskAtlasEventRecord& Event : SortedEvents)
			{
				const bool bNeedNewCluster =
					Clusters.Num() == 0
					|| !Clusters.Last().Record.SessionId.Equals(Event.SessionId, ESearchCase::CaseSensitive)
					|| (Clusters.Last().Record.bSawCompletionMarker
						&& (Event.Timestamp - Clusters.Last().Events.Last().Timestamp).GetTotalSeconds() >= GapSeconds);

				if (bNeedNewCluster)
				{
					FTaskAtlasCluster Cluster;
					Cluster.Record.SessionId = Event.SessionId;
					Cluster.Record.TStartUtc = Event.TimestampUtc;
					Cluster.Record.TEndUtc = Event.TimestampUtc;
					Cluster.Record.TaskId = MakeTaskId(Event.SessionId, Event.Timestamp);
					ApplyEventToCluster(Cluster, Event);
					Clusters.Add(MoveTemp(Cluster));
				}
				else
				{
					ApplyEventToCluster(Clusters.Last(), Event);
				}
			}

			for (FTaskAtlasCluster& Cluster : Clusters)
			{
				const FString IntentLabel = FirstSentenceLabel(Cluster.Record.UserIntentText);
				Cluster.Record.Label = IntentLabel.IsEmpty() ? MakeFallbackTaskLabel(Cluster.Events[0].Timestamp) : IntentLabel;
			}
			return Clusters;
		}

		bool TryParseActivityLine(const FString& Line, FTaskAtlasEventRecord& OutEvent)
		{
			TSharedPtr<FJsonObject> Object;
			if (!LoadJsonObjectFromString(Line.TrimStartAndEnd(), Object) || !Object.IsValid())
			{
				return false;
			}

			FString SessionId;
			if (!Object->TryGetStringField(TEXT("sessionId"), SessionId) || SessionId.TrimStartAndEnd().IsEmpty())
			{
				return false;
			}

			const FString TimestampText = GetEventTimestamp(Object);
			FDateTime Timestamp;
			if (TimestampText.IsEmpty() || !FDateTime::ParseIso8601(*TimestampText, Timestamp))
			{
				return false;
			}

			const FString EventKind = GetEventKind(Object);
			if (EventKind.IsEmpty())
			{
				return false;
			}

			const TSharedPtr<FJsonObject> Payload = GetEventPayload(Object);
			FString Content = GetJsonStringField(Payload, TEXT("content"));
			if (Content.IsEmpty())
			{
				Content = GetJsonStringField(Object, TEXT("summary"));
			}

			OutEvent.SessionId = SessionId.TrimStartAndEnd();
			OutEvent.TimestampUtc = TimestampText;
			OutEvent.Timestamp = Timestamp;
			OutEvent.EventKind = EventKind;
			OutEvent.ToolName = GetJsonStringField(Payload, TEXT("toolName"));
			OutEvent.bIsError = GetJsonBoolField(Payload, TEXT("isError"), false);
			OutEvent.Content = Content.TrimStartAndEnd();
			OutEvent.bCompletionMarker = GetJsonBoolField(Payload, TEXT("completionMarker"), false);
			return true;
		}

		void ReadPersistedChoices(TMap<FString, FTaskAtlasPersistedChoices>& OutChoices)
		{
			TArray<FString> TaskFiles;
			IFileManager::Get().FindFilesRecursive(TaskFiles, *GetTaskRoot(), TEXT("*.json"), true, false);
			for (const FString& TaskFile : TaskFiles)
			{
				TSharedPtr<FJsonObject> Object;
				if (!LoadJsonObjectFromFile(TaskFile, Object))
				{
					continue;
				}

				FString TaskId;
				if (!Object->TryGetStringField(TEXT("taskId"), TaskId) || TaskId.IsEmpty())
				{
					continue;
				}

				FTaskAtlasPersistedChoices Choices;
				FString Rating;
				if (Object->TryGetStringField(TEXT("rating"), Rating) && IsValidRating(Rating))
				{
					Choices.Rating = Rating;
				}
				Object->TryGetBoolField(TEXT("pinned"), Choices.bPinned);
				OutChoices.Add(TaskId, Choices);
			}
		}

		void ReadActivityLogEvents(
			TArray<FTaskAtlasEventRecord>& OutEvents,
			TMap<FString, FString>& OutRatingOverrides,
			TMap<FString, bool>& OutPinOverrides)
		{
			TArray<FString> ActivityFiles;
			IFileManager::Get().FindFilesRecursive(ActivityFiles, *GetActivityLogRoot(), TEXT("*.jsonl"), true, false);
			ActivityFiles.Sort();

			for (const FString& ActivityFile : ActivityFiles)
			{
				TArray<FString> Lines;
				if (!FFileHelper::LoadFileToStringArray(Lines, *ActivityFile))
				{
					continue;
				}

				for (const FString& Line : Lines)
				{
					if (Line.TrimStartAndEnd().IsEmpty())
					{
						continue;
					}

					FTaskAtlasEventRecord Event;
					if (!TryParseActivityLine(Line, Event))
					{
						continue;
					}

					if (Event.EventKind == TEXT("task_rating") || Event.EventKind == TEXT("task_pin_change"))
					{
						TSharedPtr<FJsonObject> Object;
						if (!LoadJsonObjectFromString(Line.TrimStartAndEnd(), Object))
						{
							continue;
						}
						const TSharedPtr<FJsonObject> Payload = GetEventPayload(Object);
						const FString TaskId = GetJsonStringField(Payload, TEXT("taskId"));
						if (!IsSafeTaskId(TaskId))
						{
							continue;
						}
						if (Event.EventKind == TEXT("task_rating"))
						{
							const FString Rating = GetJsonStringField(Payload, TEXT("rating"));
							if (IsValidRating(Rating))
							{
								OutRatingOverrides.Add(TaskId, Rating);
							}
						}
						else
						{
							const TSharedPtr<FJsonValue> PinnedValue = Payload.IsValid() ? Payload->TryGetField(TEXT("pinned")) : nullptr;
							if (PinnedValue.IsValid() && PinnedValue->Type == EJson::Boolean)
							{
								OutPinOverrides.Add(TaskId, PinnedValue->AsBool());
							}
						}
						continue;
					}

					OutEvents.Add(Event);
				}
			}
		}

		bool RefreshTaskFiles(FString& OutFailureReason)
		{
			TMap<FString, FTaskAtlasPersistedChoices> PersistedChoices;
			ReadPersistedChoices(PersistedChoices);

			TArray<FTaskAtlasEventRecord> Events;
			TMap<FString, FString> RatingOverrides;
			TMap<FString, bool> PinOverrides;
			ReadActivityLogEvents(Events, RatingOverrides, PinOverrides);

			TArray<FTaskAtlasCluster> Clusters = BuildTaskClusters(Events, TaskAtlasClusterGapSeconds);
			IFileManager::Get().MakeDirectory(*GetTaskRoot(), true);
			for (FTaskAtlasCluster& Cluster : Clusters)
			{
				if (const FTaskAtlasPersistedChoices* Choices = PersistedChoices.Find(Cluster.Record.TaskId))
				{
					Cluster.Record.Rating = Choices->Rating;
					Cluster.Record.bPinned = Choices->bPinned;
				}
				if (const FString* Rating = RatingOverrides.Find(Cluster.Record.TaskId))
				{
					Cluster.Record.Rating = *Rating;
				}
				if (const bool* bPinned = PinOverrides.Find(Cluster.Record.TaskId))
				{
					Cluster.Record.bPinned = *bPinned;
				}

				const TSharedPtr<FJsonObject> TaskObject = TaskRecordToJson(Cluster.Record, Cluster.Events);
				if (!WriteJsonObjectToFile(TaskObject, MakeTaskPath(Cluster.Record.TaskId), OutFailureReason))
				{
					return false;
				}
			}
			return true;
		}

		TArray<TSharedPtr<FJsonObject>> LoadAllTaskObjects()
		{
			TArray<FString> TaskFiles;
			IFileManager::Get().FindFilesRecursive(TaskFiles, *GetTaskRoot(), TEXT("*.json"), true, false);
			TaskFiles.Sort();

			TArray<TSharedPtr<FJsonObject>> Tasks;
			for (const FString& TaskFile : TaskFiles)
			{
				TSharedPtr<FJsonObject> Object;
				if (LoadJsonObjectFromFile(TaskFile, Object) && Object.IsValid())
				{
					Tasks.Add(Object);
				}
			}
			return Tasks;
		}

		bool TaskMatchesFilter(const TSharedPtr<FJsonObject>& Task, const FString& Filter)
		{
			if (Filter == TEXT("all"))
			{
				return true;
			}
			if (Filter == TEXT("pinned"))
			{
				return GetJsonBoolField(Task, TEXT("pinned"), false);
			}
			return GetJsonStringField(Task, TEXT("rating")) == Filter;
		}

		TArray<TSharedPtr<FJsonValue>> TaskObjectsToValues(const TArray<TSharedPtr<FJsonObject>>& Tasks)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			Values.Reserve(Tasks.Num());
			for (const TSharedPtr<FJsonObject>& Task : Tasks)
			{
				Values.Add(MakeShared<FJsonValueObject>(Task));
			}
			return Values;
		}

		bool TryLoadTaskById(const FString& TaskId, TSharedPtr<FJsonObject>& OutTask, FString& OutPath, FString& OutFailureReason)
		{
			if (!IsSafeTaskId(TaskId))
			{
				OutFailureReason = TEXT("taskId must be a filename-safe Task Atlas id.");
				return false;
			}

			OutPath = MakeTaskPath(TaskId);
			if (!FPaths::FileExists(OutPath) || !LoadJsonObjectFromFile(OutPath, OutTask) || !OutTask.IsValid())
			{
				OutFailureReason = FString::Printf(TEXT("Task '%s' was not found."), *TaskId);
				return false;
			}
			return true;
		}

		TArray<FString> GetCriticalPathFromTask(const TSharedPtr<FJsonObject>& Task)
		{
			TArray<FString> CriticalPath;
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (Task.IsValid() && Task->TryGetArrayField(TEXT("criticalPath"), Values) && Values)
			{
				for (const TSharedPtr<FJsonValue>& Value : *Values)
				{
					if (Value.IsValid() && Value->Type == EJson::String)
					{
						CriticalPath.Add(Value->AsString());
					}
				}
			}
			return CriticalPath;
		}

		FUnrealMcpExecutionResult ActivityLogAnnotate(const FJsonObject& Arguments)
		{
			const FString Action = TEXT("activity_log_annotate");
			FUnrealMcpExecutionResult ErrorResult;
			FString Kind;
			if (!TryReadRequiredString(Arguments, TEXT("kind"), Kind, ErrorResult, Action))
			{
				return ErrorResult;
			}
			if (!IsValidAnnotateKind(Kind))
			{
				return MakeTaskAtlasError(Action, TEXT("InvalidEnum"), TEXT("Field 'kind' must be 'user_intent' or 'ai_summary'."));
			}

			FString Content;
			if (!TryReadRequiredString(Arguments, TEXT("content"), Content, ErrorResult, Action))
			{
				return ErrorResult;
			}

			FString RequestedSessionId;
			if (!TryReadOptionalString(Arguments, TEXT("sessionId"), RequestedSessionId, ErrorResult, Action))
			{
				return ErrorResult;
			}

			const FString EffectiveSessionId = RequestedSessionId.IsEmpty() ? GetLaunchSessionId() : RequestedSessionId;
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("content"), Content.Left(8000));
			if (Kind == TEXT("user_intent") && !RequestedSessionId.IsEmpty())
			{
				Payload->SetStringField(TEXT("sessionRefId"), RequestedSessionId);
			}
			if (Kind == TEXT("ai_summary"))
			{
				Payload->SetBoolField(TEXT("completionMarker"), true);
			}

			FActivityLogEvent Event;
			Event.EventKind = Kind;
			Event.Summary = FString::Printf(TEXT("%s: %s"), Kind == TEXT("user_intent") ? TEXT("User intent") : TEXT("AI summary"), *Content.Left(1800));
			Event.Payload = Payload;

			FString FailureReason;
			if (!TryWriteActivityEventForSession(EffectiveSessionId, Event, FailureReason))
			{
				return MakeTaskAtlasError(Action, TEXT("WriteFailed"), FailureReason);
			}
			if (!RefreshTaskFiles(FailureReason))
			{
				return MakeTaskAtlasError(Action, TEXT("RefreshFailed"), FailureReason);
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), Action);
			StructuredContent->SetBoolField(TEXT("success"), true);
			StructuredContent->SetStringField(TEXT("eventKind"), Kind);
			StructuredContent->SetStringField(TEXT("sessionId"), EffectiveSessionId);
			StructuredContent->SetStringField(TEXT("taskRoot"), NormalizePathForJson(GetTaskRoot()));
			return MakeExecutionResult(FString::Printf(TEXT("Annotated ActivityLog with %s."), *Kind), StructuredContent, false);
		}

		FUnrealMcpExecutionResult TaskList(const FJsonObject& Arguments)
		{
			const FString Action = TEXT("task_list");
			FUnrealMcpExecutionResult ErrorResult;
			FString Filter;
			if (!TryReadOptionalString(Arguments, TEXT("filter"), Filter, ErrorResult, Action))
			{
				return ErrorResult;
			}
			if (Filter.IsEmpty())
			{
				Filter = TEXT("all");
			}
			if (!IsValidTaskListFilter(Filter))
			{
				return MakeTaskAtlasError(Action, TEXT("InvalidEnum"), TEXT("Field 'filter' must be one of all, success, failed, unrated, or pinned."));
			}

			int32 Limit = 50;
			if (!TryReadOptionalLimit(Arguments, Limit, ErrorResult, Action))
			{
				return ErrorResult;
			}

			FString FailureReason;
			if (!RefreshTaskFiles(FailureReason))
			{
				return MakeTaskAtlasError(Action, TEXT("RefreshFailed"), FailureReason);
			}

			TArray<TSharedPtr<FJsonObject>> Tasks = LoadAllTaskObjects();
			Tasks.Sort([](const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right)
			{
				return GetJsonStringField(Left, TEXT("tEndUtc")) > GetJsonStringField(Right, TEXT("tEndUtc"));
			});

			TArray<TSharedPtr<FJsonObject>> FilteredTasks;
			for (const TSharedPtr<FJsonObject>& Task : Tasks)
			{
				if (TaskMatchesFilter(Task, Filter))
				{
					FilteredTasks.Add(Task);
					if (FilteredTasks.Num() >= Limit)
					{
						break;
					}
				}
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), Action);
			StructuredContent->SetBoolField(TEXT("success"), true);
			StructuredContent->SetStringField(TEXT("filter"), Filter);
			StructuredContent->SetNumberField(TEXT("limit"), Limit);
			StructuredContent->SetNumberField(TEXT("count"), FilteredTasks.Num());
			StructuredContent->SetNumberField(TEXT("totalCount"), Tasks.Num());
			StructuredContent->SetStringField(TEXT("taskRoot"), NormalizePathForJson(GetTaskRoot()));
			StructuredContent->SetArrayField(TEXT("tasks"), TaskObjectsToValues(FilteredTasks));
			return MakeExecutionResult(FString::Printf(TEXT("Listed %d Task Atlas task(s)."), FilteredTasks.Num()), StructuredContent, false);
		}

		FUnrealMcpExecutionResult TaskDescribe(const FJsonObject& Arguments)
		{
			const FString Action = TEXT("task_describe");
			FUnrealMcpExecutionResult ErrorResult;
			FString TaskId;
			if (!TryReadRequiredString(Arguments, TEXT("taskId"), TaskId, ErrorResult, Action))
			{
				return ErrorResult;
			}

			FString FailureReason;
			if (!RefreshTaskFiles(FailureReason))
			{
				return MakeTaskAtlasError(Action, TEXT("RefreshFailed"), FailureReason);
			}

			TSharedPtr<FJsonObject> Task;
			FString TaskPath;
			if (!TryLoadTaskById(TaskId, Task, TaskPath, FailureReason))
			{
				return MakeTaskAtlasError(Action, TEXT("NotFound"), FailureReason);
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), Action);
			StructuredContent->SetBoolField(TEXT("success"), true);
			StructuredContent->SetStringField(TEXT("taskId"), TaskId);
			StructuredContent->SetStringField(TEXT("taskPath"), NormalizePathForJson(TaskPath));
			StructuredContent->SetStringField(TEXT("taskRoot"), NormalizePathForJson(GetTaskRoot()));
			StructuredContent->SetObjectField(TEXT("task"), Task);
			return MakeExecutionResult(FString::Printf(TEXT("Described Task Atlas task %s."), *TaskId), StructuredContent, false);
		}

		FUnrealMcpExecutionResult TaskRate(const FJsonObject& Arguments)
		{
			const FString Action = TEXT("task_rate");
			FUnrealMcpExecutionResult ErrorResult;
			FString TaskId;
			if (!TryReadRequiredString(Arguments, TEXT("taskId"), TaskId, ErrorResult, Action))
			{
				return ErrorResult;
			}
			FString Rating;
			if (!TryReadRequiredString(Arguments, TEXT("rating"), Rating, ErrorResult, Action))
			{
				return ErrorResult;
			}
			if (!IsValidRating(Rating))
			{
				return MakeTaskAtlasError(Action, TEXT("InvalidEnum"), TEXT("Field 'rating' must be 'success', 'failed', or 'unrated'."));
			}

			FString FailureReason;
			if (!RefreshTaskFiles(FailureReason))
			{
				return MakeTaskAtlasError(Action, TEXT("RefreshFailed"), FailureReason);
			}

			TSharedPtr<FJsonObject> Task;
			FString TaskPath;
			if (!TryLoadTaskById(TaskId, Task, TaskPath, FailureReason))
			{
				return MakeTaskAtlasError(Action, TEXT("NotFound"), FailureReason);
			}
			Task->SetStringField(TEXT("rating"), Rating);
			if (!WriteJsonObjectToFile(Task, TaskPath, FailureReason))
			{
				return MakeTaskAtlasError(Action, TEXT("WriteFailed"), FailureReason);
			}

			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("taskId"), TaskId);
			Payload->SetStringField(TEXT("rating"), Rating);
			FActivityLogEvent Event;
			Event.EventKind = TEXT("task_rating");
			Event.Summary = FString::Printf(TEXT("Task %s rated %s."), *TaskId, *Rating);
			Event.Payload = Payload;
			if (!TryWriteActivityEvent(Event, FailureReason))
			{
				return MakeTaskAtlasError(Action, TEXT("ActivityLogWriteFailed"), FailureReason);
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), Action);
			StructuredContent->SetBoolField(TEXT("success"), true);
			StructuredContent->SetStringField(TEXT("taskId"), TaskId);
			StructuredContent->SetStringField(TEXT("rating"), Rating);
			StructuredContent->SetStringField(TEXT("taskPath"), NormalizePathForJson(TaskPath));
			return MakeExecutionResult(FString::Printf(TEXT("Rated Task Atlas task %s as %s."), *TaskId, *Rating), StructuredContent, false);
		}

		FUnrealMcpExecutionResult TaskPin(const FJsonObject& Arguments)
		{
			const FString Action = TEXT("task_pin");
			FUnrealMcpExecutionResult ErrorResult;
			FString TaskId;
			if (!TryReadRequiredString(Arguments, TEXT("taskId"), TaskId, ErrorResult, Action))
			{
				return ErrorResult;
			}
			bool bPinned = false;
			if (!TryReadRequiredBool(Arguments, TEXT("pinned"), bPinned, ErrorResult, Action))
			{
				return ErrorResult;
			}

			FString FailureReason;
			if (!RefreshTaskFiles(FailureReason))
			{
				return MakeTaskAtlasError(Action, TEXT("RefreshFailed"), FailureReason);
			}

			TSharedPtr<FJsonObject> Task;
			FString TaskPath;
			if (!TryLoadTaskById(TaskId, Task, TaskPath, FailureReason))
			{
				return MakeTaskAtlasError(Action, TEXT("NotFound"), FailureReason);
			}

			const TArray<FString> CriticalPath = GetCriticalPathFromTask(Task);
			Task->SetBoolField(TEXT("pinned"), bPinned);
			if (!WriteJsonObjectToFile(Task, TaskPath, FailureReason))
			{
				return MakeTaskAtlasError(Action, TEXT("WriteFailed"), FailureReason);
			}

			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("taskId"), TaskId);
			Payload->SetBoolField(TEXT("pinned"), bPinned);
			Payload->SetArrayField(TEXT("criticalPathSnapshot"), MakeJsonStringArray(CriticalPath));
			FActivityLogEvent Event;
			Event.EventKind = TEXT("task_pin_change");
			Event.Summary = FString::Printf(TEXT("Task %s pin state changed to %s."), *TaskId, bPinned ? TEXT("true") : TEXT("false"));
			Event.Payload = Payload;
			if (!TryWriteActivityEvent(Event, FailureReason))
			{
				return MakeTaskAtlasError(Action, TEXT("ActivityLogWriteFailed"), FailureReason);
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), Action);
			StructuredContent->SetBoolField(TEXT("success"), true);
			StructuredContent->SetStringField(TEXT("taskId"), TaskId);
			StructuredContent->SetBoolField(TEXT("pinned"), bPinned);
			StructuredContent->SetArrayField(TEXT("criticalPathSnapshot"), MakeJsonStringArray(CriticalPath));
			StructuredContent->SetStringField(TEXT("taskPath"), NormalizePathForJson(TaskPath));
			return MakeExecutionResult(FString::Printf(TEXT("%s Task Atlas task %s."), bPinned ? TEXT("Pinned") : TEXT("Unpinned"), *TaskId), StructuredContent, false);
		}
	}

	TArray<FTaskAtlasTaskRecord> ClusterTaskAtlasEventsForTests(const TArray<FTaskAtlasEventRecord>& Events, double GapSeconds)
	{
		TArray<FTaskAtlasTaskRecord> Records;
		for (const FTaskAtlasCluster& Cluster : BuildTaskClusters(Events, GapSeconds))
		{
			Records.Add(Cluster.Record);
		}
		return Records;
	}

	bool TryExecuteTaskAtlasTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult)
	{
		if (ToolName == TEXT("unreal.activity_log_annotate"))
		{
			OutResult = ActivityLogAnnotate(Arguments);
			return true;
		}
		if (ToolName == TEXT("unreal.task_list"))
		{
			OutResult = TaskList(Arguments);
			return true;
		}
		if (ToolName == TEXT("unreal.task_describe"))
		{
			OutResult = TaskDescribe(Arguments);
			return true;
		}
		if (ToolName == TEXT("unreal.task_rate"))
		{
			OutResult = TaskRate(Arguments);
			return true;
		}
		if (ToolName == TEXT("unreal.task_pin"))
		{
			OutResult = TaskPin(Arguments);
			return true;
		}
		return false;
	}
}
