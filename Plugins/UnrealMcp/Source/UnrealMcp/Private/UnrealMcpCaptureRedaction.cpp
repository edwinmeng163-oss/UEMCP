#include "UnrealMcpCaptureRedaction.h"

#include "Containers/StringConv.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformProcess.h"
#include "Internationalization/Regex.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMcpCapturedArgsStore.h"
#include "UnrealMcpSession.h"

namespace UnrealMcp::CaptureRedaction
{
	namespace
	{
		struct FRedactionStats
		{
			int32 RedactedSecretFields = 0;
			int32 RedactedPaths = 0;
			int32 OmittedOversized = 0;
			int32 SkippedToolDenylist = 0;
			int32 OversizedTotal = 0;
		};

		struct FToolCaptureRule
		{
			const TCHAR* ToolName;
			bool bSkipEntireTool;
			const TCHAR* Reason;
		};

		const FToolCaptureRule GToolCaptureRules[] = {
			{ TEXT("unreal.execute_python"), true, TEXT("command may contain arbitrary Python source, secrets, or credentials") },
			{ TEXT("unreal.execute_python_file"), true, TEXT("scriptPath and args may expose local code paths, source, or secrets") },
			{ TEXT("unreal.execute_console_command"), true, TEXT("command may contain arbitrary editor or runtime console operations") },
			{ TEXT("unreal.code_preview_change"), true, TEXT("edits contains source file edit content") },
			{ TEXT("unreal.mcp_patch_scaffold_patch"), true, TEXT("newText and findText may contain patch source") },
			{ TEXT("unreal.mcp_validate_cpp_patch"), true, TEXT("patchText and snippetText may contain C++ source") },
		};

		bool SerializeCompactJsonObject(const TSharedPtr<FJsonObject>& Object, FString& OutJson)
		{
			if (!Object.IsValid())
			{
				OutJson = TEXT("{}");
				return true;
			}

			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
			return FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		}

		int32 Utf8ByteLength(const FString& Value)
		{
			const FTCHARToUTF8 Utf8Value(*Value);
			return Utf8Value.Length();
		}

		int32 CompactJsonByteLength(const TSharedPtr<FJsonObject>& Object)
		{
			FString Json;
			return SerializeCompactJsonObject(Object, Json) ? Utf8ByteLength(Json) : 0;
		}

		TSharedPtr<FJsonObject> MakeSummaryObject(const FRedactionStats& Stats)
		{
			TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
			Summary->SetNumberField(TEXT("redactedSecretFields"), Stats.RedactedSecretFields);
			Summary->SetNumberField(TEXT("redactedPaths"), Stats.RedactedPaths);
			Summary->SetNumberField(TEXT("omittedOversized"), Stats.OmittedOversized);
			Summary->SetNumberField(TEXT("skippedToolDenylist"), Stats.SkippedToolDenylist);
			Summary->SetNumberField(TEXT("oversizedTotal"), Stats.OversizedTotal);
			return Summary;
		}

		bool IsSecretFieldName(const FString& FieldName)
		{
			static const FRegexPattern SecretFieldPattern(TEXT("(?i)(token|api[_-]?key|password|passwd|secret|credential|authorization|cookie)"));
			FRegexMatcher Matcher(SecretFieldPattern, FieldName);
			return Matcher.FindNext();
		}

		bool ShouldSkipEntireTool(const FString& ToolName)
		{
			for (const FToolCaptureRule& Rule : GToolCaptureRules)
			{
				if (ToolName.Equals(Rule.ToolName, ESearchCase::CaseSensitive) && Rule.bSkipEntireTool)
				{
					return true;
				}
			}
			return false;
		}

		struct FPathPrefix
		{
			FString Prefix;
			FString Placeholder;
		};

		void AddUniquePathPrefix(TArray<FPathPrefix>& Prefixes, const FString& Prefix, const FString& Placeholder)
		{
			if (!Prefix.IsEmpty())
			{
				for (const FPathPrefix& ExistingPrefix : Prefixes)
				{
					if (ExistingPrefix.Placeholder == Placeholder && ExistingPrefix.Prefix.Equals(Prefix, ESearchCase::IgnoreCase))
					{
						return;
					}
				}
				Prefixes.Add({ Prefix, Placeholder });
			}
		}

