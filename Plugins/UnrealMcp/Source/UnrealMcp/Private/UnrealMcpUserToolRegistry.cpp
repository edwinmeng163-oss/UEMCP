// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.

#include "UnrealMcpUserToolRegistry.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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

		uint32 UserRegistrySha256RotateRight(uint32 Value, uint32 Shift)
		{
			return (Value >> Shift) | (Value << (32 - Shift));
		}

		FString UserRegistrySha256Bytes(const TArray<uint8>& Bytes)
		{
			static const uint32 Constants[64] =
			{
				0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
				0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
				0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
				0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
				0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
				0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
				0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
				0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
			};

			TArray<uint8> Padded = Bytes;
			const uint64 BitLength = static_cast<uint64>(Bytes.Num()) * 8ull;
			Padded.Add(0x80u);
			while ((Padded.Num() % 64) != 56)
			{
				Padded.Add(0u);
			}
			for (int32 Shift = 56; Shift >= 0; Shift -= 8)
			{
				Padded.Add(static_cast<uint8>((BitLength >> Shift) & 0xffu));
			}

			uint32 Hash[8] =
			{
				0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
				0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
			};

			for (int32 ChunkOffset = 0; ChunkOffset < Padded.Num(); ChunkOffset += 64)
			{
				uint32 Words[64] = {};
				for (int32 Index = 0; Index < 16; ++Index)
				{
					const int32 Offset = ChunkOffset + Index * 4;
					Words[Index] =
						(static_cast<uint32>(Padded[Offset]) << 24)
						| (static_cast<uint32>(Padded[Offset + 1]) << 16)
						| (static_cast<uint32>(Padded[Offset + 2]) << 8)
						| static_cast<uint32>(Padded[Offset + 3]);
				}
				for (int32 Index = 16; Index < 64; ++Index)
				{
					const uint32 S0 = UserRegistrySha256RotateRight(Words[Index - 15], 7) ^ UserRegistrySha256RotateRight(Words[Index - 15], 18) ^ (Words[Index - 15] >> 3);
					const uint32 S1 = UserRegistrySha256RotateRight(Words[Index - 2], 17) ^ UserRegistrySha256RotateRight(Words[Index - 2], 19) ^ (Words[Index - 2] >> 10);
					Words[Index] = Words[Index - 16] + S0 + Words[Index - 7] + S1;
				}

				uint32 A = Hash[0];
				uint32 B = Hash[1];
				uint32 C = Hash[2];
				uint32 D = Hash[3];
				uint32 E = Hash[4];
				uint32 F = Hash[5];
				uint32 G = Hash[6];
				uint32 H = Hash[7];

				for (int32 Index = 0; Index < 64; ++Index)
				{
					const uint32 S1 = UserRegistrySha256RotateRight(E, 6) ^ UserRegistrySha256RotateRight(E, 11) ^ UserRegistrySha256RotateRight(E, 25);
					const uint32 Choice = (E & F) ^ ((~E) & G);
					const uint32 Temp1 = H + S1 + Choice + Constants[Index] + Words[Index];
					const uint32 S0 = UserRegistrySha256RotateRight(A, 2) ^ UserRegistrySha256RotateRight(A, 13) ^ UserRegistrySha256RotateRight(A, 22);
					const uint32 Majority = (A & B) ^ (A & C) ^ (B & C);
					const uint32 Temp2 = S0 + Majority;

					H = G;
					G = F;
					F = E;
					E = D + Temp1;
					D = C;
					C = B;
					B = A;
					A = Temp1 + Temp2;
				}

				Hash[0] += A;
				Hash[1] += B;
				Hash[2] += C;
				Hash[3] += D;
				Hash[4] += E;
				Hash[5] += F;
				Hash[6] += G;
				Hash[7] += H;
			}

			static const TCHAR Hex[] = TEXT("0123456789abcdef");
			FString Result;
			Result.Reserve(64);
			for (uint32 Word : Hash)
			{
				for (int32 Shift = 24; Shift >= 0; Shift -= 8)
				{
					const uint8 Byte = static_cast<uint8>((Word >> Shift) & 0xffu);
					Result.AppendChar(Hex[(Byte >> 4) & 0x0f]);
					Result.AppendChar(Hex[Byte & 0x0f]);
				}
			}
			return Result;
		}

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

			const FString ActualSha256 = UserRegistrySha256Bytes(MainPyBytes).ToLower();
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
