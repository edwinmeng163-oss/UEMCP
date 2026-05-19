#include "UnrealMcpAutomationTools.h"

#include "UnrealMcpModule.h"

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealMcp
{
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);

	namespace
	{
		constexpr int32 AutomationStateSchemaVersion = 1;
		constexpr int32 DefaultAutomationListLimit = 200;
		constexpr int32 MaxAutomationListLimit = 1000;
		constexpr int32 DefaultAutomationTimeoutSeconds = 120;
		constexpr int32 MaxAutomationTimeoutSeconds = 3600;
		constexpr int32 StaleGraceSeconds = 60;
		constexpr int32 UnresponsiveHeartbeatSeconds = 30;
		constexpr int32 HeartbeatPersistDebounceSeconds = 1;
		constexpr int32 LogExcerptLineLimit = 50;
		constexpr int32 LogExcerptCharLimit = 4096;

		struct FAutomationResultEvent
		{
			FString EventType;
			FString Message;
			FString Location;
			int32 Frame = INDEX_NONE;
		};

		struct FAutomationRunState
		{
			FString RunId;
			FString Status = TEXT("queued");
			FString Reason;
			FString FullName;
			FString DisplayName;
			FString PrettyName;
			TArray<FString> Flags;
			TArray<FString> Tags;
			FDateTime AcceptedAtUtc;
			FDateTime StartedAtUtc;
			FDateTime EndedAtUtc;
			FDateTime LastHeartbeatUtc;
			FString StaleReason;
			int32 TimeoutSeconds = DefaultAutomationTimeoutSeconds;
			TArray<FAutomationResultEvent> Results;
			bool bBestEffortCancelAttempted = false;
		};

		FCriticalSection GAutomationRunMutex;
		bool bHasActiveAutomationRun = false;
		FAutomationRunState GActiveAutomationRun;
		FTSTicker::FDelegateHandle GAutomationTickerHandle;
		FDateTime LastPersistedHeartbeatUtc;

#if WITH_DEV_AUTOMATION_TESTS
		bool bSuppressAutomationFrameworkStartForTests = false;
#endif

		FString GetAutomationRunRoot()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/AutomationRuns")));
		}

		FString MakeReportRelativePath(const FString& RunId)
		{
			return FString::Printf(TEXT("Saved/UnrealMcp/AutomationRuns/%s.json"), *RunId);
		}

		FString MakeReportPath(const FString& RunId)
		{
			return FPaths::Combine(GetAutomationRunRoot(), RunId + TEXT(".json"));
		}

		bool IsSafeRunId(const FString& RunId)
		{
			const FString Trimmed = RunId.TrimStartAndEnd();
			if (Trimmed.IsEmpty() || Trimmed.Len() > 80)
			{
				return false;
			}
			for (const TCHAR Character : Trimmed)
			{
				if (!FChar::IsAlnum(Character) && Character != TEXT('-'))
				{
					return false;
				}
			}
			return true;
		}

		bool IsActiveStatus(const FString& Status)
		{
			return Status == TEXT("queued") || Status == TEXT("running");
		}

		FString NormalizeStatusForPublicReport(const FAutomationRunState& State, const FDateTime& NowUtc)
		{
			if (IsActiveStatus(State.Status) && State.StartedAtUtc.GetTicks() > 0)
			{
				const FDateTime TimeoutAt = State.StartedAtUtc + FTimespan::FromSeconds(State.TimeoutSeconds);
				if (NowUtc > TimeoutAt)
				{
					return TEXT("timed_out");
				}
			}
			return State.Status;
		}

		FString GetStaleReason(const FAutomationRunState& State, const FDateTime& NowUtc)
		{
			if (!IsActiveStatus(State.Status))
			{
				return FString();
			}
			const FDateTime Basis = State.StartedAtUtc.GetTicks() > 0 ? State.StartedAtUtc : State.AcceptedAtUtc;
			if (Basis.GetTicks() > 0 && NowUtc > (Basis + FTimespan::FromSeconds(State.TimeoutSeconds + StaleGraceSeconds)))
			{
				return TEXT("hard_timeout");
			}
			if (State.LastHeartbeatUtc.GetTicks() > 0
				&& NowUtc > (State.LastHeartbeatUtc + FTimespan::FromSeconds(UnresponsiveHeartbeatSeconds)))
			{
				return TEXT("unresponsive");
			}
			return FString();
		}

		TArray<TSharedPtr<FJsonValue>> MakeStringArrayValues(const TArray<FString>& Values)
		{
			TArray<TSharedPtr<FJsonValue>> JsonValues;
			for (const FString& Value : Values)
			{
				JsonValues.Add(MakeShared<FJsonValueString>(Value));
			}
			return JsonValues;
		}

		TArray<TSharedPtr<FJsonValue>> MakeResultEventValues(const TArray<FAutomationResultEvent>& Results)
		{
			TArray<TSharedPtr<FJsonValue>> JsonValues;
			for (const FAutomationResultEvent& Result : Results)
			{
				TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
				ResultObject->SetStringField(TEXT("eventType"), Result.EventType);
				ResultObject->SetStringField(TEXT("message"), Result.Message);
				if (!Result.Location.IsEmpty())
				{
					ResultObject->SetStringField(TEXT("location"), Result.Location);
				}
				if (Result.Frame != INDEX_NONE)
				{
					ResultObject->SetNumberField(TEXT("frame"), Result.Frame);
				}
				JsonValues.Add(MakeShared<FJsonValueObject>(ResultObject));
			}
			return JsonValues;
		}

		TSharedPtr<FJsonObject> MakeStateTestObject(const FAutomationRunState& State)
		{
			TSharedPtr<FJsonObject> TestObject = MakeShared<FJsonObject>();
			TestObject->SetStringField(TEXT("fullName"), State.FullName);
			TestObject->SetStringField(TEXT("displayName"), State.DisplayName);
			if (!State.PrettyName.IsEmpty())
			{
				TestObject->SetStringField(TEXT("prettyName"), State.PrettyName);
			}
			TestObject->SetArrayField(TEXT("flags"), MakeStringArrayValues(State.Flags));
			return TestObject;
		}

		TSharedPtr<FJsonObject> MakeMatchedTestObject(const FAutomationRunState& State)
		{
			TSharedPtr<FJsonObject> TestObject = MakeShared<FJsonObject>();
			TestObject->SetStringField(TEXT("fullName"), State.FullName);
			TestObject->SetStringField(TEXT("displayName"), State.DisplayName);
			return TestObject;
		}

		TSharedPtr<FJsonObject> MakePublicReportTestObject(const FAutomationRunState& State)
		{
			TSharedPtr<FJsonObject> TestObject = MakeMatchedTestObject(State);
			TestObject->SetArrayField(TEXT("flags"), MakeStringArrayValues(State.Flags));
			return TestObject;
		}

		FString TailProjectLogExcerpt()
		{
			const FString LogPath = FPaths::Combine(FPaths::ProjectLogDir(), FString::Printf(TEXT("%s.log"), FApp::GetProjectName()));
			TArray<FString> Lines;
			if (!FFileHelper::LoadFileToStringArray(Lines, *LogPath))
			{
				return FString();
			}

			FString Excerpt;
			const int32 StartIndex = FMath::Max(0, Lines.Num() - LogExcerptLineLimit);
			for (int32 Index = StartIndex; Index < Lines.Num(); ++Index)
			{
				Excerpt += Lines[Index].TrimEnd();
				Excerpt += LINE_TERMINATOR;
			}
			if (Excerpt.Len() > LogExcerptCharLimit)
			{
				Excerpt = Excerpt.Right(LogExcerptCharLimit);
			}
			return Excerpt;
		}

		TSharedPtr<FJsonObject> MakeStateJsonObject(const FAutomationRunState& State)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetNumberField(TEXT("internalSchemaVersion"), AutomationStateSchemaVersion);
			Object->SetStringField(TEXT("runId"), State.RunId);
			Object->SetStringField(TEXT("status"), State.Status);
			Object->SetStringField(TEXT("reason"), State.Reason);
			Object->SetObjectField(TEXT("test"), MakeStateTestObject(State));
			Object->SetStringField(TEXT("acceptedAt"), State.AcceptedAtUtc.ToIso8601());
			Object->SetStringField(TEXT("startedAt"), State.StartedAtUtc.ToIso8601());
			if (State.EndedAtUtc.GetTicks() > 0)
			{
				Object->SetStringField(TEXT("endedAt"), State.EndedAtUtc.ToIso8601());
				Object->SetNumberField(TEXT("durationMs"), FMath::Max(0.0, (State.EndedAtUtc - State.StartedAtUtc).GetTotalMilliseconds()));
			}
			if (State.LastHeartbeatUtc.GetTicks() > 0)
			{
				Object->SetStringField(TEXT("lastHeartbeatUtc"), State.LastHeartbeatUtc.ToIso8601());
			}
			if (!State.StaleReason.IsEmpty())
			{
				Object->SetStringField(TEXT("staleReason"), State.StaleReason);
			}
			Object->SetBoolField(TEXT("bestEffortCancelAttempted"), State.bBestEffortCancelAttempted);
			Object->SetNumberField(TEXT("timeoutSecondsConfigured"), State.TimeoutSeconds);
			Object->SetArrayField(TEXT("results"), MakeResultEventValues(State.Results));
			Object->SetStringField(TEXT("logExcerpt"), TailProjectLogExcerpt());
			Object->SetStringField(TEXT("reportPath"), MakeReportRelativePath(State.RunId));
			if (!State.Tags.IsEmpty())
			{
				Object->SetArrayField(TEXT("tags"), MakeStringArrayValues(State.Tags));
			}
			return Object;
		}

		TSharedPtr<FJsonObject> MakePublicReportObject(const FAutomationRunState& State)
		{
			const FDateTime NowUtc = FDateTime::UtcNow();
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("runId"), State.RunId);
			Object->SetStringField(TEXT("status"), NormalizeStatusForPublicReport(State, NowUtc));
			Object->SetObjectField(TEXT("test"), MakePublicReportTestObject(State));
			Object->SetArrayField(TEXT("results"), MakeResultEventValues(State.Results));
			Object->SetStringField(TEXT("startedAt"), State.StartedAtUtc.ToIso8601());
			if (State.EndedAtUtc.GetTicks() > 0)
			{
				Object->SetStringField(TEXT("endedAt"), State.EndedAtUtc.ToIso8601());
				Object->SetNumberField(TEXT("durationMs"), FMath::Max(0.0, (State.EndedAtUtc - State.StartedAtUtc).GetTotalMilliseconds()));
			}
			Object->SetStringField(TEXT("logExcerpt"), TailProjectLogExcerpt());
			Object->SetStringField(TEXT("reportPath"), MakeReportRelativePath(State.RunId));
			if (!State.Tags.IsEmpty())
			{
				Object->SetArrayField(TEXT("tags"), MakeStringArrayValues(State.Tags));
			}
			return Object;
		}

		bool SaveStateFile(const FAutomationRunState& State)
		{
			IFileManager::Get().MakeDirectory(*GetAutomationRunRoot(), true);
			FString JsonText;
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonText);
			if (!FJsonSerializer::Serialize(MakeStateJsonObject(State).ToSharedRef(), Writer))
			{
				return false;
			}
			return FFileHelper::SaveStringToFile(JsonText, *MakeReportPath(State.RunId));
		}

		bool LoadJsonObjectFromFile(const FString& Path, TSharedPtr<FJsonObject>& OutObject)
		{
			FString JsonText;
			if (!FFileHelper::LoadFileToString(JsonText, *Path))
			{
				return false;
			}
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
			return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
		}

		bool ParseIsoTimestamp(const FString& Text, FDateTime& OutTime)
		{
			OutTime = FDateTime();
			return !Text.IsEmpty() && FDateTime::ParseIso8601(*Text, OutTime);
		}

		TArray<FString> ReadStringArrayField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
		{
			TArray<FString> Values;
			const TArray<TSharedPtr<FJsonValue>>* JsonValues = nullptr;
			if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, JsonValues) || !JsonValues)
			{
				return Values;
			}
			for (const TSharedPtr<FJsonValue>& JsonValue : *JsonValues)
			{
				FString Value;
				if (JsonValue.IsValid() && JsonValue->TryGetString(Value))
				{
					Values.Add(Value);
				}
			}
			return Values;
		}

		TArray<FAutomationResultEvent> ReadResultEvents(const TSharedPtr<FJsonObject>& Object)
		{
			TArray<FAutomationResultEvent> Results;
			const TArray<TSharedPtr<FJsonValue>>* JsonValues = nullptr;
			if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("results"), JsonValues) || !JsonValues)
			{
				return Results;
			}
			for (const TSharedPtr<FJsonValue>& JsonValue : *JsonValues)
			{
				const TSharedPtr<FJsonObject>* ResultObject = nullptr;
				if (!JsonValue.IsValid() || !JsonValue->TryGetObject(ResultObject) || !ResultObject || !ResultObject->IsValid())
				{
					continue;
				}
				FAutomationResultEvent Result;
				(*ResultObject)->TryGetStringField(TEXT("eventType"), Result.EventType);
				(*ResultObject)->TryGetStringField(TEXT("message"), Result.Message);
				(*ResultObject)->TryGetStringField(TEXT("location"), Result.Location);
				double FrameNumber = 0.0;
				if ((*ResultObject)->TryGetNumberField(TEXT("frame"), FrameNumber))
				{
					Result.Frame = static_cast<int32>(FrameNumber);
				}
				Results.Add(Result);
			}
			return Results;
		}

		bool LoadStateFromFile(const FString& RunId, FAutomationRunState& OutState)
		{
			if (!IsSafeRunId(RunId))
			{
				return false;
			}
			TSharedPtr<FJsonObject> Object;
			if (!LoadJsonObjectFromFile(MakeReportPath(RunId), Object))
			{
				return false;
			}

			FAutomationRunState State;
			Object->TryGetStringField(TEXT("runId"), State.RunId);
			Object->TryGetStringField(TEXT("status"), State.Status);
			Object->TryGetStringField(TEXT("reason"), State.Reason);
			double TimeoutSeconds = static_cast<double>(DefaultAutomationTimeoutSeconds);
			Object->TryGetNumberField(TEXT("timeoutSecondsConfigured"), TimeoutSeconds);
			State.TimeoutSeconds = FMath::Clamp(static_cast<int32>(TimeoutSeconds), 1, MaxAutomationTimeoutSeconds);

			FString AcceptedAt;
			FString StartedAt;
			FString EndedAt;
			FString LastHeartbeatAt;
			Object->TryGetStringField(TEXT("acceptedAt"), AcceptedAt);
			Object->TryGetStringField(TEXT("startedAt"), StartedAt);
			Object->TryGetStringField(TEXT("endedAt"), EndedAt);
			Object->TryGetStringField(TEXT("lastHeartbeatUtc"), LastHeartbeatAt);
			ParseIsoTimestamp(AcceptedAt, State.AcceptedAtUtc);
			ParseIsoTimestamp(StartedAt, State.StartedAtUtc);
			ParseIsoTimestamp(EndedAt, State.EndedAtUtc);
			ParseIsoTimestamp(LastHeartbeatAt, State.LastHeartbeatUtc);
			if (State.StartedAtUtc.GetTicks() <= 0)
			{
				State.StartedAtUtc = State.AcceptedAtUtc;
			}
			Object->TryGetStringField(TEXT("staleReason"), State.StaleReason);
			Object->TryGetBoolField(TEXT("bestEffortCancelAttempted"), State.bBestEffortCancelAttempted);

			const TSharedPtr<FJsonObject>* TestObject = nullptr;
			if (Object->TryGetObjectField(TEXT("test"), TestObject) && TestObject && TestObject->IsValid())
			{
				(*TestObject)->TryGetStringField(TEXT("fullName"), State.FullName);
				(*TestObject)->TryGetStringField(TEXT("displayName"), State.DisplayName);
				(*TestObject)->TryGetStringField(TEXT("prettyName"), State.PrettyName);
				State.Flags = ReadStringArrayField(*TestObject, TEXT("flags"));
			}
			State.Tags = ReadStringArrayField(Object, TEXT("tags"));
			State.Results = ReadResultEvents(Object);
			OutState = MoveTemp(State);
			return !OutState.RunId.IsEmpty();
		}

		TSharedPtr<FJsonObject> MakeErrorObject(const FString& ErrorKind, const FString& Message)
		{
			TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
			ErrorObject->SetBoolField(TEXT("isError"), true);
			ErrorObject->SetStringField(TEXT("errorKind"), ErrorKind);
			ErrorObject->SetStringField(TEXT("message"), Message);
			return ErrorObject;
		}

		FUnrealMcpExecutionResult MakeErrorResult(const FString& ErrorKind, const FString& Message)
		{
			return MakeExecutionResult(Message, MakeErrorObject(ErrorKind, Message), true);
		}

		int32 GetIntArgument(const FJsonObject& Arguments, const FString& FieldName, int32 DefaultValue, int32 MinValue, int32 MaxValue)
		{
			double Value = static_cast<double>(DefaultValue);
			if (Arguments.TryGetNumberField(FieldName, Value))
			{
				return FMath::Clamp(static_cast<int32>(Value), MinValue, MaxValue);
			}
			return DefaultValue;
		}

		TArray<FString> GetStringArrayArgument(const FJsonObject& Arguments, const FString& FieldName)
		{
			TArray<FString> Values;
			const TArray<TSharedPtr<FJsonValue>>* JsonValues = nullptr;
			if (!Arguments.TryGetArrayField(FieldName, JsonValues) || !JsonValues)
			{
				return Values;
			}
			for (const TSharedPtr<FJsonValue>& JsonValue : *JsonValues)
			{
				FString Value;
				if (JsonValue.IsValid() && JsonValue->TryGetString(Value))
				{
					Values.Add(Value);
				}
			}
			return Values;
		}

		bool HasAutomationFlag(EAutomationTestFlags Flags, EAutomationTestFlags Flag)
		{
			return !!(Flags & Flag);
		}

		void AddAutomationFlagName(TArray<FString>& OutFlags, EAutomationTestFlags Flags, EAutomationTestFlags Flag, const TCHAR* Name)
		{
			if (HasAutomationFlag(Flags, Flag))
			{
				OutFlags.Add(Name);
			}
		}

		TArray<FString> AutomationFlagsToStrings(EAutomationTestFlags Flags)
		{
			TArray<FString> Names;
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::EditorContext, TEXT("EditorContext"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::ClientContext, TEXT("ClientContext"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::ServerContext, TEXT("ServerContext"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::CommandletContext, TEXT("CommandletContext"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::ProgramContext, TEXT("ProgramContext"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::NonNullRHI, TEXT("NonNullRHI"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::RequiresUser, TEXT("RequiresUser"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::Disabled, TEXT("Disabled"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::CriticalPriority, TEXT("CriticalPriority"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::HighPriority, TEXT("HighPriority"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::MediumPriority, TEXT("MediumPriority"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::LowPriority, TEXT("LowPriority"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::SmokeFilter, TEXT("SmokeFilter"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::EngineFilter, TEXT("EngineFilter"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::ProductFilter, TEXT("ProductFilter"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::PerfFilter, TEXT("PerfFilter"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::StressFilter, TEXT("StressFilter"));
			AddAutomationFlagName(Names, Flags, EAutomationTestFlags::NegativeFilter, TEXT("NegativeFilter"));
			return Names;
		}

		FString DeriveAutomationCategory(const FString& DisplayName)
		{
			FString Category;
			FString Remainder;
			if (DisplayName.Split(TEXT("."), &Category, &Remainder))
			{
				return Category;
			}
			return FString();
		}

		bool LooksLikePieTest(const FString& FullName, const FString& DisplayName)
		{
			return FullName.Contains(TEXT("PIE"), ESearchCase::IgnoreCase)
				|| FullName.Contains(TEXT("PlayInEditor"), ESearchCase::IgnoreCase)
				|| DisplayName.Contains(TEXT("PIE"), ESearchCase::IgnoreCase)
				|| DisplayName.Contains(TEXT("Play In Editor"), ESearchCase::IgnoreCase);
		}

		TSharedPtr<FJsonObject> MakeAutomationTestListItem(const FAutomationTestInfo& TestInfo, bool bIncludeDetails)
		{
			const FString FullName = TestInfo.GetTestName();
			const FString DisplayName = TestInfo.GetDisplayName();
			const FString PrettyName = TestInfo.GetFullTestPath();
			const EAutomationTestFlags Flags = TestInfo.GetTestFlags();

			TSharedPtr<FJsonObject> TestObject = MakeShared<FJsonObject>();
			TestObject->SetStringField(TEXT("fullName"), FullName);
			TestObject->SetStringField(TEXT("displayName"), DisplayName);
			if (bIncludeDetails && !PrettyName.IsEmpty() && PrettyName != DisplayName)
			{
				TestObject->SetStringField(TEXT("prettyName"), PrettyName);
			}
			TestObject->SetArrayField(TEXT("flags"), MakeStringArrayValues(AutomationFlagsToStrings(Flags)));
			if (bIncludeDetails)
			{
				const FString Category = DeriveAutomationCategory(DisplayName);
				if (!Category.IsEmpty())
				{
					TestObject->SetStringField(TEXT("category"), Category);
				}
				TestObject->SetBoolField(TEXT("requiresPie"), LooksLikePieTest(FullName, DisplayName));
				TestObject->SetBoolField(TEXT("requiresEditor"), HasAutomationFlag(Flags, EAutomationTestFlags::EditorContext));
			}
			return TestObject;
		}

		void GetAutomationTests(TArray<FAutomationTestInfo>& OutTests)
		{
			OutTests.Reset();
			FAutomationTestFramework::Get().GetValidTestNames(OutTests);
		}

		bool FindAutomationTestByFullName(const FString& FullName, FAutomationTestInfo& OutTestInfo)
		{
			TArray<FAutomationTestInfo> Tests;
			GetAutomationTests(Tests);
			for (const FAutomationTestInfo& TestInfo : Tests)
			{
				if (TestInfo.GetTestName().Equals(FullName, ESearchCase::CaseSensitive))
				{
					OutTestInfo = TestInfo;
					return true;
				}
			}
			return false;
		}

		FAutomationRunState MakeRunStateFromTestInfo(
			const FString& RunId,
			const FAutomationTestInfo& TestInfo,
			const TArray<FString>& Tags,
			int32 TimeoutSeconds)
		{
			const FDateTime NowUtc = FDateTime::UtcNow();
			FAutomationRunState State;
			State.RunId = RunId;
			State.Status = TEXT("queued");
			State.FullName = TestInfo.GetTestName();
			State.DisplayName = TestInfo.GetDisplayName();
			State.PrettyName = TestInfo.GetFullTestPath();
			State.Flags = AutomationFlagsToStrings(TestInfo.GetTestFlags());
			State.Tags = Tags;
			State.AcceptedAtUtc = NowUtc;
			State.StartedAtUtc = NowUtc;
			State.LastHeartbeatUtc = NowUtc;
			State.TimeoutSeconds = TimeoutSeconds;
			return State;
		}

		FString MakeRunId()
		{
			for (int32 Attempt = 0; Attempt < 8; ++Attempt)
			{
				const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ"));
				const FString Hex = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6).ToLower();
				const FString RunId = FString::Printf(TEXT("%s-%s"), *Timestamp, *Hex);
				if (!IFileManager::Get().FileExists(*MakeReportPath(RunId)))
				{
					return RunId;
				}
			}
			const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ"));
			const FString Hex = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6).ToLower();
			return FString::Printf(TEXT("%s-%s"), *Timestamp, *Hex);
		}

		void AttemptBestEffortCancel(FAutomationRunState& State)
		{
			if (State.bBestEffortCancelAttempted || IsEngineExitRequested())
			{
				return;
			}
			FAutomationTestFramework::Get().DequeueAllCommands();
			State.bBestEffortCancelAttempted = true;
		}

		void MarkStateStale(FAutomationRunState& State, const FString& StaleReason, bool bAttemptBestEffortCancel = true)
		{
			State.Status = TEXT("stale");
			State.StaleReason = StaleReason.TrimStartAndEnd().IsEmpty() ? TEXT("hard_timeout") : StaleReason.TrimStartAndEnd();
			if (State.StaleReason == TEXT("hard_timeout"))
			{
				State.Reason = TEXT("exceeded timeout window");
			}
			else if (State.StaleReason == TEXT("unresponsive"))
			{
				State.Reason = TEXT("automation heartbeat stopped");
			}
			else if (State.StaleReason == TEXT("editor_shutdown"))
			{
				State.Reason = TEXT("editor shut down while run was active");
			}
			else
			{
				State.Reason = State.StaleReason;
			}
			if (bAttemptBestEffortCancel)
			{
				AttemptBestEffortCancel(State);
			}
			State.EndedAtUtc = FDateTime::UtcNow();
			SaveStateFile(State);
		}

		bool TryLoadActiveRunFromDisk(FAutomationRunState& OutState)
		{
			TArray<FString> FileNames;
			IFileManager::Get().FindFiles(FileNames, *FPaths::Combine(GetAutomationRunRoot(), TEXT("*.json")), true, false);
			FileNames.Sort();
			for (int32 Index = FileNames.Num() - 1; Index >= 0; --Index)
			{
				const FString RunId = FPaths::GetBaseFilename(FileNames[Index]);
				FAutomationRunState State;
				if (!LoadStateFromFile(RunId, State) || !IsActiveStatus(State.Status))
				{
					continue;
				}
				const FString StaleReason = GetStaleReason(State, FDateTime::UtcNow());
				if (!StaleReason.IsEmpty())
				{
					MarkStateStale(State, StaleReason);
					continue;
				}
				OutState = MoveTemp(State);
				return true;
			}
			return false;
		}

		bool TryGetActiveRun(FAutomationRunState& OutState)
		{
			{
				FScopeLock Lock(&GAutomationRunMutex);
				if (bHasActiveAutomationRun)
				{
					const FString StaleReason = GetStaleReason(GActiveAutomationRun, FDateTime::UtcNow());
					if (!StaleReason.IsEmpty())
					{
						MarkStateStale(GActiveAutomationRun, StaleReason);
						bHasActiveAutomationRun = false;
					}
					else
					{
						OutState = GActiveAutomationRun;
						return true;
					}
				}
			}

			if (TryLoadActiveRunFromDisk(OutState))
			{
				FScopeLock Lock(&GAutomationRunMutex);
				GActiveAutomationRun = OutState;
				bHasActiveAutomationRun = true;
				LastPersistedHeartbeatUtc = OutState.LastHeartbeatUtc;
				return true;
			}
			return false;
		}

		void ClearActiveRunIfMatching(const FString& RunId)
		{
			FScopeLock Lock(&GAutomationRunMutex);
			if (bHasActiveAutomationRun && GActiveAutomationRun.RunId == RunId)
			{
				bHasActiveAutomationRun = false;
				LastPersistedHeartbeatUtc = FDateTime();
			}
		}

		void SetActiveRunState(const FAutomationRunState& State)
		{
			FScopeLock Lock(&GAutomationRunMutex);
			GActiveAutomationRun = State;
			bHasActiveAutomationRun = IsActiveStatus(State.Status);
			if (!bHasActiveAutomationRun)
			{
				LastPersistedHeartbeatUtc = FDateTime();
			}
		}

		FAutomationRunState GetActiveRunStateCopy()
		{
			FScopeLock Lock(&GAutomationRunMutex);
			if (!bHasActiveAutomationRun)
			{
				return FAutomationRunState();
			}
			return GActiveAutomationRun;
		}

		FString MapAutomationEventType(EAutomationEventType EventType)
		{
			switch (EventType)
			{
			case EAutomationEventType::Warning:
				return TEXT("warning");
			case EAutomationEventType::Error:
				return TEXT("assertion_failed");
			case EAutomationEventType::Info:
			default:
				return TEXT("info");
			}
		}

		TArray<FAutomationResultEvent> ConvertExecutionEntries(const FAutomationTestExecutionInfo& ExecutionInfo)
		{
			TArray<FAutomationResultEvent> Results;
			for (const FAutomationExecutionEntry& Entry : ExecutionInfo.GetEntries())
			{
				FAutomationResultEvent Result;
				Result.EventType = MapAutomationEventType(Entry.Event.Type);
				Result.Message = Entry.Event.Message;
				if (!Entry.Filename.IsEmpty() && Entry.LineNumber > 0)
				{
					Result.Location = FString::Printf(TEXT("%s:%d"), *Entry.Filename, Entry.LineNumber);
				}
				Results.Add(Result);
			}
			return Results;
		}

		void ResetAutomationTickerHandle()
		{
			GAutomationTickerHandle.Reset();
		}

		void UpdateActiveRunHeartbeat(FAutomationRunState& State, const FDateTime& NowUtc)
		{
			if (!IsActiveStatus(State.Status))
			{
				return;
			}

			State.LastHeartbeatUtc = NowUtc;
			bool bPersistHeartbeat = false;
			{
				FScopeLock Lock(&GAutomationRunMutex);
				if (bHasActiveAutomationRun && GActiveAutomationRun.RunId == State.RunId)
				{
					GActiveAutomationRun.LastHeartbeatUtc = NowUtc;
					if (LastPersistedHeartbeatUtc.GetTicks() <= 0
						|| NowUtc > (LastPersistedHeartbeatUtc + FTimespan::FromSeconds(HeartbeatPersistDebounceSeconds)))
					{
						LastPersistedHeartbeatUtc = NowUtc;
						bPersistHeartbeat = true;
					}
				}
			}

			if (bPersistHeartbeat)
			{
				SaveStateFile(State);
			}
		}

		bool TickActiveAutomationRun(float DeltaTime)
		{
			(void)DeltaTime;
			FAutomationRunState State = GetActiveRunStateCopy();
			if (State.RunId.IsEmpty())
			{
				ResetAutomationTickerHandle();
				return false;
			}

			const FDateTime NowUtc = FDateTime::UtcNow();
			UpdateActiveRunHeartbeat(State, NowUtc);

			const FString StaleReason = GetStaleReason(State, NowUtc);
			if (!StaleReason.IsEmpty())
			{
				MarkStateStale(State, StaleReason);
				ClearActiveRunIfMatching(State.RunId);
				ResetAutomationTickerHandle();
				return false;
			}

			if (State.Status == TEXT("queued"))
			{
#if WITH_DEV_AUTOMATION_TESTS
				if (bSuppressAutomationFrameworkStartForTests)
				{
					State.Status = TEXT("running");
					State.StartedAtUtc = FDateTime::UtcNow();
					State.LastHeartbeatUtc = State.StartedAtUtc;
					SetActiveRunState(State);
					SaveStateFile(State);
					LastPersistedHeartbeatUtc = State.LastHeartbeatUtc;
					return true;
				}
#endif
				FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
				Framework.StartTestByName(State.FullName, 0, State.PrettyName.IsEmpty() ? State.DisplayName : State.PrettyName);
				State.Status = TEXT("running");
				State.StartedAtUtc = FDateTime::UtcNow();
				State.LastHeartbeatUtc = State.StartedAtUtc;
				if (!GIsAutomationTesting || Framework.GetCurrentTest() == nullptr)
				{
					State.Status = TEXT("failed");
					State.Reason = TEXT("Automation framework did not start the requested test.");
					State.EndedAtUtc = FDateTime::UtcNow();
					SetActiveRunState(State);
					SaveStateFile(State);
					LastPersistedHeartbeatUtc = State.LastHeartbeatUtc;
					ClearActiveRunIfMatching(State.RunId);
					ResetAutomationTickerHandle();
					return false;
				}
				SetActiveRunState(State);
				SaveStateFile(State);
				LastPersistedHeartbeatUtc = State.LastHeartbeatUtc;
				return true;
			}

			if (State.Status != TEXT("running"))
			{
				ClearActiveRunIfMatching(State.RunId);
				ResetAutomationTickerHandle();
				return false;
			}

			FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
			if (!GIsAutomationTesting || Framework.GetCurrentTest() == nullptr)
			{
				State.Status = TEXT("failed");
				State.Reason = TEXT("Automation framework is no longer running the test.");
				State.EndedAtUtc = FDateTime::UtcNow();
				SetActiveRunState(State);
				SaveStateFile(State);
				ClearActiveRunIfMatching(State.RunId);
				ResetAutomationTickerHandle();
				return false;
			}

			// UE Automation Framework API semantics (per AutomationTest.h):
			//
			// ExecuteNetworkCommands() returns TRUE when any network commands were
			// in the queue this frame (the test should keep ticking so subsequent
			// latent commands can run next frame). FALSE means the network command
			// queue is empty and we can evaluate latent completion now.
			//
			// ExecuteLatentCommands() returns TRUE when the latent command queue
			// is empty AND the test is complete. FALSE means latent work is still
			// pending and we should tick again.
			//
			// Combined: keep ticking if there are pending network commands OR if
			// latent work is not yet complete.
			const bool bNetworkPending = Framework.ExecuteNetworkCommands();
			const bool bLatentDone = Framework.ExecuteLatentCommands();
			if (bNetworkPending || !bLatentDone)
			{
				return true;
			}

			FAutomationTestExecutionInfo ExecutionInfo;
			const bool bSuccessful = Framework.StopTest(ExecutionInfo);
			State.Status = bSuccessful ? TEXT("completed") : TEXT("failed");
			State.EndedAtUtc = FDateTime::UtcNow();
			State.Results = ConvertExecutionEntries(ExecutionInfo);
			SetActiveRunState(State);
			SaveStateFile(State);
			ClearActiveRunIfMatching(State.RunId);
			ResetAutomationTickerHandle();
			return false;
		}

		void EnsureAutomationTicker()
		{
			if (!GAutomationTickerHandle.IsValid())
			{
				GAutomationTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickActiveAutomationRun), 0.1f);
			}
		}

		FUnrealMcpExecutionResult AutomationListTool(const FJsonObject& Arguments)
		{
			FString Filter;
			bool bIncludeDetails = false;
			Arguments.TryGetStringField(TEXT("filter"), Filter);
			Arguments.TryGetBoolField(TEXT("includeDetails"), bIncludeDetails);
			const int32 Limit = GetIntArgument(Arguments, TEXT("limit"), DefaultAutomationListLimit, 1, MaxAutomationListLimit);
			const FString FilterLower = Filter.TrimStartAndEnd().ToLower();

			TArray<FAutomationTestInfo> Tests;
			GetAutomationTests(Tests);

			TArray<TSharedPtr<FJsonValue>> TestValues;
			int32 TotalCount = 0;
			for (const FAutomationTestInfo& TestInfo : Tests)
			{
				const FString FullName = TestInfo.GetTestName();
				const FString DisplayName = TestInfo.GetDisplayName();
				if (!FilterLower.IsEmpty()
					&& !FullName.ToLower().Contains(FilterLower)
					&& !DisplayName.ToLower().Contains(FilterLower))
				{
					continue;
				}

				++TotalCount;
				if (TestValues.Num() < Limit)
				{
					TestValues.Add(MakeShared<FJsonValueObject>(MakeAutomationTestListItem(TestInfo, bIncludeDetails)));
				}
			}

			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetArrayField(TEXT("tests"), TestValues);
			Content->SetNumberField(TEXT("totalCount"), TotalCount);
			Content->SetBoolField(TEXT("truncated"), TotalCount > TestValues.Num());
			return MakeExecutionResult(
				FString::Printf(TEXT("Found %d automation tests%s."), TotalCount, TotalCount > TestValues.Num() ? TEXT(" (truncated)") : TEXT("")),
				Content,
				false);
		}

		FUnrealMcpExecutionResult AutomationRunTool(const FJsonObject& Arguments)
		{
			FAutomationRunState ActiveRun;
			if (TryGetActiveRun(ActiveRun))
			{
				const FString Message = FString::Printf(TEXT("Automation run '%s' is still %s."), *ActiveRun.RunId, *ActiveRun.Status);
				TSharedPtr<FJsonObject> ErrorObject = MakeErrorObject(TEXT("RunAlreadyActive"), Message);
				ErrorObject->SetStringField(TEXT("activeRunId"), ActiveRun.RunId);
				return MakeExecutionResult(Message, ErrorObject, true);
			}

			FString FullName;
			if (!Arguments.TryGetStringField(TEXT("fullName"), FullName) || FullName.TrimStartAndEnd().IsEmpty())
			{
				return MakeErrorResult(TEXT("MissingField"), TEXT("Missing required field 'fullName'."));
			}
			FullName = FullName.TrimStartAndEnd();

			FAutomationTestInfo TestInfo;
			if (!FindAutomationTestByFullName(FullName, TestInfo))
			{
				const FString Message = FString::Printf(TEXT("Automation test '%s' was not found."), *FullName);
				TSharedPtr<FJsonObject> ErrorObject = MakeErrorObject(TEXT("TestNotFound"), Message);
				ErrorObject->SetStringField(TEXT("fullName"), FullName);
				return MakeExecutionResult(Message, ErrorObject, true);
			}

			const int32 TimeoutSeconds = GetIntArgument(Arguments, TEXT("timeoutSeconds"), DefaultAutomationTimeoutSeconds, 1, MaxAutomationTimeoutSeconds);
			const TArray<FString> Tags = GetStringArrayArgument(Arguments, TEXT("tags"));
			const FString RunId = MakeRunId();
			FAutomationRunState State = MakeRunStateFromTestInfo(RunId, TestInfo, Tags, TimeoutSeconds);
			SaveStateFile(State);
			SetActiveRunState(State);
			LastPersistedHeartbeatUtc = State.LastHeartbeatUtc;
			EnsureAutomationTicker();

			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetStringField(TEXT("runId"), State.RunId);
			Content->SetStringField(TEXT("acceptedAt"), State.AcceptedAtUtc.ToIso8601());
			Content->SetObjectField(TEXT("matchedTest"), MakeMatchedTestObject(State));
			Content->SetStringField(TEXT("initialStatus"), State.Status);
			Content->SetStringField(TEXT("reportPath"), MakeReportRelativePath(State.RunId));
			Content->SetStringField(TEXT("pollingHint"), TEXT("Poll automation_report every 1-2 seconds; tests typically complete in seconds-to-minutes."));
			return MakeExecutionResult(
				FString::Printf(TEXT("Queued automation test '%s' as run '%s'."), *State.FullName, *State.RunId),
				Content,
				false);
		}

		FUnrealMcpExecutionResult AutomationReportTool(const FJsonObject& Arguments)
		{
			FString RunId;
			if (!Arguments.TryGetStringField(TEXT("runId"), RunId) || RunId.TrimStartAndEnd().IsEmpty() || !IsSafeRunId(RunId))
			{
				return MakeErrorResult(TEXT("RunNotFound"), TEXT("Automation run was not found."));
			}
			RunId = RunId.TrimStartAndEnd();

			FAutomationRunState State;
			{
				FScopeLock Lock(&GAutomationRunMutex);
				if (bHasActiveAutomationRun && GActiveAutomationRun.RunId == RunId)
				{
					State = GActiveAutomationRun;
				}
			}

			if (State.RunId.IsEmpty() && !LoadStateFromFile(RunId, State))
			{
				const FString Message = FString::Printf(TEXT("Automation run '%s' was not found."), *RunId);
				TSharedPtr<FJsonObject> ErrorObject = MakeErrorObject(TEXT("RunNotFound"), Message);
				ErrorObject->SetStringField(TEXT("runId"), RunId);
				return MakeExecutionResult(Message, ErrorObject, true);
			}

			const FString StaleReason = GetStaleReason(State, FDateTime::UtcNow());
			if (!StaleReason.IsEmpty())
			{
				MarkStateStale(State, StaleReason);
				ClearActiveRunIfMatching(State.RunId);
			}

			return MakeExecutionResult(
				FString::Printf(TEXT("Automation run '%s' is %s."), *State.RunId, *NormalizeStatusForPublicReport(State, FDateTime::UtcNow())),
				MakePublicReportObject(State),
				false);
		}
	}

	bool TryExecuteAutomationTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult)
	{
		if (ToolName == TEXT("unreal.automation_list"))
		{
			OutResult = AutomationListTool(Arguments);
			return true;
		}
		if (ToolName == TEXT("unreal.automation_run"))
		{
			OutResult = AutomationRunTool(Arguments);
			return true;
		}
		if (ToolName == TEXT("unreal.automation_report"))
		{
			OutResult = AutomationReportTool(Arguments);
			return true;
		}
		return false;
	}

	void MarkActiveAutomationRunStaleOnShutdown()
	{
		FAutomationRunState State;
		{
			FScopeLock Lock(&GAutomationRunMutex);
			if (!bHasActiveAutomationRun)
			{
				if (GAutomationTickerHandle.IsValid())
				{
					FTSTicker::GetCoreTicker().RemoveTicker(GAutomationTickerHandle);
					GAutomationTickerHandle.Reset();
				}
				LastPersistedHeartbeatUtc = FDateTime();
				return;
			}

			State = GActiveAutomationRun;
			bHasActiveAutomationRun = false;
			GActiveAutomationRun = FAutomationRunState();
			LastPersistedHeartbeatUtc = FDateTime();
		}

		if (GAutomationTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GAutomationTickerHandle);
			GAutomationTickerHandle.Reset();
		}

		if (IsActiveStatus(State.Status))
		{
			MarkStateStale(State, TEXT("editor_shutdown"), false);
		}
	}

#if WITH_DEV_AUTOMATION_TESTS
	void ResetAutomationToolStateForTests()
	{
		FScopeLock Lock(&GAutomationRunMutex);
		bHasActiveAutomationRun = false;
		GActiveAutomationRun = FAutomationRunState();
		LastPersistedHeartbeatUtc = FDateTime();
		bSuppressAutomationFrameworkStartForTests = false;
		if (GAutomationTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GAutomationTickerHandle);
			GAutomationTickerHandle.Reset();
		}
	}

	void SetAutomationFrameworkStartSuppressedForTests(bool bSuppress)
	{
		bSuppressAutomationFrameworkStartForTests = bSuppress;
	}

	FString WriteAutomationRunStateForTests(
		const FString& RunId,
		const FString& Status,
		const FString& FullName,
		const FString& DisplayName,
		const FDateTime& StartedAtUtc,
		int32 TimeoutSeconds)
	{
		FAutomationRunState State;
		State.RunId = RunId;
		State.Status = Status;
		State.FullName = FullName;
		State.DisplayName = DisplayName;
		State.PrettyName = DisplayName;
		State.Flags = { TEXT("EditorContext"), TEXT("EngineFilter") };
		State.AcceptedAtUtc = StartedAtUtc;
		State.StartedAtUtc = StartedAtUtc;
		State.LastHeartbeatUtc = StartedAtUtc;
		State.TimeoutSeconds = FMath::Clamp(TimeoutSeconds, 1, MaxAutomationTimeoutSeconds);
		SaveStateFile(State);
		return MakeReportPath(RunId);
	}

	bool DeleteAutomationRunStateForTests(const FString& RunId)
	{
		if (!IsSafeRunId(RunId))
		{
			return false;
		}
		return IFileManager::Get().Delete(*MakeReportPath(RunId), false, true);
	}
#endif
}
