#include "UnrealMcpDiagnosticsTools.h"

#include "CoreGlobals.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeLock.h"
#include "UnrealMcpModule.h"

namespace UnrealMcp
{
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);

	namespace
	{
		constexpr int32 DiagnosticRingCapacity = 5000;
		constexpr int32 DefaultDiagnosticsLimit = 200;
		constexpr int32 MaxDiagnosticsLimit = 1000;

		enum class EDiagnosticSeverity : uint8
		{
			Warning,
			Error,
			Fatal
		};

		enum class EDiagnosticClass : uint8
		{
			Compile,
			MapCheck,
			Content,
			Automation,
			LogWarning,
			LogError
		};

		struct FDiagnosticEntry
		{
			FDateTime TimestampUtc;
			EDiagnosticSeverity Severity = EDiagnosticSeverity::Warning;
			EDiagnosticClass DiagnosticClass = EDiagnosticClass::LogWarning;
			FString Source;
			FString Message;
		};

		struct FDiagnosticsSnapshot
		{
			TArray<FDiagnosticEntry> Entries;
			FDateTime StartedAtUtc;
			bool bOverflow = false;
		};

		FCriticalSection GDiagnosticsMutex;
		TArray<FDiagnosticEntry> GDiagnosticsRing;
		int32 GNextDiagnosticWriteIndex = 0;
		FDateTime GDiagnosticsStartedAtUtc;
		bool bDiagnosticsRingOverflow = false;
		bool bDiagnosticsListenerRegistered = false;

		const TMap<FName, EDiagnosticClass>& GetDiagnosticClassByCategory()
		{
			static const TMap<FName, EDiagnosticClass> ClassByCategory = []()
			{
				TMap<FName, EDiagnosticClass> Map;
				Map.Add(FName(TEXT("LogMapCheck")), EDiagnosticClass::MapCheck);
				Map.Add(FName(TEXT("LogBlueprintEditor")), EDiagnosticClass::Compile);
				Map.Add(FName(TEXT("LogKismet")), EDiagnosticClass::Compile);
				Map.Add(FName(TEXT("LogKismetCompiler")), EDiagnosticClass::Compile);
				Map.Add(FName(TEXT("LogCompile")), EDiagnosticClass::Compile);
				Map.Add(FName(TEXT("LogLinker")), EDiagnosticClass::Content);
				Map.Add(FName(TEXT("LogPackageName")), EDiagnosticClass::Content);
				Map.Add(FName(TEXT("LogStreaming")), EDiagnosticClass::Content);
				Map.Add(FName(TEXT("LogAutomation")), EDiagnosticClass::Automation);
				Map.Add(FName(TEXT("LogAutomationController")), EDiagnosticClass::Automation);
				return Map;
			}();
			return ClassByCategory;
		}

		ELogVerbosity::Type GetDiagnosticsCleanVerbosity(ELogVerbosity::Type Verbosity)
		{
			return static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
		}

		bool TryMapDiagnosticSeverity(ELogVerbosity::Type Verbosity, EDiagnosticSeverity& OutSeverity)
		{
			switch (GetDiagnosticsCleanVerbosity(Verbosity))
			{
			case ELogVerbosity::Fatal:
				OutSeverity = EDiagnosticSeverity::Fatal;
				return true;
			case ELogVerbosity::Error:
				OutSeverity = EDiagnosticSeverity::Error;
				return true;
			case ELogVerbosity::Warning:
				OutSeverity = EDiagnosticSeverity::Warning;
				return true;
			default:
				return false;
			}
		}

		EDiagnosticClass ClassifyDiagnostic(const FName& Category, EDiagnosticSeverity Severity)
		{
			if (const EDiagnosticClass* KnownClass = GetDiagnosticClassByCategory().Find(Category))
			{
				return *KnownClass;
			}

			return Severity == EDiagnosticSeverity::Warning
				? EDiagnosticClass::LogWarning
				: EDiagnosticClass::LogError;
		}

		FString DiagnosticSeverityToString(EDiagnosticSeverity Severity)
		{
			switch (Severity)
			{
			case EDiagnosticSeverity::Fatal:
				return TEXT("fatal");
			case EDiagnosticSeverity::Error:
				return TEXT("error");
			case EDiagnosticSeverity::Warning:
			default:
				return TEXT("warning");
			}
		}

		FString DiagnosticClassToString(EDiagnosticClass DiagnosticClass)
		{
			switch (DiagnosticClass)
			{
			case EDiagnosticClass::Compile:
				return TEXT("compile");
			case EDiagnosticClass::MapCheck:
				return TEXT("map_check");
			case EDiagnosticClass::Content:
				return TEXT("content");
			case EDiagnosticClass::Automation:
				return TEXT("automation");
			case EDiagnosticClass::LogError:
				return TEXT("log_error");
			case EDiagnosticClass::LogWarning:
			default:
				return TEXT("log_warning");
			}
		}

		bool TryParseDiagnosticClass(const FString& Text, FString& OutClass)
		{
			const FString Normalized = Text.TrimStartAndEnd().ToLower();
			if (Normalized == TEXT("compile")
				|| Normalized == TEXT("map_check")
				|| Normalized == TEXT("content")
				|| Normalized == TEXT("automation")
				|| Normalized == TEXT("log_warning")
				|| Normalized == TEXT("log_error"))
			{
				OutClass = Normalized;
				return true;
			}
			return false;
		}

		bool ParseDiagnosticsIsoUtcTimestamp(const FString& Text, FDateTime& OutTimestampUtc)
		{
			OutTimestampUtc = FDateTime();
			const FString Trimmed = Text.TrimStartAndEnd();
			const bool bHasUtcSuffix = Trimmed.EndsWith(TEXT("Z"), ESearchCase::IgnoreCase) || Trimmed.EndsWith(TEXT("+00:00"));
			return bHasUtcSuffix && FDateTime::ParseIso8601(*Trimmed, OutTimestampUtc);
		}

		int32 GetDiagnosticsLimit(const FJsonObject& Arguments)
		{
			double Limit = static_cast<double>(DefaultDiagnosticsLimit);
			if (Arguments.TryGetNumberField(TEXT("limit"), Limit))
			{
				return FMath::Clamp(static_cast<int32>(Limit), 1, MaxDiagnosticsLimit);
			}
			return DefaultDiagnosticsLimit;
		}

		TArray<FString> ReadDiagnosticsStringArrayValues(const TArray<TSharedPtr<FJsonValue>>& JsonValues)
		{
			TArray<FString> Values;
			for (const TSharedPtr<FJsonValue>& JsonValue : JsonValues)
			{
				FString Value;
				if (JsonValue.IsValid() && JsonValue->TryGetString(Value))
				{
					Values.Add(Value);
				}
				else
				{
					Values.Add(FString());
				}
			}
			return Values;
		}

		bool TryReadClassFilter(const FJsonObject& Arguments, TArray<FString>& OutClasses, FString& OutError)
		{
			OutClasses.Reset();
			const TArray<TSharedPtr<FJsonValue>>* JsonValues = nullptr;
			if (!Arguments.HasField(TEXT("classes")))
			{
				return true;
			}
			if (!Arguments.TryGetArrayField(TEXT("classes"), JsonValues) || JsonValues == nullptr)
			{
				OutError = TEXT("Optional field 'classes' must be an array of diagnostic class strings.");
				return false;
			}

			for (const FString& RawClass : ReadDiagnosticsStringArrayValues(*JsonValues))
			{
				FString NormalizedClass;
				if (!TryParseDiagnosticClass(RawClass, NormalizedClass))
				{
					OutError = FString::Printf(
						TEXT("Invalid diagnostic class '%s'. Expected one of compile, map_check, content, automation, log_warning, log_error."),
						*RawClass);
					return false;
				}
				OutClasses.AddUnique(NormalizedClass);
			}
			return true;
		}

		TArray<TSharedPtr<FJsonValue>> MakeDiagnosticsStringArrayValues(const TArray<FString>& Values)
		{
			TArray<TSharedPtr<FJsonValue>> JsonValues;
			for (const FString& Value : Values)
			{
				JsonValues.Add(MakeShared<FJsonValueString>(Value));
			}
			return JsonValues;
		}

		FString GetDiagnosticsSuggestedFixForMessage(const FString& Message)
		{
			if (Message.Contains(TEXT("Package Version: 0")) || Message.Contains(TEXT("Min Required Version:")))
			{
				return TEXT("Open the asset in editor and Save to refresh CustomVersion stamps.");
			}
			if (Message.Contains(TEXT("_ExternalActors_")) && Message.Contains(TEXT("was not available")))
			{
				return TEXT("Save the parent umap to drop the stale external-actor reference.");
			}
			if (Message.Contains(TEXT("Failed to load package")))
			{
				return TEXT("Check if the package was deleted/renamed, or if its EngineAssociation differs from the current editor.");
			}
			if (Message.Contains(TEXT("is not backwards compatible")))
			{
				return TEXT("Asset was saved with a newer engine. Open with the matching engine version, resave, or revert the source change.");
			}
			if (Message.Contains(TEXT("Live Coding")))
			{
				return TEXT("Close Unreal Editor or disable Live Coding before running UBT; Live Coding can lock plugin binaries.");
			}
			return FString();
		}

		TSharedPtr<FJsonObject> MakeDiagnosticsErrorObject(const FString& ErrorKind, const FString& Message)
		{
			TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
			ErrorObject->SetBoolField(TEXT("isError"), true);
			ErrorObject->SetStringField(TEXT("errorKind"), ErrorKind);
			ErrorObject->SetStringField(TEXT("message"), Message);
			return ErrorObject;
		}

		FUnrealMcpExecutionResult MakeDiagnosticsErrorResult(const FString& ErrorKind, const FString& Message)
		{
			return MakeExecutionResult(Message, MakeDiagnosticsErrorObject(ErrorKind, Message), true);
		}

		void ResetDiagnosticsStateLocked(const FDateTime& StartedAtUtc)
		{
			GDiagnosticsRing.Reset();
			GDiagnosticsRing.Reserve(DiagnosticRingCapacity);
			GNextDiagnosticWriteIndex = 0;
			bDiagnosticsRingOverflow = false;
			GDiagnosticsStartedAtUtc = StartedAtUtc;
		}

		void AppendDiagnosticEntry(FDiagnosticEntry&& Entry)
		{
			FScopeLock Lock(&GDiagnosticsMutex);
			if (GDiagnosticsStartedAtUtc.GetTicks() <= 0)
			{
				GDiagnosticsStartedAtUtc = FDateTime::UtcNow();
			}

			if (GDiagnosticsRing.Num() < DiagnosticRingCapacity)
			{
				GDiagnosticsRing.Add(MoveTemp(Entry));
				GNextDiagnosticWriteIndex = GDiagnosticsRing.Num() % DiagnosticRingCapacity;
				return;
			}

			GDiagnosticsRing[GNextDiagnosticWriteIndex] = MoveTemp(Entry);
			GNextDiagnosticWriteIndex = (GNextDiagnosticWriteIndex + 1) % DiagnosticRingCapacity;
			bDiagnosticsRingOverflow = true;
		}

		void RecordDiagnosticLine(
			const FString& Message,
			ELogVerbosity::Type Verbosity,
			const FName& Category,
			const FDateTime& TimestampUtc)
		{
			EDiagnosticSeverity Severity;
			if (!TryMapDiagnosticSeverity(Verbosity, Severity))
			{
				return;
			}

			FDiagnosticEntry Entry;
			Entry.TimestampUtc = TimestampUtc;
			Entry.Severity = Severity;
			Entry.DiagnosticClass = ClassifyDiagnostic(Category, Severity);
			Entry.Source = Category.ToString();
			Entry.Message = Message;
			AppendDiagnosticEntry(MoveTemp(Entry));
		}

		FDiagnosticsSnapshot CaptureDiagnosticsSnapshot()
		{
			FDiagnosticsSnapshot Snapshot;
			FScopeLock Lock(&GDiagnosticsMutex);
			Snapshot.StartedAtUtc = GDiagnosticsStartedAtUtc;
			Snapshot.bOverflow = bDiagnosticsRingOverflow;
			Snapshot.Entries.Reserve(GDiagnosticsRing.Num());
			if (bDiagnosticsRingOverflow && GDiagnosticsRing.Num() == DiagnosticRingCapacity)
			{
				for (int32 Index = GNextDiagnosticWriteIndex; Index < GDiagnosticsRing.Num(); ++Index)
				{
					Snapshot.Entries.Add(GDiagnosticsRing[Index]);
				}
				for (int32 Index = 0; Index < GNextDiagnosticWriteIndex; ++Index)
				{
					Snapshot.Entries.Add(GDiagnosticsRing[Index]);
				}
			}
			else
			{
				Snapshot.Entries = GDiagnosticsRing;
			}
			return Snapshot;
		}

		TSharedPtr<FJsonObject> MakeDiagnosticEntryObject(const FDiagnosticEntry& Entry)
		{
			TSharedPtr<FJsonObject> EntryObject = MakeShared<FJsonObject>();
			EntryObject->SetStringField(TEXT("ts"), Entry.TimestampUtc.ToIso8601());
			EntryObject->SetStringField(TEXT("severity"), DiagnosticSeverityToString(Entry.Severity));
			EntryObject->SetStringField(TEXT("class"), DiagnosticClassToString(Entry.DiagnosticClass));
			EntryObject->SetStringField(TEXT("source"), Entry.Source);
			EntryObject->SetStringField(TEXT("message"), Entry.Message);
			const FString Suggested = GetDiagnosticsSuggestedFixForMessage(Entry.Message);
			if (!Suggested.IsEmpty())
			{
				EntryObject->SetStringField(TEXT("suggested"), Suggested);
			}
			return EntryObject;
		}

		bool ShouldIncludeDiagnosticEntry(
			const FDiagnosticEntry& Entry,
			const FDateTime& SinceUtc,
			const TSet<FString>& ClassFilter)
		{
			if (SinceUtc.GetTicks() > 0 && Entry.TimestampUtc < SinceUtc)
			{
				return false;
			}
			if (!ClassFilter.IsEmpty() && !ClassFilter.Contains(DiagnosticClassToString(Entry.DiagnosticClass)))
			{
				return false;
			}
			return true;
		}

		FUnrealMcpExecutionResult EditorDiagnosticsTool(const FJsonObject& Arguments)
		{
			FDateTime SinceUtc;
			FString SinceText;
			if (Arguments.TryGetStringField(TEXT("since"), SinceText) && !ParseDiagnosticsIsoUtcTimestamp(SinceText, SinceUtc))
			{
				return MakeDiagnosticsErrorResult(TEXT("InvalidSince"), TEXT("Optional field 'since' must be an ISO-8601 UTC timestamp such as 2026-05-19T12:00:00Z."));
			}

			TArray<FString> FilteredClasses;
			FString ClassFilterError;
			if (!TryReadClassFilter(Arguments, FilteredClasses, ClassFilterError))
			{
				return MakeDiagnosticsErrorResult(TEXT("InvalidClasses"), ClassFilterError);
			}
			TSet<FString> ClassFilter;
			for (const FString& FilteredClass : FilteredClasses)
			{
				ClassFilter.Add(FilteredClass);
			}

			const int32 Limit = GetDiagnosticsLimit(Arguments);
			const FDiagnosticsSnapshot Snapshot = CaptureDiagnosticsSnapshot();

			TArray<FDiagnosticEntry> MatchingEntries;
			for (const FDiagnosticEntry& Entry : Snapshot.Entries)
			{
				if (ShouldIncludeDiagnosticEntry(Entry, SinceUtc, ClassFilter))
				{
					MatchingEntries.Add(Entry);
				}
			}

			const int32 TotalCount = MatchingEntries.Num();
			const int32 StartIndex = FMath::Max(0, TotalCount - Limit);
			TArray<TSharedPtr<FJsonValue>> EntryValues;
			for (int32 Index = StartIndex; Index < MatchingEntries.Num(); ++Index)
			{
				EntryValues.Add(MakeShared<FJsonValueObject>(MakeDiagnosticEntryObject(MatchingEntries[Index])));
			}

			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetArrayField(TEXT("entries"), EntryValues);
			Content->SetNumberField(TEXT("totalCount"), TotalCount);
			Content->SetBoolField(TEXT("truncated"), TotalCount > EntryValues.Num());
			Content->SetStringField(TEXT("ringBufferStartedAt"), Snapshot.StartedAtUtc.ToIso8601());
			Content->SetBoolField(TEXT("ringBufferOverflow"), Snapshot.bOverflow);
			Content->SetArrayField(TEXT("filteredClasses"), MakeDiagnosticsStringArrayValues(FilteredClasses));

			return MakeExecutionResult(
				FString::Printf(TEXT("Returned %d of %d editor diagnostics entries."), EntryValues.Num(), TotalCount),
				Content,
				false);
		}

		class FUnrealMcpDiagnosticsOutputDevice final : public FOutputDevice
		{
		public:
			virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
			{
				RecordDiagnosticLine(V != nullptr ? FString(V) : FString(), Verbosity, Category, FDateTime::UtcNow());
			}
		};

		FUnrealMcpDiagnosticsOutputDevice GDiagnosticsOutputDevice;
	}

	void RegisterDiagnosticsListener()
	{
		GetDiagnosticClassByCategory();
		{
			FScopeLock Lock(&GDiagnosticsMutex);
			if (bDiagnosticsListenerRegistered)
			{
				return;
			}
			ResetDiagnosticsStateLocked(FDateTime::UtcNow());
			bDiagnosticsListenerRegistered = true;
		}

		if (GLog)
		{
			GLog->AddOutputDevice(&GDiagnosticsOutputDevice);
		}
	}

	void UnregisterDiagnosticsListener()
	{
		{
			FScopeLock Lock(&GDiagnosticsMutex);
			if (!bDiagnosticsListenerRegistered)
			{
				return;
			}
			bDiagnosticsListenerRegistered = false;
		}

		if (GLog)
		{
			GLog->RemoveOutputDevice(&GDiagnosticsOutputDevice);
		}
	}

	TSharedPtr<FJsonObject> CaptureDiagnosticsSummarySince(const FDateTime& SinceUtc, int32 ExcerptLimit)
	{
		const int32 ClampedLimit = FMath::Max(1, ExcerptLimit);
		const FDiagnosticsSnapshot Snapshot = CaptureDiagnosticsSnapshot();

		int32 ErrorCount = 0;
		int32 WarningCount = 0;
		int32 MatchingCount = 0;
		TArray<TSharedPtr<FJsonValue>> ExcerptValues;
		for (const FDiagnosticEntry& Entry : Snapshot.Entries)
		{
			if (!ShouldIncludeDiagnosticEntry(Entry, SinceUtc, TSet<FString>()))
			{
				continue;
			}

			++MatchingCount;
			if (Entry.Severity == EDiagnosticSeverity::Warning)
			{
				++WarningCount;
			}
			else
			{
				++ErrorCount;
			}

			if (ExcerptValues.Num() < ClampedLimit)
			{
				ExcerptValues.Add(MakeShared<FJsonValueObject>(MakeDiagnosticEntryObject(Entry)));
			}
		}

		TSharedPtr<FJsonObject> DiagnosticsObject = MakeShared<FJsonObject>();
		DiagnosticsObject->SetStringField(TEXT("startedAt"), SinceUtc.ToIso8601());
		DiagnosticsObject->SetNumberField(TEXT("errorCount"), ErrorCount);
		DiagnosticsObject->SetNumberField(TEXT("warningCount"), WarningCount);
		DiagnosticsObject->SetArrayField(TEXT("excerpt"), ExcerptValues);
		DiagnosticsObject->SetBoolField(TEXT("excerptTruncated"), MatchingCount > ExcerptValues.Num());
		return DiagnosticsObject;
	}

	bool TryExecuteDiagnosticsTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult)
	{
		if (ToolName == TEXT("unreal.editor_diagnostics"))
		{
			OutResult = EditorDiagnosticsTool(Arguments);
			return true;
		}
		return false;
	}

#if WITH_DEV_AUTOMATION_TESTS
	void ResetDiagnosticsStateForTests(const FDateTime& StartedAtUtc)
	{
		FScopeLock Lock(&GDiagnosticsMutex);
		ResetDiagnosticsStateLocked(StartedAtUtc);
	}

	void AppendDiagnosticLogLineForTests(
		const FString& Message,
		ELogVerbosity::Type Verbosity,
		const FName& Category,
		const FDateTime& TimestampUtc)
	{
		RecordDiagnosticLine(Message, Verbosity, Category, TimestampUtc);
	}
#endif
}
