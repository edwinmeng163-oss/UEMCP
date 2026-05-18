#include "UnrealMcpTaskLabelBackfillTool.h"

#include "UnrealMcpModule.h"
#include "UnrealMcpSettings.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HttpManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include <atomic>

namespace UnrealMcp
{
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);
	FString JsonObjectToString(const TSharedPtr<FJsonObject>& JsonObject);
	void ApplyAiHttpTimeoutOverrides(const UUnrealMcpSettings& Settings);

	namespace
	{
		constexpr int32 DefaultBackfillLimit = 25;
		constexpr int32 MaxBackfillLimit = 500;
		const TCHAR* PreferredBackfillModel = TEXT("claude-haiku-4-5");
		const TCHAR* FallbackBackfillModel = TEXT("claude-sonnet-4-6");

		struct FBackfillArgs
		{
			FString SessionIdFilter;
			int32 Limit = DefaultBackfillLimit;
			bool bForce = false;
			bool bDryRun = false;
		};

		struct FBackfillTask
		{
			FString TaskId;
			FString TaskPath;
			FString Label;
			FString SessionId;
			FString UserIntentText;
			bool bPinned = false;
			TArray<FString> CriticalPath;
			TSharedPtr<FJsonObject> Object;
		};

		struct FLoadedTaskObject
		{
			TSharedPtr<FJsonObject> Object;
			FString Path;
		};

		struct FBackfillSkipReason
		{
			FString TaskId;
			FString Reason;
		};

		struct FAnthropicLabelResponse
		{
			bool bSucceeded = false;
			int32 StatusCode = 0;
			FString Text;
			FString FailureReason;
			FString Model;
		};

		struct FHttpLabelRequestState
		{
			FCriticalSection Mutex;
			std::atomic<bool> bDone{false};
			bool bSucceeded = false;
			int32 StatusCode = 0;
			FString BodyString;
		};

		FString GetTaskRoot()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/Tasks")));
		}

		FString NormalizePathForJson(const FString& Path)
		{
			FString Normalized = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(Normalized);
			return Normalized;
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

		FUnrealMcpExecutionResult MakeBackfillError(const FString& ErrorKind, const FString& Message)
		{
			return MakeExecutionResult(Message, MakeErrorObject(TEXT("task_label_backfill"), ErrorKind, Message), true);
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

		bool WriteJsonObjectToFile(const TSharedPtr<FJsonObject>& Object, const FString& Path)
		{
			if (!Object.IsValid())
			{
				return false;
			}

			FString JsonText;
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonText);
			if (!FJsonSerializer::Serialize(Object.ToSharedRef(), Writer))
			{
				return false;
			}

			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			return FFileHelper::SaveStringToFile(JsonText, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		FString GetJsonStringField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
		{
			FString Value;
			if (Object.IsValid())
			{
				Object->TryGetStringField(FieldName, Value);
			}
			return Value.TrimStartAndEnd();
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
						const FString ToolName = Value->AsString().TrimStartAndEnd();
						if (!ToolName.IsEmpty())
						{
							CriticalPath.Add(ToolName);
						}
					}
				}
			}
			return CriticalPath;
		}

		bool IsDigitAt(const FString& Text, int32 Index)
		{
			return Text.IsValidIndex(Index) && FChar::IsDigit(Text[Index]);
		}

		bool IsSessionPlaceholderLabel(const FString& Label)
		{
			const FString Trimmed = Label.TrimStartAndEnd();
			if (Trimmed.Len() != 24 || !Trimmed.StartsWith(TEXT("Session "), ESearchCase::CaseSensitive))
			{
				return false;
			}

			const bool bSeparatorsOk =
				Trimmed[12] == TEXT('-')
				&& Trimmed[15] == TEXT('-')
				&& Trimmed[18] == TEXT(' ')
				&& Trimmed[21] == TEXT(':');
			if (!bSeparatorsOk)
			{
				return false;
			}

			const int32 DigitIndices[] = { 8, 9, 10, 11, 13, 14, 16, 17, 19, 20, 22, 23 };
			for (const int32 Index : DigitIndices)
			{
				if (!IsDigitAt(Trimmed, Index))
				{
					return false;
				}
			}
			return true;
		}

		void AddSkip(TArray<FBackfillSkipReason>& SkippedReasons, const FString& TaskId, const FString& Reason)
		{
			FBackfillSkipReason Skip;
			Skip.TaskId = TaskId;
			Skip.Reason = Reason;
			SkippedReasons.Add(MoveTemp(Skip));
		}

		TArray<TSharedPtr<FJsonValue>> MakeSkippedReasonValues(const TArray<FBackfillSkipReason>& SkippedReasons)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			Values.Reserve(SkippedReasons.Num());
			for (const FBackfillSkipReason& Skip : SkippedReasons)
			{
				TSharedPtr<FJsonObject> SkipObject = MakeShared<FJsonObject>();
				SkipObject->SetStringField(TEXT("taskId"), Skip.TaskId);
				SkipObject->SetStringField(TEXT("reason"), Skip.Reason);
				Values.Add(MakeShared<FJsonValueObject>(SkipObject));
			}
			return Values;
		}

		FUnrealMcpExecutionResult MakeBackfillResult(
			const FBackfillArgs& Args,
			int32 ProcessedCount,
			int32 UpdatedCount,
			const TArray<FBackfillSkipReason>& SkippedReasons,
			const FString& Text)
		{
			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), TEXT("task_label_backfill"));
			StructuredContent->SetBoolField(TEXT("success"), true);
			StructuredContent->SetNumberField(TEXT("processedCount"), ProcessedCount);
			StructuredContent->SetNumberField(TEXT("updatedCount"), UpdatedCount);
			StructuredContent->SetNumberField(TEXT("skippedCount"), SkippedReasons.Num());
			StructuredContent->SetArrayField(TEXT("skippedReasons"), MakeSkippedReasonValues(SkippedReasons));
			StructuredContent->SetStringField(TEXT("sessionIdFilter"), Args.SessionIdFilter);
			StructuredContent->SetNumberField(TEXT("limit"), Args.Limit);
			StructuredContent->SetBoolField(TEXT("force"), Args.bForce);
			StructuredContent->SetBoolField(TEXT("dryRun"), Args.bDryRun);
			StructuredContent->SetStringField(TEXT("taskRoot"), NormalizePathForJson(GetTaskRoot()));
			return MakeExecutionResult(Text, StructuredContent, false);
		}

		bool TryReadOptionalString(const FJsonObject& Arguments, const FString& FieldName, FString& OutValue, FUnrealMcpExecutionResult& OutErrorResult)
		{
			const TSharedPtr<FJsonValue> FieldValue = Arguments.TryGetField(FieldName);
			if (!FieldValue.IsValid() || FieldValue->Type == EJson::Null)
			{
				OutValue.Reset();
				return true;
			}
			if (FieldValue->Type != EJson::String)
			{
				OutErrorResult = MakeBackfillError(TEXT("TypeMismatch"), FString::Printf(TEXT("Field '%s' must be a string."), *FieldName));
				return false;
			}
			OutValue = FieldValue->AsString().TrimStartAndEnd();
			return true;
		}

		bool TryReadOptionalBool(const FJsonObject& Arguments, const FString& FieldName, bool& bOutValue, FUnrealMcpExecutionResult& OutErrorResult)
		{
			const TSharedPtr<FJsonValue> FieldValue = Arguments.TryGetField(FieldName);
			if (!FieldValue.IsValid() || FieldValue->Type == EJson::Null)
			{
				return true;
			}
			if (FieldValue->Type != EJson::Boolean)
			{
				OutErrorResult = MakeBackfillError(TEXT("TypeMismatch"), FString::Printf(TEXT("Field '%s' must be a boolean."), *FieldName));
				return false;
			}
			bOutValue = FieldValue->AsBool();
			return true;
		}

		bool TryReadOptionalLimit(const FJsonObject& Arguments, int32& OutLimit, FUnrealMcpExecutionResult& OutErrorResult)
		{
			const TSharedPtr<FJsonValue> FieldValue = Arguments.TryGetField(TEXT("limit"));
			if (!FieldValue.IsValid() || FieldValue->Type == EJson::Null)
			{
				OutLimit = DefaultBackfillLimit;
				return true;
			}
			if (FieldValue->Type != EJson::Number)
			{
				OutErrorResult = MakeBackfillError(TEXT("TypeMismatch"), TEXT("Field 'limit' must be a number."));
				return false;
			}

			const double LimitNumber = FieldValue->AsNumber();
			const double RoundedLimit = FMath::FloorToDouble(LimitNumber + 0.5);
			if (!FMath::IsNearlyEqual(LimitNumber, RoundedLimit))
			{
				OutErrorResult = MakeBackfillError(TEXT("TypeMismatch"), TEXT("Field 'limit' must be an integer."));
				return false;
			}
			OutLimit = static_cast<int32>(FMath::Clamp(RoundedLimit, 1.0, static_cast<double>(MaxBackfillLimit)));
			return true;
		}

		bool TryReadArgs(const FJsonObject& Arguments, FBackfillArgs& OutArgs, FUnrealMcpExecutionResult& OutErrorResult)
		{
			if (!TryReadOptionalString(Arguments, TEXT("sessionId"), OutArgs.SessionIdFilter, OutErrorResult))
			{
				return false;
			}
			if (!TryReadOptionalLimit(Arguments, OutArgs.Limit, OutErrorResult))
			{
				return false;
			}
			if (!TryReadOptionalBool(Arguments, TEXT("force"), OutArgs.bForce, OutErrorResult))
			{
				return false;
			}
			if (!TryReadOptionalBool(Arguments, TEXT("dryRun"), OutArgs.bDryRun, OutErrorResult))
			{
				return false;
			}
			return true;
		}

		TArray<FLoadedTaskObject> LoadAllTaskObjects()
		{
			TArray<FString> TaskFiles;
			IFileManager::Get().FindFilesRecursive(TaskFiles, *GetTaskRoot(), TEXT("*.json"), true, false);
			TaskFiles.Sort();

			TArray<FLoadedTaskObject> Tasks;
			for (const FString& TaskFile : TaskFiles)
			{
				TSharedPtr<FJsonObject> Object;
				if (LoadJsonObjectFromFile(TaskFile, Object) && Object.IsValid())
				{
					FLoadedTaskObject LoadedTask;
					LoadedTask.Object = Object;
					LoadedTask.Path = TaskFile;
					Tasks.Add(MoveTemp(LoadedTask));
				}
			}

			Tasks.Sort([](const FLoadedTaskObject& Left, const FLoadedTaskObject& Right)
			{
				return GetJsonStringField(Left.Object, TEXT("tEndUtc")) > GetJsonStringField(Right.Object, TEXT("tEndUtc"));
			});
			return Tasks;
		}

		TArray<FBackfillTask> SelectBackfillTasks(const FBackfillArgs& Args, TArray<FBackfillSkipReason>& SkippedReasons)
		{
			const TArray<FLoadedTaskObject> Tasks = LoadAllTaskObjects();
			TArray<FBackfillTask> SelectedTasks;
			SelectedTasks.Reserve(FMath::Min(Args.Limit, Tasks.Num()));

			for (const FLoadedTaskObject& LoadedTask : Tasks)
			{
				const TSharedPtr<FJsonObject>& Task = LoadedTask.Object;
				const FString TaskId = GetJsonStringField(Task, TEXT("taskId"));
				const FString SessionId = GetJsonStringField(Task, TEXT("sessionId"));
				const FString Label = GetJsonStringField(Task, TEXT("label"));
				const FString UserIntentText = GetJsonStringField(Task, TEXT("userIntentText"));
				if (!Args.SessionIdFilter.IsEmpty() && !SessionId.Equals(Args.SessionIdFilter, ESearchCase::CaseSensitive))
				{
					continue;
				}

				const bool bInitialCandidate = Args.bForce || (UserIntentText.IsEmpty() && Label.StartsWith(TEXT("Session "), ESearchCase::CaseSensitive));
				if (!bInitialCandidate)
				{
					continue;
				}

				if (GetJsonBoolField(Task, TEXT("pinned"), false))
				{
					AddSkip(SkippedReasons, TaskId, TEXT("pinned"));
					continue;
				}
				if (!UserIntentText.IsEmpty())
				{
					AddSkip(SkippedReasons, TaskId, TEXT("user_intent_present"));
					continue;
				}
				if (!IsSessionPlaceholderLabel(Label))
				{
					AddSkip(SkippedReasons, TaskId, TEXT("user_edited_label"));
					continue;
				}

				TArray<FString> CriticalPath = GetCriticalPathFromTask(Task);
				if (CriticalPath.Num() == 0)
				{
					AddSkip(SkippedReasons, TaskId, TEXT("empty_critical_path"));
					continue;
				}

				FBackfillTask BackfillTask;
				BackfillTask.TaskId = TaskId;
				BackfillTask.TaskPath = LoadedTask.Path;
				BackfillTask.Label = Label;
				BackfillTask.SessionId = SessionId;
				BackfillTask.UserIntentText = UserIntentText;
				BackfillTask.bPinned = false;
				BackfillTask.CriticalPath = MoveTemp(CriticalPath);
				BackfillTask.Object = Task;
				SelectedTasks.Add(MoveTemp(BackfillTask));
				if (SelectedTasks.Num() >= Args.Limit)
				{
					break;
				}
			}
			return SelectedTasks;
		}

		void RedactHomePrefix(FString& Text, const FString& Prefix)
		{
			const FString Replacement = TEXT("<HOME>/");
			int32 SearchStart = 0;
			while (SearchStart < Text.Len())
			{
				const int32 PrefixIndex = Text.Find(Prefix, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchStart);
				if (PrefixIndex == INDEX_NONE)
				{
					break;
				}

				const int32 NameStart = PrefixIndex + Prefix.Len();
				const int32 SlashIndex = Text.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameStart);
				if (SlashIndex == INDEX_NONE)
				{
					Text = Text.Left(PrefixIndex) + Replacement;
					SearchStart = PrefixIndex + Replacement.Len();
					continue;
				}
				if (SlashIndex == NameStart)
				{
					SearchStart = NameStart + 1;
					continue;
				}

				Text = Text.Left(PrefixIndex) + Replacement + Text.Mid(SlashIndex + 1);
				SearchStart = PrefixIndex + Replacement.Len();
			}
		}

		bool IsPathDelimiter(TCHAR Character)
		{
			return Character == TEXT(',')
				|| Character == TEXT(';')
				|| Character == TEXT('"')
				|| Character == TEXT('\'')
				|| Character == TEXT('[')
				|| Character == TEXT(']')
				|| Character == TEXT('{')
				|| Character == TEXT('}')
				|| Character == TEXT('\n')
				|| Character == TEXT('\r')
				|| Character == TEXT('\t');
		}

		bool LooksLikeWindowsAbsolutePathStart(const FString& Text, int32 Index)
		{
			return Text.IsValidIndex(Index + 2)
				&& FChar::IsAlpha(Text[Index])
				&& Text[Index + 1] == TEXT(':')
				&& (Text[Index + 2] == TEXT('\\') || Text[Index + 2] == TEXT('/'));
		}

		FString RedactAbsolutePathTokens(const FString& Input)
		{
			FString Result;
			int32 Index = 0;
			while (Index < Input.Len())
			{
				const bool bUnixPathStart = Input[Index] == TEXT('/') && !Input.Mid(Index, 2).Equals(TEXT("//"), ESearchCase::CaseSensitive);
				const bool bWindowsPathStart = LooksLikeWindowsAbsolutePathStart(Input, Index);
				if (!bUnixPathStart && !bWindowsPathStart)
				{
					Result.AppendChar(Input[Index]);
					++Index;
					continue;
				}

				int32 End = Index;
				while (End < Input.Len() && !IsPathDelimiter(Input[End]))
				{
					++End;
				}
				Result += TEXT("<ABS_PATH>");
				Index = End;
			}
			return Result;
		}

		FString SanitizePromptInput(FString Text)
		{
			RedactHomePrefix(Text, TEXT("/Users/"));
			RedactHomePrefix(Text, TEXT("/home/"));
			return RedactAbsolutePathTokens(Text);
		}

		FString BuildPromptForTask(const FBackfillTask& Task)
		{
			const FString ToolsText = SanitizePromptInput(FString::Join(Task.CriticalPath, TEXT(", ")));
			return FString::Printf(
				TEXT("Given these MCP tool calls, infer a 3-7 word task label describing the user's likely goal. Tools: [%s]. Return JSON: {\"label\":\"short task label\",\"confidence\":0.0-1.0}"),
				*ToolsText);
		}

		bool IsUsableAnthropicProvider(const FAiProviderConfig& Provider)
		{
			return Provider.Kind == EAiProviderKind::AnthropicMessages
				&& !Provider.ApiKey.TrimStartAndEnd().IsEmpty()
				&& !Provider.BaseUrl.TrimStartAndEnd().IsEmpty();
		}

		bool TrySelectAnthropicProvider(const UUnrealMcpSettings& Settings, FAiProviderConfig& OutProvider)
		{
			if (!Settings.bEnableAiAssistant)
			{
				return false;
			}

			if (const FAiProviderConfig* ActiveProvider = Settings.FindActiveProvider())
			{
				if (IsUsableAnthropicProvider(*ActiveProvider))
				{
					OutProvider = *ActiveProvider;
					return true;
				}
			}

			for (const FAiProviderConfig& Provider : Settings.Providers)
			{
				if (IsUsableAnthropicProvider(Provider))
				{
					OutProvider = Provider;
					return true;
				}
			}
			return false;
		}

		TSharedPtr<FJsonValueObject> AnthropicTextBlock(const FString& Text)
		{
			TSharedPtr<FJsonObject> Block = MakeShared<FJsonObject>();
			Block->SetStringField(TEXT("type"), TEXT("text"));
			Block->SetStringField(TEXT("text"), Text);
			return MakeShared<FJsonValueObject>(Block);
		}

		TSharedPtr<FJsonObject> AnthropicUserMessage(const FString& Text)
		{
			TArray<TSharedPtr<FJsonValue>> Blocks;
			Blocks.Add(AnthropicTextBlock(Text));
			TSharedPtr<FJsonObject> Message = MakeShared<FJsonObject>();
			Message->SetStringField(TEXT("role"), TEXT("user"));
			Message->SetArrayField(TEXT("content"), Blocks);
			return Message;
		}

		FString ExtractAnthropicError(const FString& BodyString)
		{
			TSharedPtr<FJsonObject> ErrorObject;
			const TSharedPtr<FJsonObject>* Nested = nullptr;
			FString Message;
			if (LoadJsonObjectFromString(BodyString, ErrorObject)
				&& ErrorObject.IsValid()
				&& ErrorObject->TryGetObjectField(TEXT("error"), Nested)
				&& Nested
				&& (*Nested).IsValid()
				&& (*Nested)->TryGetStringField(TEXT("message"), Message)
				&& !Message.IsEmpty())
			{
				return Message;
			}
			return BodyString.IsEmpty() ? TEXT("Unknown Anthropic provider error.") : BodyString.Left(800);
		}

		FString ExtractAnthropicText(const FString& BodyString)
		{
			TSharedPtr<FJsonObject> ResponseObject;
			if (!LoadJsonObjectFromString(BodyString, ResponseObject) || !ResponseObject.IsValid())
			{
				return FString();
			}

			FString Text;
			const TArray<TSharedPtr<FJsonValue>>* ContentValues = nullptr;
			if (ResponseObject->TryGetArrayField(TEXT("content"), ContentValues) && ContentValues)
			{
				for (const TSharedPtr<FJsonValue>& Value : *ContentValues)
				{
					if (!Value.IsValid() || Value->Type != EJson::Object || !Value->AsObject().IsValid())
					{
						continue;
					}
					const TSharedPtr<FJsonObject> Block = Value->AsObject();
					FString Type;
					FString BlockText;
					if (Block->TryGetStringField(TEXT("type"), Type)
						&& Type == TEXT("text")
						&& Block->TryGetStringField(TEXT("text"), BlockText))
					{
						Text += BlockText;
					}
				}
			}
			return Text.TrimStartAndEnd();
		}

		bool SendAnthropicLabelRequest(
			const FAiProviderConfig& Provider,
			const UUnrealMcpSettings& Settings,
			const FString& Model,
			const FString& Prompt,
			FAnthropicLabelResponse& OutResponse)
		{
			OutResponse = FAnthropicLabelResponse();
			OutResponse.Model = Model;

			TArray<TSharedPtr<FJsonValue>> MessageValues;
			MessageValues.Add(MakeShared<FJsonValueObject>(AnthropicUserMessage(Prompt)));
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("model"), Model);
			Payload->SetStringField(TEXT("system"), TEXT("Return only a compact JSON object. Do not include markdown fences or explanatory text."));
			Payload->SetArrayField(TEXT("messages"), MessageValues);
			Payload->SetNumberField(TEXT("max_tokens"), 128);
			Payload->SetNumberField(TEXT("temperature"), 0.0);
			Payload->SetBoolField(TEXT("stream"), false);

			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
			Request->SetURL(Provider.BaseUrl.TrimStartAndEnd());
			Request->SetVerb(TEXT("POST"));
			Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
			Request->SetHeader(TEXT("x-api-key"), Provider.ApiKey.TrimStartAndEnd());
			Request->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));
			Request->SetTimeout(FMath::Max(10.0f, Settings.AiRequestTimeoutSeconds));
			Request->SetActivityTimeout(FMath::Max(10.0f, Settings.AiRequestActivityTimeoutSeconds));
			Request->SetContentAsString(JsonObjectToString(Payload));

			const TSharedRef<FHttpLabelRequestState, ESPMode::ThreadSafe> State = MakeShared<FHttpLabelRequestState, ESPMode::ThreadSafe>();
			Request->OnProcessRequestComplete().BindLambda(
				[State](FHttpRequestPtr, FHttpResponsePtr HttpResponse, bool bRequestSucceeded)
				{
					{
						const FScopeLock Lock(&State->Mutex);
						State->bSucceeded = bRequestSucceeded && HttpResponse.IsValid();
						State->StatusCode = HttpResponse.IsValid() ? HttpResponse->GetResponseCode() : 0;
						State->BodyString = HttpResponse.IsValid() ? HttpResponse->GetContentAsString() : FString();
					}
					State->bDone.store(true, std::memory_order_release);
				});

			if (!Request->ProcessRequest())
			{
				OutResponse.FailureReason = TEXT("Failed to start the HTTP request to the Anthropic provider.");
				return false;
			}

			const double StartSeconds = FPlatformTime::Seconds();
			const double TimeoutSeconds = FMath::Max(10.0f, Settings.AiRequestTimeoutSeconds);
			while (!State->bDone.load(std::memory_order_acquire)
				&& FPlatformTime::Seconds() - StartSeconds < TimeoutSeconds)
			{
				FHttpModule::Get().GetHttpManager().Tick(0.016f);
				FPlatformProcess::Sleep(0.01f);
			}

			if (!State->bDone.load(std::memory_order_acquire))
			{
				Request->CancelRequest();
				OutResponse.FailureReason = TEXT("Timed out waiting for the Anthropic provider.");
				return false;
			}

			{
				const FScopeLock Lock(&State->Mutex);
				OutResponse.StatusCode = State->StatusCode;
				if (!State->bSucceeded)
				{
					OutResponse.FailureReason = TEXT("The Anthropic request failed before a valid HTTP response was returned.");
					return false;
				}
				if (State->StatusCode < 200 || State->StatusCode >= 300)
				{
					OutResponse.FailureReason = FString::Printf(TEXT("Anthropic error %d: %s"), State->StatusCode, *ExtractAnthropicError(State->BodyString));
					return false;
				}

				OutResponse.Text = ExtractAnthropicText(State->BodyString);
				if (OutResponse.Text.IsEmpty())
				{
					OutResponse.FailureReason = TEXT("Anthropic response did not contain a text block.");
					return false;
				}
			}

			OutResponse.bSucceeded = true;
			return true;
		}

		FString ExtractJsonObjectText(const FString& ResponseText)
		{
			const FString Trimmed = ResponseText.TrimStartAndEnd();
			const int32 FirstBrace = Trimmed.Find(TEXT("{"), ESearchCase::CaseSensitive);
			const int32 LastBrace = Trimmed.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (FirstBrace == INDEX_NONE || LastBrace == INDEX_NONE || LastBrace <= FirstBrace)
			{
				return Trimmed;
			}
			return Trimmed.Mid(FirstBrace, LastBrace - FirstBrace + 1);
		}

		bool ValidateLabelText(FString& InOutLabel)
		{
			TArray<FString> Words;
			InOutLabel.TrimStartAndEnd().ParseIntoArrayWS(Words);
			if (Words.Num() < 3 || Words.Num() > 7)
			{
				return false;
			}

			InOutLabel = FString::Join(Words, TEXT(" "));
			if (InOutLabel.Len() > 120)
			{
				return false;
			}
			for (const TCHAR Character : InOutLabel)
			{
				if (Character == TEXT('\n') || Character == TEXT('\r') || Character == TEXT('{') || Character == TEXT('}'))
				{
					return false;
				}
			}
			return true;
		}

		bool TryParseLabelResponse(const FString& ResponseText, FString& OutLabel, double& OutConfidence)
		{
			TSharedPtr<FJsonObject> ResponseObject;
			if (!LoadJsonObjectFromString(ExtractJsonObjectText(ResponseText), ResponseObject) || !ResponseObject.IsValid())
			{
				return false;
			}

			if (!ResponseObject->TryGetStringField(TEXT("label"), OutLabel))
			{
				return false;
			}
			OutLabel = SanitizePromptInput(OutLabel);
			if (!ValidateLabelText(OutLabel))
			{
				return false;
			}

			const TSharedPtr<FJsonValue> ConfidenceValue = ResponseObject->TryGetField(TEXT("confidence"));
			if (!ConfidenceValue.IsValid() || ConfidenceValue->Type != EJson::Number)
			{
				return false;
			}
			OutConfidence = ConfidenceValue->AsNumber();
			return OutConfidence >= 0.0 && OutConfidence <= 1.0;
		}
	}

	FUnrealMcpExecutionResult TaskLabelBackfill(const FJsonObject& Arguments)
	{
		FBackfillArgs Args;
		FUnrealMcpExecutionResult ErrorResult;
		if (!TryReadArgs(Arguments, Args, ErrorResult))
		{
			return ErrorResult;
		}

		TArray<FBackfillSkipReason> SkippedReasons;
		const TArray<FBackfillTask> SelectedTasks = SelectBackfillTasks(Args, SkippedReasons);
		if (SelectedTasks.Num() == 0)
		{
			return MakeBackfillResult(Args, 0, 0, SkippedReasons, TEXT("No Task Atlas labels needed backfill."));
		}

		if (Args.bDryRun)
		{
			return MakeBackfillResult(
				Args,
				SelectedTasks.Num(),
				0,
				SkippedReasons,
				FString::Printf(TEXT("Dry run found %d Task Atlas label(s) eligible for backfill."), SelectedTasks.Num()));
		}

		const UUnrealMcpSettings* Settings = GetDefault<UUnrealMcpSettings>();
		FAiProviderConfig Provider;
		if (!Settings || !TrySelectAnthropicProvider(*Settings, Provider))
		{
			for (const FBackfillTask& Task : SelectedTasks)
			{
				AddSkip(SkippedReasons, Task.TaskId, TEXT("no_provider_configured"));
			}
			return MakeBackfillResult(Args, 0, 0, SkippedReasons, TEXT("Task label backfill skipped: no Anthropic provider is configured."));
		}

		ApplyAiHttpTimeoutOverrides(*Settings);

		bool bPreferredModelUnavailable = false;
		bool bProviderUnavailable = false;
		int32 ProcessedCount = 0;
		int32 UpdatedCount = 0;

		for (const FBackfillTask& Task : SelectedTasks)
		{
			if (bProviderUnavailable)
			{
				AddSkip(SkippedReasons, Task.TaskId, TEXT("provider_request_failed"));
				continue;
			}

			const FString Prompt = BuildPromptForTask(Task);
			FAnthropicLabelResponse Response;
			const FString PrimaryModel = bPreferredModelUnavailable ? FString(FallbackBackfillModel) : FString(PreferredBackfillModel);
			bool bRequestSucceeded = SendAnthropicLabelRequest(Provider, *Settings, PrimaryModel, Prompt, Response);
			if (!bRequestSucceeded && !bPreferredModelUnavailable)
			{
				bPreferredModelUnavailable = true;
				bRequestSucceeded = SendAnthropicLabelRequest(Provider, *Settings, FallbackBackfillModel, Prompt, Response);
			}

			if (!bRequestSucceeded)
			{
				bProviderUnavailable = true;
				AddSkip(SkippedReasons, Task.TaskId, TEXT("provider_request_failed"));
				continue;
			}

			++ProcessedCount;
			FString Label;
			double Confidence = 0.0;
			if (!TryParseLabelResponse(Response.Text, Label, Confidence))
			{
				AddSkip(SkippedReasons, Task.TaskId, TEXT("invalid_provider_response"));
				continue;
			}

			Task.Object->SetStringField(TEXT("label"), Label);
			Task.Object->SetStringField(TEXT("labelSource"), TEXT("llm_backfill"));
			Task.Object->SetNumberField(TEXT("labelConfidence"), Confidence);
			if (!WriteJsonObjectToFile(Task.Object, Task.TaskPath))
			{
				AddSkip(SkippedReasons, Task.TaskId, TEXT("write_failed"));
				continue;
			}
			++UpdatedCount;
		}

		return MakeBackfillResult(
			Args,
			ProcessedCount,
			UpdatedCount,
			SkippedReasons,
			FString::Printf(TEXT("Backfilled %d Task Atlas label(s)."), UpdatedCount));
	}
}
