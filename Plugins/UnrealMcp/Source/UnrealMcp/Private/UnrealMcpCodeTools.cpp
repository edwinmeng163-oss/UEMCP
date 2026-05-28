#include "UnrealMcpCodeTools.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpSelfExtensionInternal.h"

#if PLATFORM_MAC || PLATFORM_LINUX
#include <limits.h>
#include <unistd.h>
#endif

namespace UnrealMcp
{
	namespace
	{
		static constexpr int32 CodeToolsListDefaultLimit = 500;
		static constexpr int32 CodeToolsListHardLimit = 2000;
		static constexpr int32 CodeToolsReadDefaultMaxChars = 100000;
		static constexpr int32 CodeToolsReadHardMaxChars = 500000;
		static constexpr int32 CodeToolsSearchDefaultContextLines = 2;
		static constexpr int32 CodeToolsSearchMaxContextLines = 10;
		static constexpr int32 CodeToolsSearchDefaultMaxMatches = 200;
		static constexpr int32 CodeToolsSearchHardMaxMatches = 1000;
		static constexpr int32 CodeToolsSearchMaxFiles = 5000;
		static constexpr int64 CodeToolsSearchMaxBytesPerFile = 2ll * 1024ll * 1024ll;

		FString CodeToolsNormalizeUnicodeNfcLite(const FString& Input)
		{
			FString Output;
			Output.Reserve(Input.Len());
			for (int32 Index = 0; Index < Input.Len(); ++Index)
			{
				const TCHAR Character = Input[Index];
				if (Index + 1 < Input.Len() && Input[Index + 1] == static_cast<TCHAR>(0x0301))
				{
					TCHAR Combined = 0;
					switch (Character)
					{
					case TEXT('a'): Combined = static_cast<TCHAR>(0x00e1); break;
					case TEXT('A'): Combined = static_cast<TCHAR>(0x00c1); break;
					case TEXT('e'): Combined = static_cast<TCHAR>(0x00e9); break;
					case TEXT('E'): Combined = static_cast<TCHAR>(0x00c9); break;
					case TEXT('i'): Combined = static_cast<TCHAR>(0x00ed); break;
					case TEXT('I'): Combined = static_cast<TCHAR>(0x00cd); break;
					case TEXT('o'): Combined = static_cast<TCHAR>(0x00f3); break;
					case TEXT('O'): Combined = static_cast<TCHAR>(0x00d3); break;
					case TEXT('u'): Combined = static_cast<TCHAR>(0x00fa); break;
					case TEXT('U'): Combined = static_cast<TCHAR>(0x00da); break;
					default: break;
					}
					if (Combined != 0)
					{
						Output.AppendChar(Combined);
						++Index;
						continue;
					}
				}
				Output.AppendChar(Character);
			}
			return Output;
		}

		FString CodeToolsNormalizePath(FString Path)
		{
			Path.TrimStartAndEndInline();
			Path = CodeToolsNormalizeUnicodeNfcLite(Path);
			Path.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
			if (FPaths::IsRelative(Path))
			{
				Path = FPaths::ConvertRelativePathToFull(Path);
			}
			FPaths::NormalizeFilename(Path);
			FPaths::CollapseRelativeDirectories(Path);
			while (Path.Len() > 1 && Path.EndsWith(TEXT("/")))
			{
				Path.LeftChopInline(1);
			}
			return Path;
		}

		FString CodeToolsCombinePath(const FString& A, const FString& B)
		{
			return CodeToolsNormalizePath(FPaths::Combine(A, B));
		}

		bool CodeToolsPathEqualsOrChild(const FString& Path, const FString& Root)
		{
			const FString CleanPath = CodeToolsNormalizePath(Path);
			const FString CleanRoot = CodeToolsNormalizePath(Root);
			return CleanPath.Equals(CleanRoot, ESearchCase::IgnoreCase)
				|| CleanPath.StartsWith(CleanRoot + TEXT("/"), ESearchCase::IgnoreCase);
		}

		FString CodeToolsProjectRelativePath(const FString& ProjectDir, const FString& Path)
		{
			FString Relative = CodeToolsNormalizePath(Path);
			const FString CleanProjectDir = CodeToolsNormalizePath(ProjectDir);
			if (Relative.Equals(CleanProjectDir, ESearchCase::IgnoreCase))
			{
				return FString();
			}
			if (Relative.StartsWith(CleanProjectDir + TEXT("/"), ESearchCase::IgnoreCase))
			{
				Relative.RightChopInline(CleanProjectDir.Len() + 1);
			}
			return Relative;
		}

		TArray<TSharedPtr<FJsonValue>> CodeToolsMakeStringArray(const TArray<FString>& Values)
		{
			TArray<TSharedPtr<FJsonValue>> JsonValues;
			for (const FString& Value : Values)
			{
				JsonValues.Add(MakeShared<FJsonValueString>(Value));
			}
			return JsonValues;
		}

		TArray<FString> CodeToolsWriteExtensions()
		{
			TArray<FString> Extensions;
			Extensions.Add(TEXT(".h"));
			Extensions.Add(TEXT(".hpp"));
			Extensions.Add(TEXT(".cpp"));
			Extensions.Add(TEXT(".inl"));
			Extensions.Add(TEXT(".ipp"));
			Extensions.Add(TEXT(".Build.cs"));
			Extensions.Add(TEXT(".Target.cs"));
			Extensions.Add(TEXT(".uplugin"));
			Extensions.Add(TEXT(".uproject"));
			Extensions.Add(TEXT(".py"));
			Extensions.Add(TEXT(".json"));
			Extensions.Add(TEXT(".ini"));
			return Extensions;
		}

