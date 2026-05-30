#include "UnrealMcpCapturedArgsStore.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMcpHashUtils.h"
#include "UnrealMcpToolRegistry.h"

namespace UnrealMcp::CapturedArgsStore
{
	namespace
	{
		constexpr const TCHAR* kCapturedArgsRelativeRoot = TEXT("CapturedToolArgs");

		FString CapturedArgsSanitizePathComponent(FString Value)
		{
			Value = Value.TrimStartAndEnd();
			FString Result;
			for (const TCHAR Character : Value)
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
			while (Result.Contains(TEXT("..")))
			{
				Result = Result.Replace(TEXT(".."), TEXT("."));
			}
			Result.RemoveFromStart(TEXT("."));
			Result.RemoveFromEnd(TEXT("."));
			Result.RemoveFromStart(TEXT("-"));
			Result.RemoveFromEnd(TEXT("-"));
			return Result.IsEmpty() ? TEXT("capture") : Result.Left(160);
		}

		FString CapturedArgsShaPathForJsonPath(const FString& JsonPath)
		{
			return JsonPath + TEXT(".sha256");
		}

		bool CapturedArgsSerializeCompactJsonObject(const TSharedPtr<FJsonObject>& Object, FString& OutJson)
		{
			if (!Object.IsValid())
			{
				return false;
			}
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
			return FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		}

		bool CapturedArgsWriteStringAtomic(const FString& TargetPath, const FString& Text)
		{
			const FString TargetDir = FPaths::GetPath(TargetPath);
			if (!IFileManager::Get().MakeDirectory(*TargetDir, true))
			{
				return false;
			}

			const FString TempPath = FString::Printf(TEXT("%s.tmp.%s"), *TargetPath, *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
			if (!FFileHelper::SaveStringToFile(Text, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				return false;
			}
			if (!IFileManager::Get().Move(*TargetPath, *TempPath, true, true))
			{
				IFileManager::Get().Delete(*TempPath, false, true);
				return false;
			}
			return true;
		}

		FString CapturedArgsNormalizePathForCompare(FString Path)
		{
			Path = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(Path);
			Path.RemoveFromEnd(TEXT("/"));
			return Path;
		}

		bool CapturedArgsIsUnderRoot(const FString& CandidatePath)
		{
			const FString Root = CapturedArgsNormalizePathForCompare(GetCapturedArgsRoot());
			const FString Candidate = CapturedArgsNormalizePathForCompare(CandidatePath);
			return Candidate.Equals(Root, ESearchCase::IgnoreCase)
				|| Candidate.StartsWith(Root + TEXT("/"), ESearchCase::IgnoreCase);
		}

		bool CapturedArgsResolveCaptureRef(const FString& CaptureRef, FString& OutJsonPath, FString& OutError)
		{
			FString NormalizedRef = CaptureRef.TrimStartAndEnd();
			NormalizedRef.ReplaceInline(TEXT("\\"), TEXT("/"));
			while (NormalizedRef.StartsWith(TEXT("/")))
			{
				NormalizedRef.RightChopInline(1);
			}

			const FString RequiredPrefix = FString(kCapturedArgsRelativeRoot) + TEXT("/");
			if (!NormalizedRef.StartsWith(RequiredPrefix, ESearchCase::CaseSensitive))
			{
				OutError = TEXT("captureRef must start with CapturedToolArgs/.");
				return false;
			}

			if (!NormalizedRef.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase))
			{
				OutError = TEXT("captureRef must target a .json captured args file.");
				return false;
			}

			const FString RelativeToStore = NormalizedRef.RightChop(RequiredPrefix.Len());
			const FString CandidatePath = FPaths::Combine(GetCapturedArgsRoot(), RelativeToStore);
			if (!CapturedArgsIsUnderRoot(CandidatePath))
			{
				OutError = TEXT("captureRef resolved outside the captured args store.");
				return false;
			}

			OutJsonPath = FPaths::ConvertRelativePathToFull(CandidatePath);
			return true;
		}

		bool CapturedArgsDeleteJsonAndSha(const FString& JsonPath)
		{
			const bool bDeletedJson = IFileManager::Get().Delete(*JsonPath, false, true, true);
			const FString ShaPath = CapturedArgsShaPathForJsonPath(JsonPath);
			const bool bDeletedSha = !FPaths::FileExists(ShaPath) || IFileManager::Get().Delete(*ShaPath, false, true, true);
			return bDeletedJson && bDeletedSha;
		}

		struct FCapturedArgsStoreFile
		{
			FString JsonPath;
			FDateTime Timestamp;
			int64 TotalBytes = 0;
			bool bDeleted = false;
		};

		int64 CapturedArgsFileSizeOrZero(const FString& Path)
		{
			const int64 FileSize = IFileManager::Get().FileSize(*Path);
			return FileSize > 0 ? FileSize : 0;
		}
	}