		void AddPathPrefixVariant(TArray<FPathPrefix>& Prefixes, FString Prefix, const FString& Placeholder)
		{
			FPaths::NormalizeDirectoryName(Prefix);
			Prefix.RemoveFromEnd(TEXT("/"));
			Prefix.RemoveFromEnd(TEXT("\\"));
			AddUniquePathPrefix(Prefixes, Prefix, Placeholder);

			const FString BackslashPrefix = Prefix.Replace(TEXT("/"), TEXT("\\"));
			AddUniquePathPrefix(Prefixes, BackslashPrefix, Placeholder);
		}

		void AddPathPrefix(TArray<FPathPrefix>& Prefixes, const FString& Prefix, const FString& Placeholder)
		{
			AddPathPrefixVariant(Prefixes, Prefix, Placeholder);
			AddPathPrefixVariant(Prefixes, FPaths::ConvertRelativePathToFull(Prefix), Placeholder);
		}

		TArray<FPathPrefix> MakePathPrefixes()
		{
			TArray<FPathPrefix> Prefixes;
			AddPathPrefix(Prefixes, FPaths::ProjectDir(), TEXT("<project>"));
			AddPathPrefix(Prefixes, FPlatformProcess::UserDir(), TEXT("<home>"));
			Prefixes.Sort([](const FPathPrefix& Left, const FPathPrefix& Right)
			{
				return Left.Prefix.Len() > Right.Prefix.Len();
			});
			return Prefixes;
		}

		FString RedactPathPrefixes(const FString& Value, int32& OutReplacementCount)
		{
			FString Redacted = Value;
			for (const FPathPrefix& Prefix : MakePathPrefixes())
			{
				const FString SlashPrefix = Prefix.Prefix + TEXT("/");
				const FString BackslashPrefix = Prefix.Prefix + TEXT("\\");
				const FString PlaceholderPrefix = Prefix.Placeholder + TEXT("/");
				OutReplacementCount += Redacted.ReplaceInline(*SlashPrefix, *PlaceholderPrefix, ESearchCase::IgnoreCase);
				OutReplacementCount += Redacted.ReplaceInline(*BackslashPrefix, *PlaceholderPrefix, ESearchCase::IgnoreCase);
				if (Redacted.Equals(Prefix.Prefix, ESearchCase::IgnoreCase))
				{
					Redacted = Prefix.Placeholder;
					++OutReplacementCount;
				}
			}
			return Redacted;
		}

		TSharedPtr<FJsonValue> SanitizeJsonValue(
			const FString& FieldName,
			const TSharedPtr<FJsonValue>& Value,
			FRedactionStats& Stats,
			int32 MaxValueChars);

		TSharedPtr<FJsonObject> SanitizeJsonObject(
			const TSharedPtr<FJsonObject>& Arguments,
			FRedactionStats& Stats,
			int32 MaxValueChars)
		{
			TSharedPtr<FJsonObject> Sanitized = MakeShared<FJsonObject>();
			if (!Arguments.IsValid())
			{
				return Sanitized;
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Arguments->Values)
			{
				Sanitized->SetField(Pair.Key, SanitizeJsonValue(Pair.Key, Pair.Value, Stats, MaxValueChars));
			}
			return Sanitized;
		}

		TSharedPtr<FJsonValue> SanitizeJsonValue(
			const FString& FieldName,
			const TSharedPtr<FJsonValue>& Value,
			FRedactionStats& Stats,
			int32 MaxValueChars)
		{
			if (!Value.IsValid() || Value->Type == EJson::Null)
			{
				return MakeShared<FJsonValueNull>();
			}

			if (IsSecretFieldName(FieldName))
			{
				++Stats.RedactedSecretFields;
				return MakeShared<FJsonValueString>(TEXT("<redacted:secret>"));
			}

			switch (Value->Type)
			{
			case EJson::String:
			{
				const FString OriginalString = Value->AsString();
				const int32 OriginalBytes = Utf8ByteLength(OriginalString);
				if (MaxValueChars > 0 && OriginalBytes > MaxValueChars)
				{
					++Stats.OmittedOversized;
					return MakeShared<FJsonValueString>(FString::Printf(TEXT("<omitted:oversized:%d bytes>"), OriginalBytes));
				}

				int32 PathReplacementCount = 0;
				const FString RedactedString = RedactPathPrefixes(OriginalString, PathReplacementCount);
				Stats.RedactedPaths += PathReplacementCount;
				return MakeShared<FJsonValueString>(RedactedString);
			}
			case EJson::Number:
				return MakeShared<FJsonValueNumber>(Value->AsNumber());
			case EJson::Boolean:
				return MakeShared<FJsonValueBoolean>(Value->AsBool());
			case EJson::Array:
			{
				TArray<TSharedPtr<FJsonValue>> SanitizedArray;
				for (const TSharedPtr<FJsonValue>& ArrayValue : Value->AsArray())
				{
					SanitizedArray.Add(SanitizeJsonValue(FieldName, ArrayValue, Stats, MaxValueChars));
				}
				return MakeShared<FJsonValueArray>(SanitizedArray);
			}
			case EJson::Object:
				return MakeShared<FJsonValueObject>(SanitizeJsonObject(Value->AsObject(), Stats, MaxValueChars));
			default:
				return MakeShared<FJsonValueNull>();
			}
		}