		TArray<FString> CodeToolsReadExtensions()
		{
			TArray<FString> Extensions = CodeToolsWriteExtensions();
			Extensions.Add(TEXT(".md"));
			Extensions.Add(TEXT(".cs"));
			Extensions.Add(TEXT(".sh"));
			Extensions.Add(TEXT(".command"));
			Extensions.Add(TEXT(".ps1"));
			Extensions.Add(TEXT(".bat"));
			Extensions.Add(TEXT(".usf"));
			Extensions.Add(TEXT(".ush"));
			Extensions.Add(TEXT(".hlsl"));
			Extensions.Add(TEXT(".yml"));
			Extensions.Add(TEXT(".yaml"));
			Extensions.Add(TEXT(".toml"));
			Extensions.Add(TEXT(".txt"));
			return Extensions;
		}

		FString CodeToolsPathExtension(const FString& Path)
		{
			if (Path.EndsWith(TEXT(".generated.h"), ESearchCase::IgnoreCase))
			{
				return TEXT(".generated.h");
			}
			if (Path.EndsWith(TEXT(".gen.cpp"), ESearchCase::IgnoreCase))
			{
				return TEXT(".gen.cpp");
			}
			if (Path.EndsWith(TEXT(".Build.cs"), ESearchCase::IgnoreCase))
			{
				return TEXT(".Build.cs");
			}
			if (Path.EndsWith(TEXT(".Target.cs"), ESearchCase::IgnoreCase))
			{
				return TEXT(".Target.cs");
			}
			return FPaths::GetExtension(Path, true).ToLower();
		}