	FString GetCapturedArgsRoot()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp"), kCapturedArgsRelativeRoot));
	}

	bool WriteCapturedArgs(
		const FString& SessionId,
		const FString& EventId,
		const FString& ToolName,
		const FString& TimestampUtc,
		const UnrealMcp::CaptureRedaction::FRedactionResult& Redacted,
		FString& OutCaptureRef,
		FString& OutSha256)
	{
		OutCaptureRef.Reset();
		OutSha256.Reset();

		if (!Redacted.CaptureStatus.Equals(TEXT("captured"), ESearchCase::CaseSensitive)
			&& !Redacted.CaptureStatus.Equals(TEXT("redacted"), ESearchCase::CaseSensitive))
		{
			return true;
		}

		if (!Redacted.SanitizedArguments.IsValid())
		{
			return false;
		}

		if (SessionId.TrimStartAndEnd().IsEmpty() || EventId.TrimStartAndEnd().IsEmpty())
		{
			return false;
		}

		const FString SafeSessionId = CapturedArgsSanitizePathComponent(SessionId);
		const FString SafeEventId = CapturedArgsSanitizePathComponent(EventId);
		const FString CaptureRef = FString::Printf(TEXT("%s/%s/%s.json"), kCapturedArgsRelativeRoot, *SafeSessionId, *SafeEventId);
		const FString JsonPath = FPaths::Combine(GetCapturedArgsRoot(), SafeSessionId, SafeEventId + TEXT(".json"));
		if (!CapturedArgsIsUnderRoot(JsonPath))
		{
			return false;
		}

		TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
		Content->SetStringField(TEXT("eventId"), EventId.TrimStartAndEnd());
		Content->SetStringField(TEXT("sessionId"), SessionId.TrimStartAndEnd());
		Content->SetStringField(TEXT("tool"), ToolName);
		Content->SetStringField(TEXT("timestampUtc"), TimestampUtc.TrimStartAndEnd());
		Content->SetObjectField(TEXT("sanitizedArguments"), Redacted.SanitizedArguments);
		Content->SetObjectField(TEXT("redactionSummary"), Redacted.RedactionSummaryPublic.IsValid() ? Redacted.RedactionSummaryPublic : MakeShared<FJsonObject>());
		Content->SetStringField(TEXT("captureStatus"), Redacted.CaptureStatus);
		Content->SetNumberField(TEXT("originalSize"), Redacted.OriginalSize);
		Content->SetNumberField(TEXT("storedSize"), Redacted.StoredSize);
		Content->SetObjectField(TEXT("toolPolicyAtCapture"), UnrealMcp::MakeToolPolicyObject(ToolName));
		Content->SetNumberField(TEXT("captureSchemaVersion"), UnrealMcp::CaptureRedaction::kCaptureSchemaVersion);

		FString JsonText;
		if (!CapturedArgsSerializeCompactJsonObject(Content, JsonText))
		{
			return false;
		}

		const FString Sha256 = UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(JsonText);
		if (!CapturedArgsWriteStringAtomic(JsonPath, JsonText))
		{
			return false;
		}
		if (!CapturedArgsWriteStringAtomic(CapturedArgsShaPathForJsonPath(JsonPath), Sha256 + LINE_TERMINATOR))
		{
			IFileManager::Get().Delete(*JsonPath, false, true);
			return false;
		}

		OutCaptureRef = CaptureRef;
		OutSha256 = Sha256;
		return true;
	}