		FString MakeCaptureStatus(const FRedactionStats& Stats)
		{
			if (Stats.RedactedSecretFields > 0 || Stats.RedactedPaths > 0 || Stats.OmittedOversized > 0)
			{
				return TEXT("redacted");
			}
			return TEXT("captured");
		}
	}

	FRedactionResult SanitizeToolArguments_Pure(
		const FString& ToolName,
		const TSharedPtr<FJsonObject>& Arguments,
		int32 MaxValueChars,
		int32 MaxTotalChars)
	{
		FRedactionResult Result;
		FRedactionStats Stats;
		Result.CaptureStatus = TEXT("captured");

		const TSharedPtr<FJsonObject> SafeArguments = Arguments.IsValid() ? Arguments : MakeShared<FJsonObject>();
		Result.OriginalSize = CompactJsonByteLength(SafeArguments);

		if (ShouldSkipEntireTool(ToolName))
		{
			Stats.SkippedToolDenylist = 1;
			Result.CaptureStatus = TEXT("skipped");
			Result.RedactionSummaryPublic = MakeSummaryObject(Stats);
			return Result;
		}

		if (MaxTotalChars > 0 && Result.OriginalSize > MaxTotalChars)
		{
			Stats.OversizedTotal = 1;
			Result.CaptureStatus = TEXT("oversized");
			Result.RedactionSummaryPublic = MakeSummaryObject(Stats);
			return Result;
		}

		Result.SanitizedArguments = SanitizeJsonObject(SafeArguments, Stats, MaxValueChars);
		Result.StoredSize = CompactJsonByteLength(Result.SanitizedArguments);
		if (MaxTotalChars > 0 && Result.StoredSize > MaxTotalChars)
		{
			Stats.OversizedTotal = 1;
			Result.SanitizedArguments.Reset();
			Result.StoredSize = 0;
			Result.CaptureStatus = TEXT("oversized");
			Result.RedactionSummaryPublic = MakeSummaryObject(Stats);
			return Result;
		}

		Result.CaptureStatus = MakeCaptureStatus(Stats);
		Result.RedactionSummaryPublic = MakeSummaryObject(Stats);
		return Result;
	}

	void AttachCaptureMetadata(
		const TSharedPtr<FJsonObject>& Payload,
		const FString& ToolName,
		const FJsonObject& Arguments,
		const FString& EventId,
		int32 MaxValueChars,
		int32 MaxTotalChars)
	{
		if (!Payload.IsValid())
		{
			return;
		}

		TSharedPtr<FJsonObject> ArgumentObject = MakeShared<FJsonObject>();
		ArgumentObject->Values = Arguments.Values;
		const FRedactionResult Redaction = SanitizeToolArguments_Pure(ToolName, ArgumentObject, MaxValueChars, MaxTotalChars);

		Payload->SetNumberField(TEXT("captureSchemaVersion"), kCaptureSchemaVersion);
		Payload->SetStringField(TEXT("captureStatus"), Redaction.CaptureStatus);
		Payload->SetObjectField(TEXT("redactionSummaryPublic"), Redaction.RedactionSummaryPublic.IsValid() ? Redaction.RedactionSummaryPublic : MakeShared<FJsonObject>());

		FString CaptureRef;
		FString CaptureSha256;
		if (UnrealMcp::CapturedArgsStore::WriteCapturedArgs(
			UnrealMcp::GetLaunchSessionId(),
			EventId,
			ToolName,
			FDateTime::UtcNow().ToIso8601(),
			Redaction,
			CaptureRef,
			CaptureSha256)
			&& !CaptureRef.IsEmpty()
			&& !CaptureSha256.IsEmpty())
		{
			Payload->SetStringField(TEXT("captureRef"), CaptureRef);
			Payload->SetStringField(TEXT("captureSha256"), CaptureSha256);
		}
	}
}