		bool CodeToolsExtensionAllowed(const FString& Path, const TArray<FString>& AllowedExtensions)
		{
			const FString PathExtension = CodeToolsPathExtension(Path);
			for (const FString& Extension : AllowedExtensions)
			{
				if (PathExtension.Equals(Extension, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
			return false;
		}

		bool CodeToolsHasForbiddenGeneratedExtension(const FString& Path)
		{
			const FString PathExtension = CodeToolsPathExtension(Path);
			return PathExtension.Equals(TEXT(".generated.h"), ESearchCase::IgnoreCase)
				|| PathExtension.Equals(TEXT(".gen.cpp"), ESearchCase::IgnoreCase)
				|| PathExtension.Equals(TEXT(".uasset"), ESearchCase::IgnoreCase)
				|| PathExtension.Equals(TEXT(".umap"), ESearchCase::IgnoreCase);
		}

		bool CodeToolsHasAmbiguousPathSegment(const FString& Path, FString& OutReason)
		{
			TArray<FString> Parts;
			Path.ParseIntoArray(Parts, TEXT("/"), false);
			for (const FString& Part : Parts)
			{
				if (Part == TEXT(".") || Part == TEXT(".."))
				{
					OutReason = TEXT("Path contains ambiguous relative segments after normalization.");
					return true;
				}
				if (Part.EndsWith(TEXT("."), ESearchCase::CaseSensitive) || Part.EndsWith(TEXT(" "), ESearchCase::CaseSensitive))
				{
					OutReason = TEXT("Path contains a trailing-dot or trailing-space segment.");
					return true;
				}
			}
			return false;
		}

		bool CodeToolsIsExcludedProjectRelativePath(const FString& ProjectRelativePath)
		{
			TArray<FString> Parts;
			ProjectRelativePath.ParseIntoArray(Parts, TEXT("/"), false);
			if (Parts.Num() == 0)
			{
				return false;
			}
			const FString First = Parts[0];
			if (First.Equals(TEXT("Binaries"), ESearchCase::IgnoreCase)
				|| First.Equals(TEXT("Intermediate"), ESearchCase::IgnoreCase)
				|| First.Equals(TEXT("DerivedDataCache"), ESearchCase::IgnoreCase)
				|| First.Equals(TEXT("Saved"), ESearchCase::IgnoreCase)
				|| First.Equals(TEXT("Content"), ESearchCase::IgnoreCase))
			{
				return true;
			}
			if (Parts.Num() >= 3 && First.Equals(TEXT("Plugins"), ESearchCase::IgnoreCase))
			{
				return Parts[2].Equals(TEXT("Binaries"), ESearchCase::IgnoreCase)
					|| Parts[2].Equals(TEXT("Intermediate"), ESearchCase::IgnoreCase);
			}
			return false;
		}

		bool CodeToolsIsSavedCodeChangesTarget(const FString& ProjectRelativePath)
		{
			return ProjectRelativePath.Equals(TEXT("Saved/UnrealMcp/CodeChanges"), ESearchCase::IgnoreCase)
				|| ProjectRelativePath.StartsWith(TEXT("Saved/UnrealMcp/CodeChanges/"), ESearchCase::IgnoreCase);
		}

		bool CodeToolsIsEnginePath(const FString& Path)
		{
			const FString CleanPath = CodeToolsNormalizePath(Path);
			return CleanPath.Contains(TEXT("/Engine/"), ESearchCase::IgnoreCase)
				|| CleanPath.EndsWith(TEXT("/Engine"), ESearchCase::IgnoreCase);
		}

		FString CodeToolsResolvePluginBaseDir()
		{
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealMcp"));
			if (Plugin.IsValid() && !Plugin->GetBaseDir().IsEmpty())
			{
				return CodeToolsNormalizePath(Plugin->GetBaseDir());
			}
			return CodeToolsCombinePath(FPaths::ProjectDir(), TEXT("Plugins/UnrealMcp"));
		}

		FCodePathPolicy CodeToolsMakePolicy(ECodePathClassification Classification, const FString& CanonicalPath, const FString& Reason)
		{
			FCodePathPolicy Policy;
			Policy.Classification = Classification;
			Policy.CanonicalPath = CanonicalPath;
			Policy.Reason = Reason;
			return Policy;
		}

		FString CodeToolsClassifySourceKind(const FString& ProjectDir, const FString& PluginBaseDir, const FString& Path)
		{
			const FString ProjectRoot = CodeToolsNormalizePath(ProjectDir);
			const FString PluginRoot = CodeToolsNormalizePath(PluginBaseDir);
			if (CodeToolsPathEqualsOrChild(Path, PluginRoot)
				|| CodeToolsPathEqualsOrChild(Path, CodeToolsCombinePath(ProjectRoot, TEXT("Plugins/UnrealMcp"))))
			{
				return TEXT("forbidden_write");
			}
			if (CodeToolsPathEqualsOrChild(Path, CodeToolsCombinePath(ProjectRoot, TEXT("Tools/UnrealMcpPyTools")))
				|| CodeToolsPathEqualsOrChild(Path, CodeToolsCombinePath(ProjectRoot, TEXT("Tools/UnrealMcpPyToolSamples"))))
			{
				return TEXT("default_writable");
			}
			if (CodeToolsPathEqualsOrChild(Path, CodeToolsCombinePath(ProjectRoot, TEXT("Source")))
				|| CodeToolsPathEqualsOrChild(Path, CodeToolsCombinePath(ProjectRoot, TEXT("Config")))
				|| CodeToolsPathEqualsOrChild(Path, CodeToolsCombinePath(ProjectRoot, TEXT("Plugins"))))
			{
				return TEXT("high_risk");
			}
			return TEXT("project");
		}

		TArray<FString> CodeToolsGetScopeRoots(const FString& Scope)
		{
			const FString ProjectRoot = CodeToolsNormalizePath(FPaths::ProjectDir());
			TArray<FString> Roots;
			if (Scope.Equals(TEXT("source"), ESearchCase::IgnoreCase))
			{
				Roots.Add(CodeToolsCombinePath(ProjectRoot, TEXT("Source")));
			}
			else if (Scope.Equals(TEXT("plugins"), ESearchCase::IgnoreCase))
			{
				Roots.Add(CodeToolsCombinePath(ProjectRoot, TEXT("Plugins")));
			}
			else if (Scope.Equals(TEXT("user_tools"), ESearchCase::IgnoreCase))
			{
				Roots.Add(CodeToolsCombinePath(ProjectRoot, TEXT("Tools/UnrealMcpPyToolSamples")));
				Roots.Add(CodeToolsCombinePath(ProjectRoot, TEXT("Tools/UnrealMcpPyTools")));
			}
			else if (Scope.Equals(TEXT("python_tools"), ESearchCase::IgnoreCase))
			{
				Roots.Add(CodeToolsCombinePath(ProjectRoot, TEXT("Tools/UnrealMcpPyTools")));
			}
			else
			{
				Roots.Add(ProjectRoot);
			}
			return Roots;
		}

		int32 CodeToolsGetClampedIntArgument(const FJsonObject& Arguments, const FString& FieldName, int32 DefaultValue, int32 MinValue, int32 MaxValue)
		{
			double Number = static_cast<double>(DefaultValue);
			Arguments.TryGetNumberField(FieldName, Number);
			return FMath::Clamp(static_cast<int32>(Number), MinValue, MaxValue);
		}

		void CodeToolsGetStringArrayArgument(const FJsonObject& Arguments, const FString& FieldName, TArray<FString>& OutValues)
		{
			OutValues.Reset();
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Arguments.TryGetArrayField(FieldName, Values) || !Values)
			{
				return;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				if (Value.IsValid() && Value->Type == EJson::String)
				{
					FString StringValue = Value->AsString().TrimStartAndEnd();
					if (!StringValue.IsEmpty())
					{
						if (!StringValue.StartsWith(TEXT(".")))
						{
							StringValue = TEXT(".") + StringValue;
						}
						OutValues.Add(StringValue);
					}
				}
			}
		}

		uint32 CodeToolsSha256RotateRight(uint32 Value, uint32 Shift)
		{
			return (Value >> Shift) | (Value << (32 - Shift));
		}

		FString CodeToolsSha256Bytes(const TArray<uint8>& Bytes)
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
					const uint32 S0 = CodeToolsSha256RotateRight(Words[Index - 15], 7) ^ CodeToolsSha256RotateRight(Words[Index - 15], 18) ^ (Words[Index - 15] >> 3);
					const uint32 S1 = CodeToolsSha256RotateRight(Words[Index - 2], 17) ^ CodeToolsSha256RotateRight(Words[Index - 2], 19) ^ (Words[Index - 2] >> 10);
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
					const uint32 S1 = CodeToolsSha256RotateRight(E, 6) ^ CodeToolsSha256RotateRight(E, 11) ^ CodeToolsSha256RotateRight(E, 25);
					const uint32 Choice = (E & F) ^ ((~E) & G);
					const uint32 Temp1 = H + S1 + Choice + Constants[Index] + Words[Index];
					const uint32 S0 = CodeToolsSha256RotateRight(A, 2) ^ CodeToolsSha256RotateRight(A, 13) ^ CodeToolsSha256RotateRight(A, 22);
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

		bool CodeToolsReadFileBytesAndText(const FString& Path, TArray<uint8>& OutBytes, FString& OutText, FString& OutSha256)
		{
			OutBytes.Reset();
			OutText.Reset();
			OutSha256.Reset();
			if (!FFileHelper::LoadFileToArray(OutBytes, *Path))
			{
				return false;
			}
			if (!FFileHelper::LoadFileToString(OutText, *Path))
			{
				return false;
			}
			OutSha256 = CodeToolsSha256Bytes(OutBytes);
			return true;
		}

		bool CodeToolsResolveReadablePath(const FString& RawPath, FString& OutPath, FString& OutProjectRelativePath, FString& OutFailureReason)
		{
			const FString ProjectRoot = CodeToolsNormalizePath(FPaths::ProjectDir());
			FString TrimmedPath = RawPath.TrimStartAndEnd();
			if (TrimmedPath.IsEmpty())
			{
				OutFailureReason = TEXT("path is required.");
				return false;
			}

			FString CandidatePath = FPaths::IsRelative(TrimmedPath)
				? CodeToolsCombinePath(ProjectRoot, TrimmedPath)
				: CodeToolsNormalizePath(TrimmedPath);
			FString AmbiguousReason;
			if (CodeToolsHasAmbiguousPathSegment(CandidatePath, AmbiguousReason))
			{
				OutFailureReason = AmbiguousReason;
				return false;
			}
			if (!CodeToolsPathEqualsOrChild(CandidatePath, ProjectRoot))
			{
				OutFailureReason = TEXT("path must resolve inside the project.");
				return false;
			}
			OutProjectRelativePath = CodeToolsProjectRelativePath(ProjectRoot, CandidatePath);
			if (CodeToolsIsExcludedProjectRelativePath(OutProjectRelativePath))
			{
				OutFailureReason = TEXT("path is under a read-excluded project directory.");
				return false;
			}
			if (!CodeToolsExtensionAllowed(CandidatePath, CodeToolsReadExtensions()))
			{
				OutFailureReason = FString::Printf(TEXT("file extension '%s' is not in the Code tools read allowlist."), *CodeToolsPathExtension(CandidatePath));
				return false;
			}
			if (!FPaths::FileExists(CandidatePath))
			{
				OutFailureReason = FString::Printf(TEXT("file does not exist: %s"), *OutProjectRelativePath);
				return false;
			}

			OutPath = CandidatePath;
			return true;
		}

		bool CodeToolsRealPathExists(const FString& Path)
		{
			return FPaths::FileExists(Path) || FPaths::DirectoryExists(Path);
		}

		bool CodeToolsTryResolveRealSymlinkTarget(const FString& Path, FString& OutTarget)
		{
			OutTarget.Reset();
#if PLATFORM_MAC || PLATFORM_LINUX
			char Buffer[PATH_MAX + 1] = {};
			FTCHARToUTF8 Converter(*Path);
			const ssize_t Length = readlink(Converter.Get(), Buffer, PATH_MAX);
			if (Length <= 0)
			{
				return false;
			}
			Buffer[Length] = '\0';
			FUTF8ToTCHAR TargetConverter(Buffer);
			FString Target(TargetConverter.Length(), TargetConverter.Get());
			if (FPaths::IsRelative(Target))
			{
				Target = FPaths::Combine(FPaths::GetPath(Path), Target);
			}
			OutTarget = CodeToolsNormalizePath(Target);
			return true;
#else
			return false;
#endif
		}

		TSharedPtr<FJsonObject> CodeToolsMakeFileObject(const FString& ProjectDir, const FString& PluginBaseDir, const FString& Path)
		{
			TSharedPtr<FJsonObject> FileObject = MakeShared<FJsonObject>();
			FileObject->SetStringField(TEXT("projectRelativePath"), CodeToolsProjectRelativePath(ProjectDir, Path));
			FileObject->SetStringField(TEXT("ext"), CodeToolsPathExtension(Path));
			FileObject->SetNumberField(TEXT("sizeBytes"), static_cast<double>(IFileManager::Get().FileSize(*Path)));
			FileObject->SetStringField(TEXT("sourceKind"), CodeToolsClassifySourceKind(ProjectDir, PluginBaseDir, Path));
			return FileObject;
		}

		TArray<FString> CodeToolsFindReadableFiles(
			const FString& Scope,
			const TArray<FString>& ExtensionFilter,
			const FString& Glob,
			int32 MaxFilesToScan,
			int32& OutScannedCount,
			int32& OutMatchedCount,
			bool& bOutScanTruncated)
		{
			OutScannedCount = 0;
			OutMatchedCount = 0;
			bOutScanTruncated = false;
			const FString ProjectRoot = CodeToolsNormalizePath(FPaths::ProjectDir());
			TArray<FString> Matches;
			TSet<FString> SeenRelativePaths;
			const TArray<FString> AllowedExtensions = ExtensionFilter.Num() > 0 ? ExtensionFilter : CodeToolsReadExtensions();

			for (const FString& Root : CodeToolsGetScopeRoots(Scope))
			{
				if (!FPaths::DirectoryExists(Root))
				{
					continue;
				}

				TArray<FString> Files;
				IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*"), true, false);
				Files.Sort();
				for (const FString& FilePath : Files)
				{
					if (OutScannedCount >= MaxFilesToScan)
					{
						bOutScanTruncated = true;
						return Matches;
					}
					++OutScannedCount;

					const FString NormalizedPath = CodeToolsNormalizePath(FilePath);
					const FString ProjectRelativePath = CodeToolsProjectRelativePath(ProjectRoot, NormalizedPath);
					if (ProjectRelativePath.IsEmpty()
						|| SeenRelativePaths.Contains(ProjectRelativePath.ToLower())
						|| CodeToolsIsExcludedProjectRelativePath(ProjectRelativePath)
						|| !CodeToolsExtensionAllowed(NormalizedPath, AllowedExtensions))
					{
						continue;
					}
					if (!Glob.TrimStartAndEnd().IsEmpty() && !ProjectRelativePath.MatchesWildcard(Glob, ESearchCase::IgnoreCase))
					{
						continue;
					}

					SeenRelativePaths.Add(ProjectRelativePath.ToLower());
					++OutMatchedCount;
					Matches.Add(NormalizedPath);
				}
			}
			return Matches;
		}

		TSharedPtr<FJsonObject> CodeToolsMakeLineRangeObject(int32 StartLine, int32 EndLine)
		{
			TSharedPtr<FJsonObject> Range = MakeShared<FJsonObject>();
			Range->SetNumberField(TEXT("start"), StartLine);
			Range->SetNumberField(TEXT("end"), EndLine);
			return Range;
		}

		TArray<TSharedPtr<FJsonValue>> CodeToolsMakeContextArray(const TArray<FString>& Lines, int32 StartIndex, int32 EndIndex)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (int32 Index = FMath::Max(0, StartIndex); Index <= EndIndex && Index < Lines.Num(); ++Index)
			{
				Values.Add(MakeShared<FJsonValueString>(Lines[Index]));
			}
			return Values;
		}

		TSharedPtr<FJsonObject> CodeToolsMakeSearchMatch(
			const FString& ProjectRelativePath,
			int32 LineNumber,
			int32 ColumnStart,
			const FString& LineText,
			const TArray<FString>& Lines,
			int32 LineIndex,
			int32 ContextLines)
		{
			TSharedPtr<FJsonObject> MatchObject = MakeShared<FJsonObject>();
			MatchObject->SetStringField(TEXT("projectRelativePath"), ProjectRelativePath);
			MatchObject->SetNumberField(TEXT("lineNumber"), LineNumber);
			if (ColumnStart > 0)
			{
				MatchObject->SetNumberField(TEXT("columnStart"), ColumnStart);
			}
			MatchObject->SetStringField(TEXT("lineText"), LineText);
			MatchObject->SetArrayField(TEXT("contextBefore"), CodeToolsMakeContextArray(Lines, LineIndex - ContextLines, LineIndex - 1));
			MatchObject->SetArrayField(TEXT("contextAfter"), CodeToolsMakeContextArray(Lines, LineIndex + 1, LineIndex + ContextLines));
			return MatchObject;
		}

		FUnrealMcpExecutionResult CodeToolsWorkspaceStatus(const FJsonObject& Arguments)
		{
			(void)Arguments;
			const FString ProjectRoot = CodeToolsNormalizePath(FPaths::ProjectDir());
			const FString PluginRoot = CodeToolsResolvePluginBaseDir();
			const FString LatestManifest = CodeToolsCombinePath(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/CodeChanges/LastCodeChange.json"));
			const FString LockPath = GetMcpExtensionLockPath();

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("projectDir"), ProjectRoot);
			StructuredContent->SetArrayField(TEXT("allowedReadRoots"), CodeToolsMakeStringArray({ ProjectRoot }));
			StructuredContent->SetArrayField(TEXT("defaultWritableRoots"), CodeToolsMakeStringArray({
				CodeToolsCombinePath(ProjectRoot, TEXT("Tools/UnrealMcpPyToolSamples")),
				CodeToolsCombinePath(ProjectRoot, TEXT("Tools/UnrealMcpPyTools"))
			}));
			StructuredContent->SetArrayField(TEXT("highRiskWritableRoots"), CodeToolsMakeStringArray({
				CodeToolsCombinePath(ProjectRoot, TEXT("Source")),
				CodeToolsCombinePath(ProjectRoot, TEXT("Plugins/*/Source")),
				CodeToolsCombinePath(ProjectRoot, TEXT("*.uplugin")),
				CodeToolsCombinePath(ProjectRoot, TEXT("*.uproject")),
				CodeToolsCombinePath(ProjectRoot, TEXT("Config/*.ini"))
			}));
			StructuredContent->SetArrayField(TEXT("forbiddenRoots"), CodeToolsMakeStringArray({
				PluginRoot,
				CodeToolsCombinePath(ProjectRoot, TEXT("Plugins/UnrealMcp")),
				CodeToolsCombinePath(ProjectRoot, TEXT("Binaries")),
				CodeToolsCombinePath(ProjectRoot, TEXT("Intermediate")),
				CodeToolsCombinePath(ProjectRoot, TEXT("DerivedDataCache")),
				CodeToolsCombinePath(ProjectRoot, TEXT("Saved")),
				CodeToolsCombinePath(ProjectRoot, TEXT("Content"))
			}));
			StructuredContent->SetArrayField(TEXT("allowedExtensions"), CodeToolsMakeStringArray(CodeToolsReadExtensions()));
			StructuredContent->SetArrayField(TEXT("writeAllowedExtensions"), CodeToolsMakeStringArray(CodeToolsWriteExtensions()));
			if (FPaths::FileExists(LatestManifest))
			{
				StructuredContent->SetStringField(TEXT("latestCodeChangeManifest"), LatestManifest);
			}
			else
			{
				StructuredContent->SetField(TEXT("latestCodeChangeManifest"), MakeShared<FJsonValueNull>());
			}

			TSharedPtr<FJsonObject> LockObject;
			FString FailureReason;
			if (FPaths::FileExists(LockPath) && LoadJsonObjectFromFile(LockPath, LockObject, FailureReason) && LockObject.IsValid())
			{
				LockObject->SetBoolField(TEXT("held"), true);
				StructuredContent->SetObjectField(TEXT("extensionLockStatus"), LockObject);
			}
			else
			{
				StructuredContent->SetField(TEXT("extensionLockStatus"), MakeShared<FJsonValueNull>());
			}
			StructuredContent->SetStringField(TEXT("extensionLockPath"), LockPath);

			return MakeExecutionResult(TEXT("Returned Code tools workspace status."), StructuredContent, false);
		}

		FUnrealMcpExecutionResult CodeToolsListFiles(const FJsonObject& Arguments)
		{
			FString Scope = TEXT("project");
			FString Glob;
			Arguments.TryGetStringField(TEXT("scope"), Scope);
			Arguments.TryGetStringField(TEXT("glob"), Glob);
			TArray<FString> ExtensionFilter;
			CodeToolsGetStringArrayArgument(Arguments, TEXT("extensions"), ExtensionFilter);
			const int32 MaxResults = CodeToolsGetClampedIntArgument(Arguments, TEXT("maxResults"), CodeToolsListDefaultLimit, 1, CodeToolsListHardLimit);

			int32 ScannedCount = 0;
			int32 MatchedCount = 0;
			bool bScanTruncated = false;
			const TArray<FString> Files = CodeToolsFindReadableFiles(Scope, ExtensionFilter, Glob, MAX_int32, ScannedCount, MatchedCount, bScanTruncated);
			const FString ProjectRoot = CodeToolsNormalizePath(FPaths::ProjectDir());
			const FString PluginRoot = CodeToolsResolvePluginBaseDir();

			TArray<TSharedPtr<FJsonValue>> FileValues;
			const int32 ReturnCount = FMath::Min(Files.Num(), MaxResults);
			for (int32 Index = 0; Index < ReturnCount; ++Index)
			{
				FileValues.Add(MakeShared<FJsonValueObject>(CodeToolsMakeFileObject(ProjectRoot, PluginRoot, Files[Index])));
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetArrayField(TEXT("files"), FileValues);
			StructuredContent->SetBoolField(TEXT("truncated"), bScanTruncated || Files.Num() > MaxResults);
			StructuredContent->SetNumberField(TEXT("scannedCount"), ScannedCount);
			StructuredContent->SetNumberField(TEXT("matchedCount"), MatchedCount);
			return MakeExecutionResult(
				FString::Printf(TEXT("Listed %d Code-readable files."), ReturnCount),
				StructuredContent,
				false);
		}

		FUnrealMcpExecutionResult CodeToolsReadFile(const FJsonObject& Arguments)
		{
			FString RawPath;
			Arguments.TryGetStringField(TEXT("path"), RawPath);

			FString ResolvedPath;
			FString ProjectRelativePath;
			FString FailureReason;
			if (!CodeToolsResolveReadablePath(RawPath, ResolvedPath, ProjectRelativePath, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			TArray<uint8> Bytes;
			FString Text;
			FString Sha256;
			if (!CodeToolsReadFileBytesAndText(ResolvedPath, Bytes, Text, Sha256))
			{
				return MakeExecutionResult(FString::Printf(TEXT("Failed to read file '%s'."), *ProjectRelativePath), nullptr, true);
			}

			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines, false);
			const int32 TotalLineCount = Lines.Num();
			const int32 StartLine = CodeToolsGetClampedIntArgument(Arguments, TEXT("startLine"), 1, 1, FMath::Max(1, TotalLineCount));
			int32 RequestedLineCount = TotalLineCount - StartLine + 1;
			double LineCountNumber = 0.0;
			if (Arguments.TryGetNumberField(TEXT("lineCount"), LineCountNumber))
			{
				RequestedLineCount = FMath::Max(1, static_cast<int32>(LineCountNumber));
			}
			const int32 MaxChars = CodeToolsGetClampedIntArgument(Arguments, TEXT("maxChars"), CodeToolsReadDefaultMaxChars, 1, CodeToolsReadHardMaxChars);
			const int32 StartIndex = StartLine - 1;
			const int32 EndIndex = FMath::Min(TotalLineCount - 1, StartIndex + RequestedLineCount - 1);

			FString ReturnedText;
			int32 ReturnedEndLine = TotalLineCount > 0 ? StartLine : 0;
			bool bTruncated = false;
			for (int32 Index = StartIndex; Index <= EndIndex && Index < Lines.Num(); ++Index)
			{
				FString NextLine = Lines[Index];
				if (!ReturnedText.IsEmpty())
				{
					NextLine = TEXT("\n") + NextLine;
				}
				if (ReturnedText.Len() + NextLine.Len() > MaxChars)
				{
					const int32 RemainingChars = FMath::Max(0, MaxChars - ReturnedText.Len());
					if (RemainingChars > 0)
					{
						ReturnedText += NextLine.Left(RemainingChars);
					}
					bTruncated = true;
					ReturnedEndLine = Index + 1;
					break;
				}
				ReturnedText += NextLine;
				ReturnedEndLine = Index + 1;
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("resolvedPath"), ResolvedPath);
			StructuredContent->SetStringField(TEXT("projectRelativePath"), ProjectRelativePath);
			StructuredContent->SetNumberField(TEXT("lineCount"), TotalLineCount);
			StructuredContent->SetObjectField(TEXT("returnedLineRange"), CodeToolsMakeLineRangeObject(TotalLineCount > 0 ? StartLine : 0, ReturnedEndLine));
			StructuredContent->SetStringField(TEXT("sha256"), Sha256);
			StructuredContent->SetBoolField(TEXT("truncated"), bTruncated || EndIndex < TotalLineCount - 1);
			StructuredContent->SetStringField(TEXT("text"), ReturnedText);
			return MakeExecutionResult(
				FString::Printf(TEXT("Read Code file '%s'."), *ProjectRelativePath),
				StructuredContent,
				false);
		}

		FUnrealMcpExecutionResult CodeToolsSearch(const FJsonObject& Arguments)
		{
			FString Query;
			FString Mode = TEXT("literal");
			FString Scope = TEXT("project");
			Arguments.TryGetStringField(TEXT("query"), Query);
			Arguments.TryGetStringField(TEXT("mode"), Mode);
			Arguments.TryGetStringField(TEXT("scope"), Scope);
			Query = Query.TrimStartAndEnd();
			if (Query.IsEmpty())
			{
				return MakeExecutionResult(TEXT("query is required."), nullptr, true);
			}

			TArray<FString> ExtensionFilter;
			CodeToolsGetStringArrayArgument(Arguments, TEXT("extensions"), ExtensionFilter);
			const int32 ContextLines = CodeToolsGetClampedIntArgument(Arguments, TEXT("contextLines"), CodeToolsSearchDefaultContextLines, 0, CodeToolsSearchMaxContextLines);
			const int32 MaxMatches = CodeToolsGetClampedIntArgument(Arguments, TEXT("maxMatches"), CodeToolsSearchDefaultMaxMatches, 1, CodeToolsSearchHardMaxMatches);

			int32 FilesScannedByFinder = 0;
			int32 MatchedFilesByFinder = 0;
			bool bScanTruncated = false;
			const TArray<FString> Files = CodeToolsFindReadableFiles(Scope, ExtensionFilter, FString(), CodeToolsSearchMaxFiles, FilesScannedByFinder, MatchedFilesByFinder, bScanTruncated);
			const FString ProjectRoot = CodeToolsNormalizePath(FPaths::ProjectDir());

			TArray<TSharedPtr<FJsonValue>> MatchValues;
			bool bTruncated = bScanTruncated;
			int32 FilesScanned = 0;

			for (const FString& FilePath : Files)
			{
				if (MatchValues.Num() >= MaxMatches)
				{
					bTruncated = true;
					break;
				}
				++FilesScanned;

				const FString ProjectRelativePath = CodeToolsProjectRelativePath(ProjectRoot, FilePath);
				if (Mode.Equals(TEXT("filename"), ESearchCase::IgnoreCase))
				{
					if (ProjectRelativePath.Contains(Query, ESearchCase::IgnoreCase))
					{
						TSharedPtr<FJsonObject> MatchObject = MakeShared<FJsonObject>();
						MatchObject->SetStringField(TEXT("projectRelativePath"), ProjectRelativePath);
						MatchObject->SetNumberField(TEXT("lineNumber"), 0);
						MatchObject->SetNumberField(TEXT("columnStart"), ProjectRelativePath.Find(Query, ESearchCase::IgnoreCase) + 1);
						MatchObject->SetStringField(TEXT("lineText"), ProjectRelativePath);
						MatchObject->SetArrayField(TEXT("contextBefore"), TArray<TSharedPtr<FJsonValue>>());
						MatchObject->SetArrayField(TEXT("contextAfter"), TArray<TSharedPtr<FJsonValue>>());
						MatchValues.Add(MakeShared<FJsonValueObject>(MatchObject));
					}
					continue;
				}

				const int64 FileSize = IFileManager::Get().FileSize(*FilePath);
				if (FileSize > CodeToolsSearchMaxBytesPerFile)
				{
					continue;
				}

				FString Text;
				if (!FFileHelper::LoadFileToString(Text, *FilePath))
				{
					continue;
				}
				TArray<FString> Lines;
				Text.ParseIntoArrayLines(Lines, false);

				if (Mode.Equals(TEXT("regex"), ESearchCase::IgnoreCase))
				{
					const FRegexPattern Pattern(Query);
					for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
					{
						FRegexMatcher Matcher(Pattern, Lines[LineIndex]);
						if (Matcher.FindNext())
						{
							MatchValues.Add(MakeShared<FJsonValueObject>(CodeToolsMakeSearchMatch(
								ProjectRelativePath,
								LineIndex + 1,
								Matcher.GetMatchBeginning() + 1,
								Lines[LineIndex],
								Lines,
								LineIndex,
								ContextLines)));
							if (MatchValues.Num() >= MaxMatches)
							{
								bTruncated = true;
								break;
							}
						}
					}
				}
				else
				{
					for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
					{
						const int32 FoundIndex = Lines[LineIndex].Find(Query, ESearchCase::CaseSensitive);
						if (FoundIndex != INDEX_NONE)
						{
							MatchValues.Add(MakeShared<FJsonValueObject>(CodeToolsMakeSearchMatch(
								ProjectRelativePath,
								LineIndex + 1,
								FoundIndex + 1,
								Lines[LineIndex],
								Lines,
								LineIndex,
								ContextLines)));
							if (MatchValues.Num() >= MaxMatches)
							{
								bTruncated = true;
								break;
							}
						}
					}
				}
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetArrayField(TEXT("matches"), MatchValues);
			StructuredContent->SetBoolField(TEXT("truncated"), bTruncated);
			StructuredContent->SetNumberField(TEXT("filesScanned"), FilesScanned);
			StructuredContent->SetNumberField(TEXT("matchesReturned"), MatchValues.Num());
			return MakeExecutionResult(
				FString::Printf(TEXT("Returned %d Code search matches."), MatchValues.Num()),
				StructuredContent,
				false);
		}

		FUnrealMcpExecutionResult CodeToolsWaveBStub(const FString& ToolName)
		{
			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("code"), TEXT("not_implemented_wave_b"));
			StructuredContent->SetStringField(TEXT("message"), TEXT("code_* write tools land in v0.29 Wave B"));
			StructuredContent->SetStringField(TEXT("toolName"), ToolName);
			return MakeExecutionResult(TEXT("code_* write tools land in v0.29 Wave B"), StructuredContent, true);
		}
	}

	FString LexToString(ECodePathClassification Classification)
	{
		switch (Classification)
		{
		case ECodePathClassification::DefaultWritable:
			return TEXT("default_writable");
		case ECodePathClassification::HighRisk:
			return TEXT("high_risk");
		case ECodePathClassification::OutsideProject:
			return TEXT("outside_project");
		case ECodePathClassification::Forbidden:
		default:
			return TEXT("forbidden");
		}
	}

	FCodePathPolicy ClassifyCodePath_Pure(
		const FString& ProjectDir,
		const FString& PluginBaseDir,
		const FString& RawInputPath,
		TFunctionRef<bool(const FString&)> PathExists,
		TFunctionRef<bool(const FString& Path, FString& OutTarget)> TryResolveSymlinkTarget)
	{
		const FString ProjectRoot = CodeToolsNormalizePath(ProjectDir);
		const FString PluginRoot = CodeToolsNormalizePath(PluginBaseDir);
		FString TrimmedPath = RawInputPath.TrimStartAndEnd();
		if (TrimmedPath.IsEmpty())
		{
			return CodeToolsMakePolicy(ECodePathClassification::Forbidden, FString(), TEXT("Path is required."));
		}

		FString CandidatePath = FPaths::IsRelative(TrimmedPath)
			? CodeToolsCombinePath(ProjectRoot, TrimmedPath)
			: CodeToolsNormalizePath(TrimmedPath);

		FString AmbiguousReason;
		if (CodeToolsHasAmbiguousPathSegment(CandidatePath, AmbiguousReason))
		{
			return CodeToolsMakePolicy(ECodePathClassification::Forbidden, CandidatePath, AmbiguousReason);
		}

		FString SymlinkTarget;
		if (PathExists(CandidatePath) && TryResolveSymlinkTarget(CandidatePath, SymlinkTarget) && !SymlinkTarget.IsEmpty())
		{
			CandidatePath = FPaths::IsRelative(SymlinkTarget)
				? CodeToolsCombinePath(FPaths::GetPath(CandidatePath), SymlinkTarget)
				: CodeToolsNormalizePath(SymlinkTarget);
		}

		if (CodeToolsHasAmbiguousPathSegment(CandidatePath, AmbiguousReason))
		{
			return CodeToolsMakePolicy(ECodePathClassification::Forbidden, CandidatePath, AmbiguousReason);
		}

		if (!CodeToolsPathEqualsOrChild(CandidatePath, ProjectRoot))
		{
			if (CodeToolsPathEqualsOrChild(CandidatePath, PluginRoot) || CodeToolsIsEnginePath(CandidatePath))
			{
				return CodeToolsMakePolicy(ECodePathClassification::Forbidden, CandidatePath, TEXT("Resolved path targets a forbidden plugin or Engine directory."));
			}
			return CodeToolsMakePolicy(ECodePathClassification::OutsideProject, CandidatePath, TEXT("Resolved path is outside the project."));
		}

		const FString ProjectRelativePath = CodeToolsProjectRelativePath(ProjectRoot, CandidatePath);
		if (CodeToolsPathEqualsOrChild(CandidatePath, PluginRoot)
			|| CodeToolsPathEqualsOrChild(CandidatePath, CodeToolsCombinePath(ProjectRoot, TEXT("Plugins/UnrealMcp"))))
		{
			return CodeToolsMakePolicy(ECodePathClassification::Forbidden, CandidatePath, TEXT("Plugins/UnrealMcp is forbidden for Code tool writes."));
		}
		if (CodeToolsIsSavedCodeChangesTarget(ProjectRelativePath))
		{
			return CodeToolsMakePolicy(ECodePathClassification::Forbidden, CandidatePath, TEXT("Saved/UnrealMcp/CodeChanges is tool-owned state, not a user-code target."));
		}
		if (CodeToolsIsExcludedProjectRelativePath(ProjectRelativePath))
		{
			return CodeToolsMakePolicy(ECodePathClassification::Forbidden, CandidatePath, TEXT("Resolved path is under a forbidden generated/runtime directory."));
		}
		if (CodeToolsHasForbiddenGeneratedExtension(CandidatePath))
		{
			return CodeToolsMakePolicy(ECodePathClassification::Forbidden, CandidatePath, TEXT("Generated headers, generated sources, assets, and maps are forbidden write targets."));
		}
		if (!CodeToolsExtensionAllowed(CandidatePath, CodeToolsWriteExtensions()))
		{
			return CodeToolsMakePolicy(ECodePathClassification::Forbidden, CandidatePath, FString::Printf(TEXT("Extension '%s' is not writable by Code tools."), *CodeToolsPathExtension(CandidatePath)));
		}

		if (CodeToolsPathEqualsOrChild(CandidatePath, CodeToolsCombinePath(ProjectRoot, TEXT("Tools/UnrealMcpPyToolSamples")))
			|| CodeToolsPathEqualsOrChild(CandidatePath, CodeToolsCombinePath(ProjectRoot, TEXT("Tools/UnrealMcpPyTools"))))
		{
			return CodeToolsMakePolicy(ECodePathClassification::DefaultWritable, CandidatePath, TEXT("Path is under a default-writable user Python tools root."));
		}

		const bool bHighRisk =
			CodeToolsPathEqualsOrChild(CandidatePath, CodeToolsCombinePath(ProjectRoot, TEXT("Source")))
			|| (ProjectRelativePath.StartsWith(TEXT("Plugins/"), ESearchCase::IgnoreCase) && ProjectRelativePath.Contains(TEXT("/Source/"), ESearchCase::IgnoreCase))
			|| (ProjectRelativePath.StartsWith(TEXT("Plugins/"), ESearchCase::IgnoreCase) && CandidatePath.EndsWith(TEXT(".uplugin"), ESearchCase::IgnoreCase))
			|| CandidatePath.EndsWith(TEXT(".uproject"), ESearchCase::IgnoreCase)
			|| (ProjectRelativePath.StartsWith(TEXT("Config/"), ESearchCase::IgnoreCase) && CandidatePath.EndsWith(TEXT(".ini"), ESearchCase::IgnoreCase));
		if (bHighRisk)
		{
			const FString ParentPath = CodeToolsNormalizePath(FPaths::GetPath(CandidatePath));
			const bool bParentExists = PathExists(ParentPath);
			return CodeToolsMakePolicy(
				ECodePathClassification::HighRisk,
				CandidatePath,
				bParentExists ? TEXT("Path is under a high-risk host project code/config root.") : TEXT("Path is under a high-risk root with an absent parent directory."));
		}

		return CodeToolsMakePolicy(ECodePathClassification::Forbidden, CandidatePath, TEXT("Path is not under an allowed writable root."));
	}

	bool TryExecuteCodeTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult)
	{
		if (ToolName == TEXT("unreal.code_workspace_status"))
		{
			OutResult = CodeToolsWorkspaceStatus(Arguments);
			return true;
		}
		if (ToolName == TEXT("unreal.code_list_files"))
		{
			OutResult = CodeToolsListFiles(Arguments);
			return true;
		}
		if (ToolName == TEXT("unreal.code_read_file"))
		{
			OutResult = CodeToolsReadFile(Arguments);
			return true;
		}
		if (ToolName == TEXT("unreal.code_search"))
		{
			OutResult = CodeToolsSearch(Arguments);
			return true;
		}
		if (ToolName == TEXT("unreal.code_preview_change"))
		{
			OutResult = CodeToolsWaveBStub(ToolName);
			return true;
		}
		if (ToolName == TEXT("unreal.code_apply_change"))
		{
			OutResult = CodeToolsWaveBStub(ToolName);
			return true;
		}
		if (ToolName == TEXT("unreal.code_rollback_change"))
		{
			OutResult = CodeToolsWaveBStub(ToolName);
			return true;
		}
		return false;
	}
}