	bool ReadCapturedArgs(
		const FString& CaptureRef,
		TSharedPtr<FJsonObject>& OutContent,
		FString& OutError)
	{
		OutContent.Reset();
		OutError.Reset();

		FString JsonPath;
		if (!CapturedArgsResolveCaptureRef(CaptureRef, JsonPath, OutError))
		{
			return false;
		}

		FString JsonText;
		if (!FFileHelper::LoadFileToString(JsonText, *JsonPath))
		{
			OutError = FString::Printf(TEXT("Failed to read captured args file '%s'."), *JsonPath);
			return false;
		}

		FString ExpectedSha256;
		const FString ShaPath = CapturedArgsShaPathForJsonPath(JsonPath);
		if (!FFileHelper::LoadFileToString(ExpectedSha256, *ShaPath))
		{
			OutError = FString::Printf(TEXT("Failed to read captured args sha file '%s'."), *ShaPath);
			return false;
		}
		ExpectedSha256 = ExpectedSha256.TrimStartAndEnd();

		const FString ActualSha256 = UnrealMcp::HashUtils::Sha256LowerHexFromUtf8(JsonText);
		if (!ActualSha256.Equals(ExpectedSha256, ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(TEXT("Captured args sha mismatch for '%s'."), *CaptureRef);
			return false;
		}

		TSharedPtr<FJsonObject> Parsed;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
		{
			OutError = FString::Printf(TEXT("Failed to parse captured args JSON '%s'."), *CaptureRef);
			return false;
		}

		OutContent = Parsed;
		return true;
	}

	bool PruneCapturedArgs(int32 MaxAgeDays, int64 MaxTotalBytes, FString* OutError)
	{
		if (OutError)
		{
			OutError->Reset();
		}

		const FString Root = GetCapturedArgsRoot();
		if (!FPaths::DirectoryExists(Root))
		{
			return true;
		}

		TArray<FString> JsonFiles;
		IFileManager::Get().FindFilesRecursive(JsonFiles, *Root, TEXT("*.json"), true, false);

		TArray<FCapturedArgsStoreFile> Files;
		int64 TotalBytes = 0;
		for (const FString& JsonPath : JsonFiles)
		{
			FCapturedArgsStoreFile File;
			File.JsonPath = JsonPath;
			File.Timestamp = IFileManager::Get().GetTimeStamp(*JsonPath);
			File.TotalBytes = CapturedArgsFileSizeOrZero(JsonPath) + CapturedArgsFileSizeOrZero(CapturedArgsShaPathForJsonPath(JsonPath));
			TotalBytes += File.TotalBytes;
			Files.Add(MoveTemp(File));
		}

		Files.Sort([](const FCapturedArgsStoreFile& Left, const FCapturedArgsStoreFile& Right)
		{
			return Left.Timestamp < Right.Timestamp;
		});

		const bool bUseAgeLimit = MaxAgeDays > 0;
		const FDateTime OldestAllowed = FDateTime::UtcNow() - FTimespan::FromDays(MaxAgeDays);
		for (FCapturedArgsStoreFile& File : Files)
		{
			if (bUseAgeLimit && File.Timestamp < OldestAllowed)
			{
				if (!CapturedArgsDeleteJsonAndSha(File.JsonPath))
				{
					if (OutError)
					{
						*OutError = FString::Printf(TEXT("Failed to prune captured args file '%s'."), *File.JsonPath);
					}
					return false;
				}
				File.bDeleted = true;
				TotalBytes -= File.TotalBytes;
			}
		}

		if (MaxTotalBytes > 0)
		{
			for (FCapturedArgsStoreFile& File : Files)
			{
				if (TotalBytes <= MaxTotalBytes)
				{
					break;
				}
				if (File.bDeleted)
				{
					continue;
				}
				if (!CapturedArgsDeleteJsonAndSha(File.JsonPath))
				{
					if (OutError)
					{
						*OutError = FString::Printf(TEXT("Failed to prune captured args file '%s'."), *File.JsonPath);
					}
					return false;
				}
				File.bDeleted = true;
				TotalBytes -= File.TotalBytes;
			}
		}

		return true;
	}
}
