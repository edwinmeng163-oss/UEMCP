// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.

#include "UnrealMcpUserToolRegistry.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealMcpHashUtils.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpPythonToolBridge.h"
#include "UnrealMcpToolRegistry.h"

namespace UnrealMcp::UserRegistry
{
	namespace
	{
		TMap<FString, FUserToolEntry> GUserTools;
		FString GUserToolsRoot;
		bool bUserToolRegistryInitialized = false;

		bool UserRegistryIsLowerHexSha256(const FString& Value)
		{
			if (Value.Len() != 64)
			{
				return false;
			}

			for (const TCHAR Character : Value)
			{
				const bool bDigit = Character >= TEXT('0') && Character <= TEXT('9');
				const bool bLowerHex = Character >= TEXT('a') && Character <= TEXT('f');
				if (!bDigit && !bLowerHex)
				{
					return false;
				}
			}
			return true;
		}

		void UserRegistryEnsureInitialized()
		{
			if (bUserToolRegistryInitialized)
			{
				return;
			}

			GUserToolsRoot = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), ::UnrealMcp::Extension::UserPyToolsRelativeRoot));
			FPaths::NormalizeFilename(GUserToolsRoot);
			FPaths::CollapseRelativeDirectories(GUserToolsRoot);
			GUserToolsRoot.RemoveFromEnd(TEXT("/"));
			bUserToolRegistryInitialized = true;
		}

		bool UserRegistryLoadJsonObject(const FString& Path, TSharedPtr<FJsonObject>& OutObject)
		{
			FString JsonText;
			if (!FFileHelper::LoadFileToString(JsonText, *Path))
			{
				return false;
			}

			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
			return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
		}

		bool UserRegistryIsSafeToolDirectoryName(const FString& DirectoryName)
		{
			if (DirectoryName.IsEmpty()
				|| DirectoryName.Contains(TEXT("/"), ESearchCase::CaseSensitive)
				|| DirectoryName.Contains(TEXT("\\"), ESearchCase::CaseSensitive)
				|| DirectoryName.Contains(TEXT(".."), ESearchCase::CaseSensitive)
				|| DirectoryName.Contains(TEXT(":"), ESearchCase::CaseSensitive)
				|| FPaths::IsRelative(DirectoryName) == false)
			{
				return false;
			}

			return true;
		}

		bool UserRegistryIsIgnoredGeneratedCacheDirectory(const FString& DirectoryName)
		{
			return DirectoryName.Equals(TEXT("__pycache__"), ESearchCase::CaseSensitive)
				|| DirectoryName.Equals(TEXT(".pytest_cache"), ESearchCase::CaseSensitive)
				|| DirectoryName.Equals(TEXT(".mypy_cache"), ESearchCase::CaseSensitive);
		}

		TArray<FString> UserRegistryReadStringArrayField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
		{
			TArray<FString> Values;
			const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
			if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, ArrayValues) || ArrayValues == nullptr)
			{
				return Values;
			}

			for (const TSharedPtr<FJsonValue>& Value : *ArrayValues)
			{
				FString StringValue;
				if (Value.IsValid() && Value->TryGetString(StringValue) && !StringValue.TrimStartAndEnd().IsEmpty())
				{
					Values.Add(StringValue.TrimStartAndEnd());
				}
			}
			return Values;
		}

		void UserRegistryAddRejection(FReloadResult& Result, const FString& ToolName, const FString& Reason)
		{
			FReloadResult::FRejection Rejection;
			Rejection.ToolName = ToolName;
			Rejection.Reason = Reason;
			Result.RejectedTools.Add(MoveTemp(Rejection));
			UE_LOG(LogUnrealMcp, Log, TEXT("User tool rejected: %s (%s)"), *ToolName, *Reason);
		}

		bool UserRegistryValidateSingleFileDirectory(const FString& ToolDirectory, FString& OutReason)
		{
			TArray<FString> Subdirectories;
			IFileManager::Get().FindFiles(Subdirectories, *FPaths::Combine(ToolDirectory, TEXT("*")), false, true);
			for (const FString& Subdirectory : Subdirectories)
			{
				if (!UserRegistryIsIgnoredGeneratedCacheDirectory(Subdirectory))
				{
					OutReason = TEXT("user_registry_invalid: subdirectories are not allowed in v0.26 user Python tools.");
					return false;
				}
			}

			TArray<FString> PythonFiles;
			IFileManager::Get().FindFiles(PythonFiles, *FPaths::Combine(ToolDirectory, TEXT("*.py")), true, false);
			for (const FString& PythonFile : PythonFiles)
			{
				if (!PythonFile.Equals(TEXT("main.py"), ESearchCase::CaseSensitive))
				{
					OutReason = FString::Printf(TEXT("user_registry_invalid: extra Python file '%s' is not allowed; v0.26 supports only main.py."), *PythonFile);
					return false;
				}
			}

			return true;
		}

		bool UserRegistryBuildEntryFromDirectory(
			const FString& DirectoryName,
			const FString& ToolDirectory,
			bool bAcceptChangedHashes,
			FReloadResult& Result,
			FUserToolEntry& OutEntry)
		{
			if (!UserRegistryIsSafeToolDirectoryName(DirectoryName))
			{
				UserRegistryAddRejection(Result, DirectoryName, TEXT("user_registry_invalid: unsafe user tool directory name."));
				return false;
			}

			FString SingleFileFailure;
			if (!UserRegistryValidateSingleFileDirectory(ToolDirectory, SingleFileFailure))
			{
				UserRegistryAddRejection(Result, DirectoryName, SingleFileFailure);
				return false;
			}

			const FString ToolJsonPath = FPaths::Combine(ToolDirectory, TEXT("tool.json"));
			TSharedPtr<FJsonObject> ToolJson;
			if (!UserRegistryLoadJsonObject(ToolJsonPath, ToolJson))
			{
				UserRegistryAddRejection(Result, DirectoryName, TEXT("user_registry_invalid: missing or invalid tool.json."));
				return false;
			}

			FString ToolName;
			if (!ToolJson->TryGetStringField(TEXT("name"), ToolName) || ToolName.TrimStartAndEnd().IsEmpty())
			{
				UserRegistryAddRejection(Result, DirectoryName, TEXT("user_registry_invalid: tool.json requires non-empty name."));
				return false;
			}
			ToolName = ToolName.TrimStartAndEnd();

			const FString ExpectedToolName = FString::Printf(TEXT("user.%s"), *DirectoryName);
			if (FindToolRegistryEntry(ToolName) != nullptr)
			{
				UserRegistryAddRejection(Result, ToolName, TEXT("user_registry_invalid: user tool cannot shadow a core tool name."));
				return false;
			}
			if (!ToolName.Equals(ExpectedToolName, ESearchCase::CaseSensitive))
			{
				UserRegistryAddRejection(Result, ToolName, FString::Printf(TEXT("user_registry_invalid: tool.json name must match directory as '%s'."), *ExpectedToolName));
				return false;
			}

			FString DeclaredSha256;
			if (!ToolJson->TryGetStringField(TEXT("pythonHandlerSha256"), DeclaredSha256) || !UserRegistryIsLowerHexSha256(DeclaredSha256.TrimStartAndEnd().ToLower()))
			{
				UserRegistryAddRejection(Result, ToolName, TEXT("user_registry_invalid: tool.json requires lowercase 64-character pythonHandlerSha256."));
				return false;
			}
			DeclaredSha256 = DeclaredSha256.TrimStartAndEnd().ToLower();

			const FString MainPyPath = FPaths::Combine(ToolDirectory, TEXT("main.py"));
			if (!FPaths::FileExists(MainPyPath))
			{
				UserRegistryAddRejection(Result, ToolName, TEXT("python_handler_missing: main.py is required."));
				return false;
			}

			TArray<uint8> MainPyBytes;
			if (!FFileHelper::LoadFileToArray(MainPyBytes, *MainPyPath))
			{
				UserRegistryAddRejection(Result, ToolName, TEXT("python_handler_missing: failed to read main.py."));
				return false;
			}

			const FString ActualSha256 = UnrealMcp::HashUtils::Sha256LowerHex(MainPyBytes).ToLower();
			if (!ActualSha256.Equals(DeclaredSha256, ESearchCase::CaseSensitive) && !bAcceptChangedHashes)
			{
				UserRegistryAddRejection(
					Result,
					ToolName,
					FString::Printf(TEXT("python_sha_mismatch: expected=%s actual=%s."), *DeclaredSha256, *ActualSha256));
				return false;
			}

			FString NormalizedToolDirectory = ToolDirectory;
			FPaths::NormalizeFilename(NormalizedToolDirectory);
			NormalizedToolDirectory.RemoveFromEnd(TEXT("/"));

			FString NormalizedMainPyPath = MainPyPath;
			FPaths::NormalizeFilename(NormalizedMainPyPath);

			OutEntry.ToolName = ToolName;
			OutEntry.ScaffoldDir = NormalizedToolDirectory;
			OutEntry.PythonHandlerPath = NormalizedMainPyPath;
			OutEntry.PythonHandlerSha256 = ActualSha256;
			OutEntry.ImportAllowlist = UserRegistryReadStringArrayField(ToolJson, TEXT("importAllowlist"));
			OutEntry.ToolJson = ToolJson;
			OutEntry.LifecycleState = ::UnrealMcp::Extension::ELifecycleState::LoadedUserPythonHot;
			return true;
		}

		FReloadResult UserRegistryScanAndMaybeApply(bool bAcceptChangedHashes, bool bApply)
		{
			UserRegistryEnsureInitialized();
			const double StartSeconds = FPlatformTime::Seconds();

			FReloadResult Result;
			Result.BeforeCount = GUserTools.Num();

			TMap<FString, FUserToolEntry> NewTools;
			if (IFileManager::Get().DirectoryExists(*GUserToolsRoot))
			{
				TArray<FString> DirectoryNames;
				IFileManager::Get().FindFiles(DirectoryNames, *FPaths::Combine(GUserToolsRoot, TEXT("*")), false, true);
				DirectoryNames.Sort();

				for (const FString& DirectoryName : DirectoryNames)
				{
					const FString ToolDirectory = FPaths::Combine(GUserToolsRoot, DirectoryName);
					const int32 RejectionCountBefore = Result.RejectedTools.Num();
					FUserToolEntry Entry;
					if (UserRegistryBuildEntryFromDirectory(DirectoryName, ToolDirectory, bAcceptChangedHashes, Result, Entry))
					{
						NewTools.Add(Entry.ToolName, MoveTemp(Entry));
					}
					else if (Result.RejectedTools.Num() > RejectionCountBefore
						&& Result.RejectedTools.Last().Reason.StartsWith(TEXT("python_sha_mismatch:")))
					{
						const FString ExistingToolName = FString::Printf(TEXT("user.%s"), *DirectoryName);
						if (const FUserToolEntry* Existing = GUserTools.Find(ExistingToolName))
						{
							NewTools.Add(ExistingToolName, *Existing);
						}
					}
				}
			}

			for (const TPair<FString, FUserToolEntry>& Pair : NewTools)
			{
				if (const FUserToolEntry* Existing = GUserTools.Find(Pair.Key))
				{
					if (!Existing->PythonHandlerSha256.Equals(Pair.Value.PythonHandlerSha256, ESearchCase::CaseSensitive)
						|| !Existing->PythonHandlerPath.Equals(Pair.Value.PythonHandlerPath, ESearchCase::CaseSensitive)
						|| Existing->ImportAllowlist.Num() != Pair.Value.ImportAllowlist.Num())
					{
						Result.UpdatedTools.Add(Pair.Key);
					}
				}
				else
				{
					Result.AddedTools.Add(Pair.Key);
				}
			}

			for (const TPair<FString, FUserToolEntry>& Pair : GUserTools)
			{
				if (!NewTools.Contains(Pair.Key))
				{
					Result.RemovedTools.Add(Pair.Key);
				}
			}

			Result.AfterCount = NewTools.Num();
			Result.DurationSeconds = FPlatformTime::Seconds() - StartSeconds;

			if (bApply)
			{
				for (const FString& ToolName : Result.UpdatedTools)
				{
					UnrealMcpPythonToolBridge::InvalidateUserToolCache(ToolName);
				}
				for (const FString& ToolName : Result.RemovedTools)
				{
					UnrealMcpPythonToolBridge::InvalidateUserToolCache(ToolName);
				}

				GUserTools = MoveTemp(NewTools);
			}

			for (const FString& ToolName : Result.AddedTools)
			{
				UE_LOG(LogUnrealMcp, Log, TEXT("User tool added: %s"), *ToolName);
			}
			for (const FString& ToolName : Result.UpdatedTools)
			{
				UE_LOG(LogUnrealMcp, Log, TEXT("User tool updated: %s"), *ToolName);
			}
			for (const FString& ToolName : Result.RemovedTools)
			{
				UE_LOG(LogUnrealMcp, Log, TEXT("User tool removed: %s"), *ToolName);
			}

			return Result;
		}
	}

	void InitializeUserToolRegistry()
	{
		UserRegistryEnsureInitialized();
	}

	FReloadResult ReloadUserToolRegistry(bool bAcceptChangedHashes)
	{
		return UserRegistryScanAndMaybeApply(bAcceptChangedHashes, true);
	}

	FReloadResult PreviewUserToolRegistryReload(bool bAcceptChangedHashes)
	{
		return UserRegistryScanAndMaybeApply(bAcceptChangedHashes, false);
	}

	const FUserToolEntry* FindUserTool(const FString& ToolName)
	{
		UserRegistryEnsureInitialized();
		return GUserTools.Find(ToolName);
	}

	TArray<const FUserToolEntry*> GetAllUserTools()
	{
		UserRegistryEnsureInitialized();
		TArray<const FUserToolEntry*> Tools;
		for (const TPair<FString, FUserToolEntry>& Pair : GUserTools)
		{
			Tools.Add(&Pair.Value);
		}
		return Tools;
	}

	int32 GetUserToolCount()
	{
		UserRegistryEnsureInitialized();
		return GUserTools.Num();
	}

	FString GetUserToolsRootDir()
	{
		UserRegistryEnsureInitialized();
		return GUserToolsRoot;
	}
}
