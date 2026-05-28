#include "UnrealMcpCodeTools.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Internationalization/Regex.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
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
		static constexpr int32 CodeToolsLockTtlSeconds = 900;
		static constexpr int32 CodeToolsMaxEditIdAttempts = 5;

#if WITH_DEV_AUTOMATION_TESTS
		FCodeToolsApplyTestHooks GCodeToolsApplyTestHooks;
#endif

		struct FCodeToolsTextBytes
		{
			TArray<uint8> Bytes;
			bool bValidUtf8 = false;
		};

		struct FCodeToolsEditPlan
		{
			FString Path;
			FString ProjectRelativePath;
			FString Operation;
			FString ExpectedSha256;
			FString ShaBefore;
			FString ShaAfter;
			FString BackupPath;
			FString PathRisk;
			FString PathReason;
			TArray<uint8> BeforeBytes;
			TArray<uint8> AfterBytes;
			bool bOriginalExisted = true;
		};

		struct FCodeToolsPreviewPlan
		{
			FString PreviewId;
			FString PreviewPath;
			FString RiskLevel = TEXT("low");
			FString UnifiedDiff;
			bool bRequiresApproval = false;
			bool bRequiresBuild = false;
			bool bRequiresRestart = false;
			TArray<FCodeToolsEditPlan> Edits;
			TArray<TSharedPtr<FJsonValue>> PathPolicyResults;
			TArray<TSharedPtr<FJsonValue>> ShaCheckResults;
			TSharedPtr<FJsonObject> PreviewObject;
		};

		struct FCodeToolsRollbackPlan
		{
			FString EditId;
			FString ManifestPath;
			FString RollbackId;
			bool bDriftDetected = false;
			TArray<FString> DriftFiles;
			TArray<FCodeToolsEditPlan> Edits;
			TSharedPtr<FJsonObject> ManifestObject;
		};

		bool CodeToolsRealPathExists(const FString& Path);
		bool CodeToolsTryResolveRealSymlinkTarget(const FString& Path, FString& OutTarget);

		class FScopedCodeChangeLock
		{
		public:
			explicit FScopedCodeChangeLock(const FString& ToolName)
			{
				const FString Owner = TEXT("Unreal MCP Code tools");
				const FString Reason = FString::Printf(TEXT("Executing %s"), *ToolName);
				bAcquired = TryAcquireExtensionSessionLock(Owner, Reason, CodeToolsLockTtlSeconds, false, SessionId, LockObject, FailureReason);
				bOwnsLock = bAcquired;
			}

			~FScopedCodeChangeLock()
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

			const FString& GetSessionId() const
			{
				return SessionId;
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

		bool CodeToolsReadFileBytes(const FString& Path, TArray<uint8>& OutBytes, FString& OutSha256)
		{
			OutBytes.Reset();
			OutSha256.Reset();
			if (!FFileHelper::LoadFileToArray(OutBytes, *Path))
			{
				return false;
			}
			OutSha256 = CodeToolsSha256Bytes(OutBytes);
			return true;
		}

		bool CodeToolsIsValidUtf8(const TArray<uint8>& Bytes)
		{
			int32 Index = 0;
			if (Bytes.Num() >= 3 && Bytes[0] == 0xef && Bytes[1] == 0xbb && Bytes[2] == 0xbf)
			{
				Index = 3;
			}
			while (Index < Bytes.Num())
			{
				const uint8 Byte = Bytes[Index];
				int32 ContinuationCount = 0;
				uint32 CodePoint = 0;
				if ((Byte & 0x80) == 0)
				{
					++Index;
					continue;
				}
				if ((Byte & 0xe0) == 0xc0)
				{
					ContinuationCount = 1;
					CodePoint = Byte & 0x1f;
					if (CodePoint == 0)
					{
						return false;
					}
				}
				else if ((Byte & 0xf0) == 0xe0)
				{
					ContinuationCount = 2;
					CodePoint = Byte & 0x0f;
				}
				else if ((Byte & 0xf8) == 0xf0)
				{
					ContinuationCount = 3;
					CodePoint = Byte & 0x07;
				}
				else
				{
					return false;
				}

				if (Index + ContinuationCount >= Bytes.Num())
				{
					return false;
				}
				for (int32 Offset = 1; Offset <= ContinuationCount; ++Offset)
				{
					const uint8 Continuation = Bytes[Index + Offset];
					if ((Continuation & 0xc0) != 0x80)
					{
						return false;
					}
					CodePoint = (CodePoint << 6) | (Continuation & 0x3f);
				}

				if ((ContinuationCount == 1 && CodePoint < 0x80)
					|| (ContinuationCount == 2 && CodePoint < 0x800)
					|| (ContinuationCount == 3 && CodePoint < 0x10000)
					|| CodePoint > 0x10ffff
					|| (CodePoint >= 0xd800 && CodePoint <= 0xdfff))
				{
					return false;
				}
				Index += ContinuationCount + 1;
			}
			return true;
		}

		TArray<uint8> CodeToolsStringToUtf8Bytes(const FString& Text)
		{
			TArray<uint8> Bytes;
			FTCHARToUTF8 Converter(*Text);
			Bytes.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
			return Bytes;
		}

		FString CodeToolsUtf8BytesToDisplayString(const TArray<uint8>& Bytes)
		{
			int32 Offset = 0;
			if (Bytes.Num() >= 3 && Bytes[0] == 0xef && Bytes[1] == 0xbb && Bytes[2] == 0xbf)
			{
				Offset = 3;
			}
			if (Offset >= Bytes.Num())
			{
				return FString();
			}
			FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + Offset), Bytes.Num() - Offset);
			return FString(Converter.Length(), Converter.Get());
		}

		bool CodeToolsFindUniqueByteMatch(
			const TArray<uint8>& Haystack,
			const TArray<uint8>& Needle,
			int32& OutIndex,
			FString& OutFailureCode,
			FString& OutFailureReason)
		{
			OutIndex = INDEX_NONE;
			if (Needle.Num() == 0)
			{
				OutFailureCode = TEXT("missingMatch");
				OutFailureReason = TEXT("Match text must not be empty.");
				return false;
			}

			int32 MatchCount = 0;
			for (int32 Index = 0; Index <= Haystack.Num() - Needle.Num(); ++Index)
			{
				bool bMatches = true;
				for (int32 NeedleIndex = 0; NeedleIndex < Needle.Num(); ++NeedleIndex)
				{
					if (Haystack[Index + NeedleIndex] != Needle[NeedleIndex])
					{
						bMatches = false;
						break;
					}
				}
				if (bMatches)
				{
					++MatchCount;
					OutIndex = Index;
					if (MatchCount > 1)
					{
						OutFailureCode = TEXT("ambiguousMatch");
						OutFailureReason = TEXT("Match text appears more than once.");
						return false;
					}
				}
			}

			if (MatchCount == 0)
			{
				OutFailureCode = TEXT("missingMatch");
				OutFailureReason = TEXT("Match text was not found.");
				return false;
			}
			return true;
		}

		bool CodeToolsApplyByteEdit(
			const TArray<uint8>& BeforeBytes,
			const FString& Operation,
			const FString& OldText,
			const FString& AnchorText,
			const FString& NewText,
			TArray<uint8>& OutAfterBytes,
			FString& OutFailureCode,
			FString& OutFailureReason)
		{
			const TArray<uint8> NewBytes = CodeToolsStringToUtf8Bytes(NewText);
			OutAfterBytes = BeforeBytes;
			if (Operation.Equals(TEXT("replace_exact"), ESearchCase::IgnoreCase))
			{
				const TArray<uint8> OldBytes = CodeToolsStringToUtf8Bytes(OldText);
				int32 MatchIndex = INDEX_NONE;
				if (!CodeToolsFindUniqueByteMatch(BeforeBytes, OldBytes, MatchIndex, OutFailureCode, OutFailureReason))
				{
					return false;
				}
				OutAfterBytes.Reset();
				OutAfterBytes.Append(BeforeBytes.GetData(), MatchIndex);
				OutAfterBytes.Append(NewBytes);
				OutAfterBytes.Append(BeforeBytes.GetData() + MatchIndex + OldBytes.Num(), BeforeBytes.Num() - MatchIndex - OldBytes.Num());
				return true;
			}
			if (Operation.Equals(TEXT("insert_before"), ESearchCase::IgnoreCase)
				|| Operation.Equals(TEXT("insert_after"), ESearchCase::IgnoreCase))
			{
				const TArray<uint8> AnchorBytes = CodeToolsStringToUtf8Bytes(AnchorText);
				int32 MatchIndex = INDEX_NONE;
				if (!CodeToolsFindUniqueByteMatch(BeforeBytes, AnchorBytes, MatchIndex, OutFailureCode, OutFailureReason))
				{
					return false;
				}
				const int32 InsertIndex = Operation.Equals(TEXT("insert_after"), ESearchCase::IgnoreCase)
					? MatchIndex + AnchorBytes.Num()
					: MatchIndex;
				OutAfterBytes.Reset();
				OutAfterBytes.Append(BeforeBytes.GetData(), InsertIndex);
				OutAfterBytes.Append(NewBytes);
				OutAfterBytes.Append(BeforeBytes.GetData() + InsertIndex, BeforeBytes.Num() - InsertIndex);
				return true;
			}
			if (Operation.Equals(TEXT("create_file"), ESearchCase::IgnoreCase))
			{
				OutAfterBytes = NewBytes;
				return true;
			}

			OutFailureCode = TEXT("missingMatch");
			OutFailureReason = FString::Printf(TEXT("Unsupported code edit operation '%s'."), *Operation);
			return false;
		}

		TSharedPtr<FJsonObject> CodeToolsMakeErrorObject(const FString& Code, const FString& Message)
		{
			TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
			ErrorObject->SetStringField(TEXT("code"), Code);
			ErrorObject->SetStringField(TEXT("message"), Message);
			return ErrorObject;
		}

		FUnrealMcpExecutionResult CodeToolsMakeErrorResult(const FString& Code, const FString& Message)
		{
			return MakeExecutionResult(Message, CodeToolsMakeErrorObject(Code, Message), true);
		}

		FString CodeToolsChangesRoot()
		{
			return CodeToolsNormalizePath(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/CodeChanges")));
		}

		FString CodeToolsPreviewsRoot()
		{
			return CodeToolsCombinePath(CodeToolsChangesRoot(), TEXT("Previews"));
		}

		FString CodeToolsBackupsRoot()
		{
			return CodeToolsCombinePath(CodeToolsChangesRoot(), TEXT("Backups"));
		}

		FString CodeToolsRollbacksRoot()
		{
			return CodeToolsCombinePath(CodeToolsChangesRoot(), TEXT("Rollbacks"));
		}

		FString CodeToolsLastChangePath()
		{
			return CodeToolsCombinePath(CodeToolsChangesRoot(), TEXT("LastCodeChange.json"));
		}

		bool CodeToolsIsSafeArtifactId(const FString& Id)
		{
			if (Id.IsEmpty() || Id.Len() > 160)
			{
				return false;
			}
			for (int32 Index = 0; Index < Id.Len(); ++Index)
			{
				const TCHAR Ch = Id[Index];
				const bool bAllowed = (Ch >= TEXT('a') && Ch <= TEXT('z'))
					|| (Ch >= TEXT('A') && Ch <= TEXT('Z'))
					|| (Ch >= TEXT('0') && Ch <= TEXT('9'))
					|| Ch == TEXT('_')
					|| Ch == TEXT('-');
				if (!bAllowed)
				{
					return false;
				}
			}
			return true;
		}

		bool CodeToolsWriteBytesAtomic(const FString& TargetPath, const TArray<uint8>& Bytes, FString& OutFailureReason)
		{
			const FString TargetDir = FPaths::GetPath(TargetPath);
			if (!IFileManager::Get().MakeDirectory(*TargetDir, true))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to create directory for '%s'."), *TargetPath);
				return false;
			}
			const FString TempPath = FString::Printf(TEXT("%s.tmp.%s"), *TargetPath, *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
			if (!FFileHelper::SaveArrayToFile(Bytes, *TempPath))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to write temporary file '%s'."), *TempPath);
				return false;
			}
			if (!FPlatformFileManager::Get().GetPlatformFile().MoveFile(*TargetPath, *TempPath))
			{
				IFileManager::Get().Delete(*TempPath, false, true);
				OutFailureReason = FString::Printf(TEXT("Failed to move temporary file into '%s'."), *TargetPath);
				return false;
			}
			return true;
		}

		bool CodeToolsWriteJsonObjectAtomic(const TSharedPtr<FJsonObject>& Object, const FString& TargetPath, FString& OutFailureReason)
		{
			if (!Object.IsValid())
			{
				OutFailureReason = TEXT("Cannot write an invalid JSON object.");
				return false;
			}
			FString JsonText;
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonText);
			if (!FJsonSerializer::Serialize(Object.ToSharedRef(), Writer))
			{
				OutFailureReason = TEXT("Failed to serialize JSON object.");
				return false;
			}

			const FString TargetDir = FPaths::GetPath(TargetPath);
			if (!IFileManager::Get().MakeDirectory(*TargetDir, true))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to create directory for '%s'."), *TargetPath);
				return false;
			}
			const FString TempPath = FString::Printf(TEXT("%s.tmp.%s"), *TargetPath, *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
			if (!FFileHelper::SaveStringToFile(JsonText, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to write temporary JSON file '%s'."), *TempPath);
				return false;
			}
			if (!IFileManager::Get().Move(*TargetPath, *TempPath, true, true))
			{
				IFileManager::Get().Delete(*TempPath, false, true);
				OutFailureReason = FString::Printf(TEXT("Failed to atomically replace JSON file '%s'."), *TargetPath);
				return false;
			}
			return true;
		}

		TSharedPtr<FJsonObject> CodeToolsMakePathPolicyObject(const FString& Path, const FCodePathPolicy& Policy)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("path"), Path);
			Object->SetStringField(TEXT("resolvedPath"), Policy.CanonicalPath);
			Object->SetStringField(TEXT("classification"), LexToString(Policy.Classification));
			Object->SetStringField(TEXT("reason"), Policy.Reason);
			return Object;
		}

		TSharedPtr<FJsonObject> CodeToolsMakeShaCheckObject(const FString& Path, const FString& Expected, const FString& Current, bool bMatch)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("path"), Path);
			Object->SetStringField(TEXT("expected"), Expected);
			Object->SetStringField(TEXT("current"), Current);
			Object->SetBoolField(TEXT("match"), bMatch);
			return Object;
		}

		bool CodeToolsPathNeedsBuild(const FString& Path)
		{
			const FString Ext = CodeToolsPathExtension(Path);
			return Ext.Equals(TEXT(".h"), ESearchCase::IgnoreCase)
				|| Ext.Equals(TEXT(".hpp"), ESearchCase::IgnoreCase)
				|| Ext.Equals(TEXT(".cpp"), ESearchCase::IgnoreCase)
				|| Ext.Equals(TEXT(".inl"), ESearchCase::IgnoreCase)
				|| Ext.Equals(TEXT(".ipp"), ESearchCase::IgnoreCase)
				|| Ext.Equals(TEXT(".Build.cs"), ESearchCase::IgnoreCase)
				|| Ext.Equals(TEXT(".Target.cs"), ESearchCase::IgnoreCase);
		}

		bool CodeToolsPathNeedsRestart(const FString& Path)
		{
			const FString Ext = CodeToolsPathExtension(Path);
			return Ext.Equals(TEXT(".Build.cs"), ESearchCase::IgnoreCase)
				|| Ext.Equals(TEXT(".Target.cs"), ESearchCase::IgnoreCase)
				|| Ext.Equals(TEXT(".uplugin"), ESearchCase::IgnoreCase)
				|| Ext.Equals(TEXT(".uproject"), ESearchCase::IgnoreCase);
		}

		FString CodeToolsJsonValueToComparableString(const TSharedPtr<FJsonValue>& Value)
		{
			TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
			Wrapper->SetField(TEXT("value"), Value);
			FString JsonText;
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonText);
			FJsonSerializer::Serialize(Wrapper.ToSharedRef(), Writer);
			return JsonText;
		}

		bool CodeToolsParseJsonBytes(const TArray<uint8>& Bytes, TSharedPtr<FJsonObject>& OutObject)
		{
			OutObject.Reset();
			const FString Text = CodeToolsUtf8BytesToDisplayString(Bytes);
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
			return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
		}

		bool CodeToolsIsEngineAssociationOnlyUprojectChange(const TArray<uint8>& BeforeBytes, const TArray<uint8>& AfterBytes)
		{
			TSharedPtr<FJsonObject> BeforeObject;
			TSharedPtr<FJsonObject> AfterObject;
			if (!CodeToolsParseJsonBytes(BeforeBytes, BeforeObject) || !CodeToolsParseJsonBytes(AfterBytes, AfterObject))
			{
				return false;
			}

			TSet<FString> Keys;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : BeforeObject->Values)
			{
				Keys.Add(Pair.Key);
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : AfterObject->Values)
			{
				Keys.Add(Pair.Key);
			}

			bool bEngineAssociationChanged = false;
			for (const FString& Key : Keys)
			{
				const TSharedPtr<FJsonValue>* BeforeValue = BeforeObject->Values.Find(Key);
				const TSharedPtr<FJsonValue>* AfterValue = AfterObject->Values.Find(Key);
				const FString BeforeText = BeforeValue ? CodeToolsJsonValueToComparableString(*BeforeValue) : TEXT("<missing>");
				const FString AfterText = AfterValue ? CodeToolsJsonValueToComparableString(*AfterValue) : TEXT("<missing>");
				if (BeforeText == AfterText)
				{
					continue;
				}
				if (Key.Equals(TEXT("EngineAssociation"), ESearchCase::CaseSensitive))
				{
					bEngineAssociationChanged = true;
					continue;
				}
				return false;
			}
			return bEngineAssociationChanged;
		}

		TSharedPtr<FJsonObject> CodeToolsMakeTouchedFileObject(const FCodeToolsEditPlan& Edit, bool bForPreview)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("path"), Edit.ProjectRelativePath);
			Object->SetStringField(TEXT("projectRelativePath"), Edit.ProjectRelativePath);
			Object->SetStringField(TEXT("sourcePath"), Edit.Path);
			Object->SetStringField(TEXT("operation"), Edit.Operation);
			Object->SetStringField(TEXT("shaBefore"), Edit.ShaBefore);
			Object->SetStringField(TEXT("shaAfter"), Edit.ShaAfter);
			Object->SetBoolField(TEXT("originalExisted"), Edit.bOriginalExisted);
			if (!Edit.BackupPath.IsEmpty())
			{
				Object->SetStringField(TEXT("backupPath"), Edit.BackupPath);
			}
			if (bForPreview)
			{
				Object->SetStringField(TEXT("expectedNewSha256"), Edit.ShaAfter);
				Object->SetStringField(TEXT("pathRisk"), Edit.PathRisk);
				Object->SetStringField(TEXT("pathPolicyReason"), Edit.PathReason);
			}
			return Object;
		}

		TArray<TSharedPtr<FJsonValue>> CodeToolsMakeTouchedFileArray(const TArray<FCodeToolsEditPlan>& Edits, bool bForPreview)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FCodeToolsEditPlan& Edit : Edits)
			{
				Values.Add(MakeShared<FJsonValueObject>(CodeToolsMakeTouchedFileObject(Edit, bForPreview)));
			}
			return Values;
		}

		FString CodeToolsMakeUnifiedDiff(const TArray<FCodeToolsEditPlan>& Edits)
		{
			FString Diff;
			for (const FCodeToolsEditPlan& Edit : Edits)
			{
				if (!Diff.IsEmpty())
				{
					Diff += TEXT("\n");
				}
				Diff += FString::Printf(
					TEXT("--- %s\n+++ %s\n@@ byte-exact %s sha %s -> %s @@\n"),
					Edit.bOriginalExisted ? *FString::Printf(TEXT("a/%s"), *Edit.ProjectRelativePath) : TEXT("/dev/null"),
					*FString::Printf(TEXT("b/%s"), *Edit.ProjectRelativePath),
					*Edit.Operation,
					*Edit.ShaBefore,
					*Edit.ShaAfter);

				TArray<FString> BeforeLines;
				TArray<FString> AfterLines;
				CodeToolsUtf8BytesToDisplayString(Edit.BeforeBytes).ParseIntoArrayLines(BeforeLines, false);
				CodeToolsUtf8BytesToDisplayString(Edit.AfterBytes).ParseIntoArrayLines(AfterLines, false);
				const int32 MaxDisplayLines = 80;
				const int32 BeforeCount = FMath::Min(BeforeLines.Num(), MaxDisplayLines);
				for (int32 Index = 0; Index < BeforeCount; ++Index)
				{
					Diff += FString::Printf(TEXT("-%s\n"), *BeforeLines[Index]);
				}
				if (BeforeLines.Num() > MaxDisplayLines)
				{
					Diff += TEXT("-... truncated ...\n");
				}
				const int32 AfterCount = FMath::Min(AfterLines.Num(), MaxDisplayLines);
				for (int32 Index = 0; Index < AfterCount; ++Index)
				{
					Diff += FString::Printf(TEXT("+%s\n"), *AfterLines[Index]);
				}
				if (AfterLines.Num() > MaxDisplayLines)
				{
					Diff += TEXT("+... truncated ...\n");
				}
			}
			return Diff;
		}

		TSharedPtr<FJsonObject> CodeToolsMakeManifestObject(
			const FString& Action,
			const FString& EditId,
			const FString& PreviewId,
			const FString& ApplyState,
			const FString& SessionId,
			const TArray<FCodeToolsEditPlan>& Edits,
			const FString& PathRisk,
			bool bDryRun,
			bool bRequiresBuild,
			bool bRequiresRestart,
			bool bForcedOverDrift)
		{
			TSharedPtr<FJsonObject> Manifest = MakeShared<FJsonObject>();
			Manifest->SetNumberField(TEXT("schemaVersion"), 1);
			Manifest->SetStringField(TEXT("action"), Action);
			Manifest->SetStringField(TEXT("editId"), EditId);
			Manifest->SetStringField(TEXT("previewId"), PreviewId);
			Manifest->SetStringField(TEXT("timestampUtc"), FDateTime::UtcNow().ToIso8601());
			Manifest->SetStringField(TEXT("sessionId"), SessionId);
			Manifest->SetStringField(TEXT("applyState"), ApplyState);
			Manifest->SetArrayField(TEXT("touchedFiles"), CodeToolsMakeTouchedFileArray(Edits, false));
			Manifest->SetStringField(TEXT("pathRisk"), PathRisk);
			Manifest->SetBoolField(TEXT("dryRun"), bDryRun);
			Manifest->SetBoolField(TEXT("requiresBuild"), bRequiresBuild);
			Manifest->SetBoolField(TEXT("requiresRestart"), bRequiresRestart);
			if (bForcedOverDrift)
			{
				Manifest->SetBoolField(TEXT("forcedOverDrift"), true);
			}
			TSharedPtr<FJsonObject> Correlation = MakeShared<FJsonObject>();
			Correlation->SetStringField(TEXT("extensionLockSessionId"), SessionId);
			Manifest->SetObjectField(TEXT("correlation"), Correlation);
			return Manifest;
		}

		bool CodeToolsWriteApplyManifestState(
			const FString& ManifestPath,
			const FString& State,
			const FString& EditId,
			const FString& PreviewId,
			const FString& SessionId,
			const FCodeToolsPreviewPlan& Plan,
			FString& OutFailureReason)
		{
			TSharedPtr<FJsonObject> Manifest = CodeToolsMakeManifestObject(
				TEXT("code_apply_change"),
				EditId,
				PreviewId,
				State,
				SessionId,
				Plan.Edits,
				Plan.RiskLevel,
				false,
				Plan.bRequiresBuild,
				Plan.bRequiresRestart,
				false);
			const bool bWrote = CodeToolsWriteJsonObjectAtomic(Manifest, ManifestPath, OutFailureReason);
#if WITH_DEV_AUTOMATION_TESTS
			if (bWrote && GCodeToolsApplyTestHooks.AfterApplyManifestState)
			{
				GCodeToolsApplyTestHooks.AfterApplyManifestState(State, ManifestPath);
			}
#endif
			return bWrote;
		}

		bool CodeToolsBuildPreviewPlanFromEdits(
			const TArray<TSharedPtr<FJsonValue>>& EditValues,
			const FString& PreviewId,
			FCodeToolsPreviewPlan& OutPlan,
			FString& OutFailureCode,
			FString& OutFailureReason)
		{
			OutPlan = FCodeToolsPreviewPlan();
			OutPlan.PreviewId = PreviewId;
			if (EditValues.Num() == 0)
			{
				OutFailureCode = TEXT("missingMatch");
				OutFailureReason = TEXT("edits must contain at least one edit.");
				return false;
			}

			const FString ProjectRoot = CodeToolsNormalizePath(FPaths::ProjectDir());
			const FString PluginRoot = CodeToolsResolvePluginBaseDir();
			TSet<FString> SeenPaths;
			TArray<TSharedPtr<FJsonValue>> RequestedEditValues;

			for (const TSharedPtr<FJsonValue>& EditValue : EditValues)
			{
				const TSharedPtr<FJsonObject> EditObject = EditValue.IsValid() ? EditValue->AsObject() : nullptr;
				if (!EditObject.IsValid())
				{
					OutFailureCode = TEXT("missingMatch");
					OutFailureReason = TEXT("Each edit must be an object.");
					return false;
				}

				FString RawPath;
				FString ExpectedSha;
				FString Operation;
				FString OldText;
				FString NewText;
				FString AnchorText;
				EditObject->TryGetStringField(TEXT("path"), RawPath);
				EditObject->TryGetStringField(TEXT("expectedSha256"), ExpectedSha);
				EditObject->TryGetStringField(TEXT("operation"), Operation);
				EditObject->TryGetStringField(TEXT("oldText"), OldText);
				EditObject->TryGetStringField(TEXT("newText"), NewText);
				EditObject->TryGetStringField(TEXT("anchorText"), AnchorText);
				RawPath = RawPath.TrimStartAndEnd();
				ExpectedSha = ExpectedSha.TrimStartAndEnd().ToLower();
				Operation = Operation.TrimStartAndEnd();

				if (RawPath.IsEmpty() || Operation.IsEmpty())
				{
					OutFailureCode = TEXT("missingMatch");
					OutFailureReason = TEXT("Each edit requires path and operation.");
					return false;
				}
				if (Operation.Equals(TEXT("replace_exact"), ESearchCase::IgnoreCase))
				{
					if (!EditObject->HasField(TEXT("oldText")) || !EditObject->HasField(TEXT("newText")))
					{
						OutFailureCode = TEXT("missingMatch");
						OutFailureReason = TEXT("replace_exact requires oldText and newText.");
						return false;
					}
				}
				else if (Operation.Equals(TEXT("insert_before"), ESearchCase::IgnoreCase)
					|| Operation.Equals(TEXT("insert_after"), ESearchCase::IgnoreCase))
				{
					if (!EditObject->HasField(TEXT("anchorText")) || !EditObject->HasField(TEXT("newText")))
					{
						OutFailureCode = TEXT("missingMatch");
						OutFailureReason = TEXT("insert_before/insert_after require anchorText and newText.");
						return false;
					}
				}
				else if (Operation.Equals(TEXT("create_file"), ESearchCase::IgnoreCase))
				{
					if (!EditObject->HasField(TEXT("newText")))
					{
						OutFailureCode = TEXT("missingMatch");
						OutFailureReason = TEXT("create_file requires newText.");
						return false;
					}
				}
				else
				{
					OutFailureCode = TEXT("missingMatch");
					OutFailureReason = FString::Printf(TEXT("Unsupported operation '%s'."), *Operation);
					return false;
				}

				const FCodePathPolicy Policy = ClassifyCodePath_Pure(
					ProjectRoot,
					PluginRoot,
					RawPath,
					[](const FString& Path) { return CodeToolsRealPathExists(Path); },
					[](const FString& Path, FString& OutTarget) { return CodeToolsTryResolveRealSymlinkTarget(Path, OutTarget); });
				OutPlan.PathPolicyResults.Add(MakeShared<FJsonValueObject>(CodeToolsMakePathPolicyObject(RawPath, Policy)));
				if (Policy.Classification == ECodePathClassification::Forbidden
					|| Policy.Classification == ECodePathClassification::OutsideProject)
				{
					OutFailureCode = TEXT("forbiddenPath");
					OutFailureReason = Policy.Reason;
					return false;
				}

				const FString ProjectRelativePath = CodeToolsProjectRelativePath(ProjectRoot, Policy.CanonicalPath);
				if (SeenPaths.Contains(ProjectRelativePath.ToLower()))
				{
					OutFailureCode = TEXT("ambiguousMatch");
					OutFailureReason = FString::Printf(TEXT("Multiple edits for the same file are not supported in one preview: %s"), *ProjectRelativePath);
					return false;
				}
				SeenPaths.Add(ProjectRelativePath.ToLower());

				FCodeToolsEditPlan Edit;
				Edit.Path = Policy.CanonicalPath;
				Edit.ProjectRelativePath = ProjectRelativePath;
				Edit.Operation = Operation;
				Edit.ExpectedSha256 = ExpectedSha;
				Edit.PathRisk = Policy.Classification == ECodePathClassification::HighRisk ? TEXT("high") : TEXT("low");
				Edit.PathReason = Policy.Reason;
				Edit.bOriginalExisted = !Operation.Equals(TEXT("create_file"), ESearchCase::IgnoreCase);

				if (Policy.Classification == ECodePathClassification::HighRisk)
				{
					OutPlan.RiskLevel = TEXT("high");
					OutPlan.bRequiresApproval = true;
				}
				OutPlan.bRequiresBuild = OutPlan.bRequiresBuild || CodeToolsPathNeedsBuild(Edit.Path);
				OutPlan.bRequiresRestart = OutPlan.bRequiresRestart || CodeToolsPathNeedsRestart(Edit.Path);

				if (Operation.Equals(TEXT("create_file"), ESearchCase::IgnoreCase))
				{
					if (FPaths::FileExists(Edit.Path))
					{
						FString CurrentSha;
						TArray<uint8> ExistingBytes;
						CodeToolsReadFileBytes(Edit.Path, ExistingBytes, CurrentSha);
						OutPlan.ShaCheckResults.Add(MakeShared<FJsonValueObject>(CodeToolsMakeShaCheckObject(RawPath, ExpectedSha, CurrentSha, false)));
						OutFailureCode = TEXT("staleExpectedSha");
						OutFailureReason = FString::Printf(TEXT("create_file target already exists: %s"), *ProjectRelativePath);
						return false;
					}
					const FString ParentPath = CodeToolsNormalizePath(FPaths::GetPath(Edit.Path));
					if (Policy.Classification == ECodePathClassification::HighRisk && !FPaths::DirectoryExists(ParentPath))
					{
						OutFailureCode = TEXT("forbiddenPath");
						OutFailureReason = TEXT("create_file into a high-risk root requires an existing parent directory.");
						return false;
					}
					Edit.ShaBefore = FString();
					Edit.BeforeBytes.Reset();
					if (!ExpectedSha.IsEmpty())
					{
						OutPlan.ShaCheckResults.Add(MakeShared<FJsonValueObject>(CodeToolsMakeShaCheckObject(RawPath, ExpectedSha, TEXT("<missing>"), false)));
						OutFailureCode = TEXT("staleExpectedSha");
						OutFailureReason = TEXT("create_file expectedSha256 must be empty because the target must not exist.");
						return false;
					}
					OutPlan.ShaCheckResults.Add(MakeShared<FJsonValueObject>(CodeToolsMakeShaCheckObject(RawPath, ExpectedSha, TEXT("<missing>"), true)));
				}
				else
				{
					if (ExpectedSha.IsEmpty())
					{
						OutFailureCode = TEXT("staleExpectedSha");
						OutFailureReason = TEXT("expectedSha256 is required for non-create edits.");
						return false;
					}
					if (!CodeToolsReadFileBytes(Edit.Path, Edit.BeforeBytes, Edit.ShaBefore))
					{
						OutPlan.ShaCheckResults.Add(MakeShared<FJsonValueObject>(CodeToolsMakeShaCheckObject(RawPath, ExpectedSha, TEXT("<missing>"), false)));
						OutFailureCode = TEXT("staleExpectedSha");
						OutFailureReason = FString::Printf(TEXT("File is missing or unreadable: %s"), *ProjectRelativePath);
						return false;
					}
					const bool bShaMatches = Edit.ShaBefore.Equals(ExpectedSha, ESearchCase::IgnoreCase);
					OutPlan.ShaCheckResults.Add(MakeShared<FJsonValueObject>(CodeToolsMakeShaCheckObject(RawPath, ExpectedSha, Edit.ShaBefore, bShaMatches)));
					if (!bShaMatches)
					{
						OutFailureCode = TEXT("staleExpectedSha");
						OutFailureReason = FString::Printf(TEXT("Current sha for %s does not match expectedSha256."), *ProjectRelativePath);
						return false;
					}
					if (!CodeToolsIsValidUtf8(Edit.BeforeBytes))
					{
						OutFailureCode = TEXT("forbiddenPath");
						OutFailureReason = FString::Printf(TEXT("File is not valid UTF-8: %s"), *ProjectRelativePath);
						return false;
					}
				}

				if (!CodeToolsApplyByteEdit(Edit.BeforeBytes, Operation, OldText, AnchorText, NewText, Edit.AfterBytes, OutFailureCode, OutFailureReason))
				{
					OutFailureReason = FString::Printf(TEXT("%s in %s"), *OutFailureReason, *ProjectRelativePath);
					return false;
				}
				Edit.ShaAfter = CodeToolsSha256Bytes(Edit.AfterBytes);
				if (CodeToolsPathExtension(Edit.Path).Equals(TEXT(".uproject"), ESearchCase::IgnoreCase)
					&& Edit.bOriginalExisted
					&& CodeToolsIsEngineAssociationOnlyUprojectChange(Edit.BeforeBytes, Edit.AfterBytes))
				{
					OutFailureCode = TEXT("forbiddenPath");
					OutFailureReason = TEXT("EngineAssociation-only .uproject edits must use unreal.project_version_migration.");
					return false;
				}

				TSharedPtr<FJsonObject> StoredEdit = MakeShared<FJsonObject>();
				StoredEdit->SetStringField(TEXT("path"), ProjectRelativePath);
				StoredEdit->SetStringField(TEXT("expectedSha256"), ExpectedSha);
				StoredEdit->SetStringField(TEXT("operation"), Operation);
				if (EditObject->HasField(TEXT("oldText")))
				{
					StoredEdit->SetStringField(TEXT("oldText"), OldText);
				}
				if (EditObject->HasField(TEXT("anchorText")))
				{
					StoredEdit->SetStringField(TEXT("anchorText"), AnchorText);
				}
				if (EditObject->HasField(TEXT("newText")))
				{
					StoredEdit->SetStringField(TEXT("newText"), NewText);
				}
				RequestedEditValues.Add(MakeShared<FJsonValueObject>(StoredEdit));
				OutPlan.Edits.Add(Edit);
			}

			OutPlan.UnifiedDiff = CodeToolsMakeUnifiedDiff(OutPlan.Edits);
			OutPlan.PreviewPath = CodeToolsCombinePath(CodeToolsPreviewsRoot(), PreviewId + TEXT(".json"));
			OutPlan.PreviewObject = MakeShared<FJsonObject>();
			OutPlan.PreviewObject->SetNumberField(TEXT("schemaVersion"), 1);
			OutPlan.PreviewObject->SetStringField(TEXT("previewId"), PreviewId);
			OutPlan.PreviewObject->SetStringField(TEXT("timestampUtc"), FDateTime::UtcNow().ToIso8601());
			OutPlan.PreviewObject->SetStringField(TEXT("riskLevel"), OutPlan.RiskLevel);
			OutPlan.PreviewObject->SetBoolField(TEXT("requiresApproval"), OutPlan.bRequiresApproval);
			OutPlan.PreviewObject->SetBoolField(TEXT("wouldRequireBuild"), OutPlan.bRequiresBuild);
			OutPlan.PreviewObject->SetBoolField(TEXT("wouldRequireRestart"), OutPlan.bRequiresRestart);
			OutPlan.PreviewObject->SetStringField(TEXT("unifiedDiff"), OutPlan.UnifiedDiff);
			OutPlan.PreviewObject->SetArrayField(TEXT("requestedEdits"), RequestedEditValues);
			OutPlan.PreviewObject->SetArrayField(TEXT("touchedFiles"), CodeToolsMakeTouchedFileArray(OutPlan.Edits, true));
			OutPlan.PreviewObject->SetArrayField(TEXT("pathPolicyResult"), OutPlan.PathPolicyResults);
			OutPlan.PreviewObject->SetArrayField(TEXT("shaCheckResult"), OutPlan.ShaCheckResults);
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

		bool CodeToolsGetRequestedEditsFromPreview(
			const TSharedPtr<FJsonObject>& PreviewObject,
			const TArray<TSharedPtr<FJsonValue>>*& OutEditValues,
			FString& OutPreviewId,
			FString& OutFailureReason)
		{
			OutEditValues = nullptr;
			OutPreviewId.Reset();
			if (!PreviewObject.IsValid())
			{
				OutFailureReason = TEXT("Preview artifact is invalid JSON.");
				return false;
			}
			PreviewObject->TryGetStringField(TEXT("previewId"), OutPreviewId);
			if (OutPreviewId.IsEmpty() || !CodeToolsIsSafeArtifactId(OutPreviewId))
			{
				OutFailureReason = TEXT("Preview artifact has an invalid previewId.");
				return false;
			}
			if (!PreviewObject->TryGetArrayField(TEXT("requestedEdits"), OutEditValues) || !OutEditValues)
			{
				OutFailureReason = TEXT("Preview artifact is missing requestedEdits.");
				return false;
			}
			return true;
		}

		TSharedPtr<FJsonObject> CodeToolsMakePreviewResponse(const FCodeToolsPreviewPlan& Plan)
		{
			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("previewId"), Plan.PreviewId);
			StructuredContent->SetStringField(TEXT("previewPath"), Plan.PreviewPath);
			StructuredContent->SetStringField(TEXT("unifiedDiff"), Plan.UnifiedDiff);
			StructuredContent->SetArrayField(TEXT("touchedFiles"), CodeToolsMakeTouchedFileArray(Plan.Edits, true));
			StructuredContent->SetStringField(TEXT("riskLevel"), Plan.RiskLevel);
			StructuredContent->SetBoolField(TEXT("requiresApproval"), Plan.bRequiresApproval);
			StructuredContent->SetBoolField(TEXT("wouldRequireBuild"), Plan.bRequiresBuild);
			StructuredContent->SetBoolField(TEXT("wouldRequireRestart"), Plan.bRequiresRestart);
			StructuredContent->SetArrayField(TEXT("pathPolicyResult"), Plan.PathPolicyResults);
			StructuredContent->SetArrayField(TEXT("shaCheckResult"), Plan.ShaCheckResults);
			return StructuredContent;
		}

		FUnrealMcpExecutionResult CodeToolsPreviewChange(const FJsonObject& Arguments)
		{
			const TArray<TSharedPtr<FJsonValue>>* EditValues = nullptr;
			if (!Arguments.TryGetArrayField(TEXT("edits"), EditValues) || !EditValues)
			{
				return CodeToolsMakeErrorResult(TEXT("missingMatch"), TEXT("edits is required."));
			}

			const FString PreviewId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
			FCodeToolsPreviewPlan Plan;
			FString FailureCode;
			FString FailureReason;
			if (!CodeToolsBuildPreviewPlanFromEdits(*EditValues, PreviewId, Plan, FailureCode, FailureReason))
			{
				return CodeToolsMakeErrorResult(FailureCode, FailureReason);
			}

			if (!CodeToolsWriteJsonObjectAtomic(Plan.PreviewObject, Plan.PreviewPath, FailureReason))
			{
				return CodeToolsMakeErrorResult(TEXT("forbiddenPath"), FailureReason);
			}
			return MakeExecutionResult(
				FString::Printf(TEXT("Previewed %d code edit(s)."), Plan.Edits.Num()),
				CodeToolsMakePreviewResponse(Plan),
				false);
		}

		bool CodeToolsLoadPreviewPlan(const FString& PreviewId, FCodeToolsPreviewPlan& OutPlan, FString& OutFailureCode, FString& OutFailureReason)
		{
			if (!CodeToolsIsSafeArtifactId(PreviewId))
			{
				OutFailureCode = TEXT("missingMatch");
				OutFailureReason = TEXT("previewId is invalid.");
				return false;
			}
			const FString PreviewPath = CodeToolsCombinePath(CodeToolsPreviewsRoot(), PreviewId + TEXT(".json"));
			TSharedPtr<FJsonObject> PreviewObject;
			if (!LoadJsonObjectFromFile(PreviewPath, PreviewObject, OutFailureReason) || !PreviewObject.IsValid())
			{
				OutFailureCode = TEXT("missingMatch");
				OutFailureReason = FString::Printf(TEXT("Preview artifact not found or unreadable: %s"), *PreviewId);
				return false;
			}

			const TArray<TSharedPtr<FJsonValue>>* EditValues = nullptr;
			FString StoredPreviewId;
			if (!CodeToolsGetRequestedEditsFromPreview(PreviewObject, EditValues, StoredPreviewId, OutFailureReason))
			{
				OutFailureCode = TEXT("missingMatch");
				return false;
			}
			if (StoredPreviewId != PreviewId)
			{
				OutFailureCode = TEXT("missingMatch");
				OutFailureReason = TEXT("Preview artifact previewId does not match the requested previewId.");
				return false;
			}
			if (!CodeToolsBuildPreviewPlanFromEdits(*EditValues, PreviewId, OutPlan, OutFailureCode, OutFailureReason))
			{
				return false;
			}
			OutPlan.PreviewPath = PreviewPath;
			return true;
		}

		FString CodeToolsSanitizeArtifactName(FString Value)
		{
			Value.ReplaceInline(TEXT("\\"), TEXT("_"), ESearchCase::CaseSensitive);
			Value.ReplaceInline(TEXT("/"), TEXT("_"), ESearchCase::CaseSensitive);
			Value.ReplaceInline(TEXT(":"), TEXT("_"), ESearchCase::CaseSensitive);
			Value.ReplaceInline(TEXT("*"), TEXT("_"), ESearchCase::CaseSensitive);
			Value.ReplaceInline(TEXT("?"), TEXT("_"), ESearchCase::CaseSensitive);
			Value.ReplaceInline(TEXT("\""), TEXT("_"), ESearchCase::CaseSensitive);
			Value.ReplaceInline(TEXT("<"), TEXT("_"), ESearchCase::CaseSensitive);
			Value.ReplaceInline(TEXT(">"), TEXT("_"), ESearchCase::CaseSensitive);
			Value.ReplaceInline(TEXT("|"), TEXT("_"), ESearchCase::CaseSensitive);
			return Value.Left(96);
		}

		FString CodeToolsGenerateEditId()
		{
			return FString::Printf(
				TEXT("code-%s-%s"),
				*FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")),
				*FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
		}

		bool CodeToolsTryCreateBackupDirectory(FString& OutEditId, FString& OutBackupDir, FString& OutFailureCode, FString& OutFailureReason)
		{
			for (int32 Attempt = 0; Attempt < CodeToolsMaxEditIdAttempts; ++Attempt)
			{
				const FString EditId = CodeToolsGenerateEditId();
				const FString BackupDir = CodeToolsCombinePath(CodeToolsBackupsRoot(), EditId);
				bool bPretendExists = false;
#if WITH_DEV_AUTOMATION_TESTS
				if (GCodeToolsApplyTestHooks.ShouldPretendBackupDirectoryExists)
				{
					bPretendExists = GCodeToolsApplyTestHooks.ShouldPretendBackupDirectoryExists(EditId);
				}
#endif
				if (bPretendExists || FPaths::DirectoryExists(BackupDir))
				{
					continue;
				}
				if (!IFileManager::Get().MakeDirectory(*BackupDir, true))
				{
					OutFailureCode = TEXT("editIdCollisionExhausted");
					OutFailureReason = FString::Printf(TEXT("Failed to create backup directory: %s"), *BackupDir);
					return false;
				}
				OutEditId = EditId;
				OutBackupDir = BackupDir;
				return true;
			}
			OutFailureCode = TEXT("editIdCollisionExhausted");
			OutFailureReason = TEXT("Failed to allocate a unique code editId after bounded retries.");
			return false;
		}

		bool CodeToolsWriteAndVerifyBackups(TArray<FCodeToolsEditPlan>& Edits, const FString& BackupDir, FString& OutFailureReason)
		{
			for (int32 Index = 0; Index < Edits.Num(); ++Index)
			{
				FCodeToolsEditPlan& Edit = Edits[Index];
				if (!Edit.bOriginalExisted)
				{
					Edit.BackupPath.Reset();
					continue;
				}
				Edit.BackupPath = CodeToolsCombinePath(
					BackupDir,
					FString::Printf(TEXT("%03d_%s.before"), Index, *CodeToolsSanitizeArtifactName(Edit.ProjectRelativePath)));
				if (!FFileHelper::SaveArrayToFile(Edit.BeforeBytes, *Edit.BackupPath))
				{
					OutFailureReason = FString::Printf(TEXT("Failed to write backup for %s."), *Edit.ProjectRelativePath);
					return false;
				}
				TArray<uint8> BackupBytes;
				FString BackupSha;
				if (!CodeToolsReadFileBytes(Edit.BackupPath, BackupBytes, BackupSha) || BackupSha != Edit.ShaBefore)
				{
					OutFailureReason = FString::Printf(TEXT("Backup verification failed for %s."), *Edit.ProjectRelativePath);
					return false;
				}
			}
			return true;
		}

		bool CodeToolsRestoreEditFromBackup(const FCodeToolsEditPlan& Edit, bool bForce, FString& OutFailureReason)
		{
			if (!Edit.bOriginalExisted)
			{
				if (!FPaths::FileExists(Edit.Path))
				{
					return true;
				}
				TArray<uint8> CurrentBytes;
				FString CurrentSha;
				if (!CodeToolsReadFileBytes(Edit.Path, CurrentBytes, CurrentSha))
				{
					OutFailureReason = FString::Printf(TEXT("Failed to read created file before delete: %s"), *Edit.ProjectRelativePath);
					return false;
				}
				if (!bForce && CurrentSha != Edit.ShaAfter)
				{
					OutFailureReason = FString::Printf(TEXT("Created file drifted before delete: %s"), *Edit.ProjectRelativePath);
					return false;
				}
				if (!IFileManager::Get().Delete(*Edit.Path, false, true))
				{
					OutFailureReason = FString::Printf(TEXT("Failed to delete created file: %s"), *Edit.ProjectRelativePath);
					return false;
				}
				return true;
			}

			TArray<uint8> BackupBytes;
			FString BackupSha;
			if (Edit.BackupPath.IsEmpty() || !CodeToolsReadFileBytes(Edit.BackupPath, BackupBytes, BackupSha) || BackupSha != Edit.ShaBefore)
			{
				OutFailureReason = FString::Printf(TEXT("Backup is missing or does not match shaBefore for %s."), *Edit.ProjectRelativePath);
				return false;
			}
			if (!CodeToolsWriteBytesAtomic(Edit.Path, BackupBytes, OutFailureReason))
			{
				return false;
			}
			TArray<uint8> RestoredBytes;
			FString RestoredSha;
			if (!CodeToolsReadFileBytes(Edit.Path, RestoredBytes, RestoredSha) || RestoredSha != Edit.ShaBefore)
			{
				OutFailureReason = FString::Printf(TEXT("Restored sha does not match shaBefore for %s."), *Edit.ProjectRelativePath);
				return false;
			}
			return true;
		}

		bool CodeToolsRollbackWrittenEdits(const TArray<FCodeToolsEditPlan>& WrittenEdits, FString& OutFailureReason)
		{
			for (int32 Index = WrittenEdits.Num() - 1; Index >= 0; --Index)
			{
				if (!CodeToolsRestoreEditFromBackup(WrittenEdits[Index], false, OutFailureReason))
				{
					return false;
				}
			}
			return true;
		}

		TSharedPtr<FJsonObject> CodeToolsMakeApplyResponse(
			const FCodeToolsPreviewPlan& Plan,
			const FString& EditId,
			const FString& ManifestPath,
			const FString& BackupDirectory,
			bool bDryRun,
			int32 AppliedChangeCount)
		{
			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("editId"), EditId);
			if (ManifestPath.IsEmpty())
			{
				StructuredContent->SetField(TEXT("manifestPath"), MakeShared<FJsonValueNull>());
			}
			else
			{
				StructuredContent->SetStringField(TEXT("manifestPath"), ManifestPath);
			}
			if (BackupDirectory.IsEmpty())
			{
				StructuredContent->SetField(TEXT("backupDirectory"), MakeShared<FJsonValueNull>());
			}
			else
			{
				StructuredContent->SetStringField(TEXT("backupDirectory"), BackupDirectory);
			}
			StructuredContent->SetArrayField(TEXT("touchedFiles"), CodeToolsMakeTouchedFileArray(Plan.Edits, false));
			StructuredContent->SetBoolField(TEXT("buildRecommended"), Plan.bRequiresBuild);
			StructuredContent->SetBoolField(TEXT("restartRecommended"), Plan.bRequiresRestart);
			StructuredContent->SetBoolField(TEXT("dryRun"), bDryRun);
			StructuredContent->SetNumberField(TEXT("appliedChangeCount"), AppliedChangeCount);
			return StructuredContent;
		}

		FUnrealMcpExecutionResult CodeToolsApplyChange(const FJsonObject& Arguments)
		{
			FString PreviewId;
			Arguments.TryGetStringField(TEXT("previewId"), PreviewId);
			PreviewId = PreviewId.TrimStartAndEnd();
			if (PreviewId.IsEmpty())
			{
				return CodeToolsMakeErrorResult(TEXT("missingMatch"), TEXT("previewId is required."));
			}

			bool bDryRun = true;
			bool bConfirmHighRisk = false;
			Arguments.TryGetBoolField(TEXT("dryRun"), bDryRun);
			Arguments.TryGetBoolField(TEXT("confirmHighRisk"), bConfirmHighRisk);

			FCodeToolsPreviewPlan Plan;
			FString FailureCode;
			FString FailureReason;
			if (!CodeToolsLoadPreviewPlan(PreviewId, Plan, FailureCode, FailureReason))
			{
				return CodeToolsMakeErrorResult(FailureCode, FailureReason);
			}
			if (Plan.bRequiresApproval && !bConfirmHighRisk)
			{
				return CodeToolsMakeErrorResult(TEXT("highRiskConfirmationRequired"), TEXT("confirmHighRisk=true is required for high-risk code paths."));
			}

			if (bDryRun)
			{
				TSharedPtr<FJsonObject> StructuredContent = CodeToolsMakeApplyResponse(Plan, FString(), FString(), FString(), true, 0);
				TSharedPtr<FJsonObject> LockAvailability = MakeShared<FJsonObject>();
				LockAvailability->SetBoolField(TEXT("currentlyHeld"), FPaths::FileExists(GetMcpExtensionLockPath()));
				LockAvailability->SetStringField(TEXT("note"), TEXT("Dry-run does not acquire the lock; this availability check is informational and cannot guarantee later availability."));
				StructuredContent->SetObjectField(TEXT("lockAvailability"), LockAvailability);
				return MakeExecutionResult(
					FString::Printf(TEXT("Dry run: would apply %d code edit(s)."), Plan.Edits.Num()),
					StructuredContent,
					false);
			}

			FScopedCodeChangeLock ScopedLock(TEXT("unreal.code_apply_change"));
			if (!ScopedLock.IsAcquired())
			{
				return MakeExecutionResult(
					ScopedLock.GetFailureReason(),
					ScopedLock.MakeStructuredContent(TEXT("code_change_lock_failed")),
					true);
			}

			FString EditId;
			FString BackupDir;
			if (!CodeToolsTryCreateBackupDirectory(EditId, BackupDir, FailureCode, FailureReason))
			{
				return CodeToolsMakeErrorResult(FailureCode, FailureReason);
			}

			FString ManifestPath = CodeToolsCombinePath(BackupDir, TEXT("Manifest.json"));
			for (int32 Index = 0; Index < Plan.Edits.Num(); ++Index)
			{
				FCodeToolsEditPlan& Edit = Plan.Edits[Index];
				Edit.BackupPath = Edit.bOriginalExisted
					? CodeToolsCombinePath(BackupDir, FString::Printf(TEXT("%03d_%s.before"), Index, *CodeToolsSanitizeArtifactName(Edit.ProjectRelativePath)))
					: FString();
			}

			if (!CodeToolsWriteApplyManifestState(ManifestPath, TEXT("started"), EditId, PreviewId, ScopedLock.GetSessionId(), Plan, FailureReason))
			{
				return CodeToolsMakeErrorResult(TEXT("transactionRolledBack"), FailureReason);
			}
			if (!CodeToolsWriteAndVerifyBackups(Plan.Edits, BackupDir, FailureReason))
			{
				return CodeToolsMakeErrorResult(TEXT("transactionRolledBack"), FailureReason);
			}
			if (!CodeToolsWriteApplyManifestState(ManifestPath, TEXT("backupComplete"), EditId, PreviewId, ScopedLock.GetSessionId(), Plan, FailureReason))
			{
				return CodeToolsMakeErrorResult(TEXT("transactionRolledBack"), FailureReason);
			}

			TArray<FCodeToolsEditPlan> WrittenEdits;
			for (const FCodeToolsEditPlan& Edit : Plan.Edits)
			{
#if WITH_DEV_AUTOMATION_TESTS
				if (GCodeToolsApplyTestHooks.BeforeWrite)
				{
					GCodeToolsApplyTestHooks.BeforeWrite(Edit.ProjectRelativePath);
				}
#endif
				if (Edit.bOriginalExisted)
				{
					TArray<uint8> CurrentBytes;
					FString CurrentSha;
					if (!CodeToolsReadFileBytes(Edit.Path, CurrentBytes, CurrentSha) || CurrentSha != Edit.ShaBefore)
					{
						FString RollbackFailure;
						const bool bRolledBack = CodeToolsRollbackWrittenEdits(WrittenEdits, RollbackFailure);
						CodeToolsWriteApplyManifestState(ManifestPath, TEXT("rolledBack"), EditId, PreviewId, ScopedLock.GetSessionId(), Plan, FailureReason);
						return CodeToolsMakeErrorResult(
							TEXT("transactionRolledBack"),
							bRolledBack
								? FString::Printf(TEXT("TOCTOU sha check failed before writing %s; already-written files were rolled back."), *Edit.ProjectRelativePath)
								: FString::Printf(TEXT("TOCTOU sha check failed before writing %s; rollback failed: %s"), *Edit.ProjectRelativePath, *RollbackFailure));
					}
				}
				else
				{
					if (FPaths::FileExists(Edit.Path))
					{
						FString RollbackFailure;
						const bool bRolledBack = CodeToolsRollbackWrittenEdits(WrittenEdits, RollbackFailure);
						CodeToolsWriteApplyManifestState(ManifestPath, TEXT("rolledBack"), EditId, PreviewId, ScopedLock.GetSessionId(), Plan, FailureReason);
						return CodeToolsMakeErrorResult(
							TEXT("transactionRolledBack"),
							bRolledBack
								? FString::Printf(TEXT("create_file target appeared before writing %s; already-written files were rolled back."), *Edit.ProjectRelativePath)
								: FString::Printf(TEXT("create_file target appeared before writing %s; rollback failed: %s"), *Edit.ProjectRelativePath, *RollbackFailure));
					}
					const FString ParentDir = FPaths::GetPath(Edit.Path);
					if (!IFileManager::Get().MakeDirectory(*ParentDir, true))
					{
						FString RollbackFailure;
						const bool bRolledBack = CodeToolsRollbackWrittenEdits(WrittenEdits, RollbackFailure);
						CodeToolsWriteApplyManifestState(ManifestPath, TEXT("rolledBack"), EditId, PreviewId, ScopedLock.GetSessionId(), Plan, FailureReason);
						return CodeToolsMakeErrorResult(
							TEXT("transactionRolledBack"),
							bRolledBack
								? FString::Printf(TEXT("Failed to create parent directory for %s; already-written files were rolled back."), *Edit.ProjectRelativePath)
								: FString::Printf(TEXT("Failed to create parent directory for %s; rollback failed: %s"), *Edit.ProjectRelativePath, *RollbackFailure));
					}
				}

				if (!CodeToolsWriteBytesAtomic(Edit.Path, Edit.AfterBytes, FailureReason))
				{
					FString RollbackFailure;
					TArray<FCodeToolsEditPlan> RollbackTargets = WrittenEdits;
					if (FPaths::FileExists(Edit.Path))
					{
						RollbackTargets.Add(Edit);
					}
					const bool bRolledBack = CodeToolsRollbackWrittenEdits(RollbackTargets, RollbackFailure);
					CodeToolsWriteApplyManifestState(ManifestPath, TEXT("rolledBack"), EditId, PreviewId, ScopedLock.GetSessionId(), Plan, FailureReason);
					return CodeToolsMakeErrorResult(
						TEXT("transactionRolledBack"),
						bRolledBack
							? FString::Printf(TEXT("Write failed for %s; transaction was rolled back."), *Edit.ProjectRelativePath)
							: FString::Printf(TEXT("Write failed for %s; rollback failed: %s"), *Edit.ProjectRelativePath, *RollbackFailure));
				}
				WrittenEdits.Add(Edit);
			}

			if (!CodeToolsWriteApplyManifestState(ManifestPath, TEXT("writeComplete"), EditId, PreviewId, ScopedLock.GetSessionId(), Plan, FailureReason))
			{
				FString RollbackFailure;
				CodeToolsRollbackWrittenEdits(WrittenEdits, RollbackFailure);
				return CodeToolsMakeErrorResult(TEXT("transactionRolledBack"), FailureReason);
			}

			for (const FCodeToolsEditPlan& Edit : Plan.Edits)
			{
				TArray<uint8> AfterBytes;
				FString AfterSha;
				if (!CodeToolsReadFileBytes(Edit.Path, AfterBytes, AfterSha) || AfterSha != Edit.ShaAfter)
				{
					FString RollbackFailure;
					const bool bRolledBack = CodeToolsRollbackWrittenEdits(WrittenEdits, RollbackFailure);
					CodeToolsWriteApplyManifestState(ManifestPath, TEXT("rolledBack"), EditId, PreviewId, ScopedLock.GetSessionId(), Plan, FailureReason);
					return CodeToolsMakeErrorResult(
						TEXT("transactionRolledBack"),
						bRolledBack
							? FString::Printf(TEXT("Post-write sha verification failed for %s; transaction was rolled back."), *Edit.ProjectRelativePath)
							: FString::Printf(TEXT("Post-write sha verification failed for %s; rollback failed: %s"), *Edit.ProjectRelativePath, *RollbackFailure));
				}
			}

			if (!CodeToolsWriteApplyManifestState(ManifestPath, TEXT("verified"), EditId, PreviewId, ScopedLock.GetSessionId(), Plan, FailureReason))
			{
				return CodeToolsMakeErrorResult(TEXT("transactionRolledBack"), FailureReason);
			}
			TSharedPtr<FJsonObject> VerifiedManifest = CodeToolsMakeManifestObject(
				TEXT("code_apply_change"),
				EditId,
				PreviewId,
				TEXT("verified"),
				ScopedLock.GetSessionId(),
				Plan.Edits,
				Plan.RiskLevel,
				false,
				Plan.bRequiresBuild,
				Plan.bRequiresRestart,
				false);
			if (!CodeToolsWriteJsonObjectAtomic(VerifiedManifest, CodeToolsLastChangePath(), FailureReason))
			{
				return CodeToolsMakeErrorResult(TEXT("transactionRolledBack"), FailureReason);
			}

			return MakeExecutionResult(
				FString::Printf(TEXT("Applied %d code edit(s)."), Plan.Edits.Num()),
				CodeToolsMakeApplyResponse(Plan, EditId, ManifestPath, BackupDir, false, Plan.Edits.Num()),
				false);
		}

		bool CodeToolsResolveRollbackManifestPath(const FJsonObject& Arguments, FString& OutManifestPath, FString& OutFailureReason)
		{
			FString ManifestPath;
			FString EditId;
			Arguments.TryGetStringField(TEXT("manifestPath"), ManifestPath);
			Arguments.TryGetStringField(TEXT("editId"), EditId);
			ManifestPath = ManifestPath.TrimStartAndEnd();
			EditId = EditId.TrimStartAndEnd();
			if (!ManifestPath.IsEmpty())
			{
				const FString ProjectRoot = CodeToolsNormalizePath(FPaths::ProjectDir());
				OutManifestPath = FPaths::IsRelative(ManifestPath)
					? CodeToolsCombinePath(ProjectRoot, ManifestPath)
					: CodeToolsNormalizePath(ManifestPath);
				if (!CodeToolsPathEqualsOrChild(OutManifestPath, CodeToolsChangesRoot()))
				{
					OutFailureReason = TEXT("manifestPath must resolve under Saved/UnrealMcp/CodeChanges.");
					return false;
				}
				return true;
			}
			if (!EditId.IsEmpty())
			{
				if (!CodeToolsIsSafeArtifactId(EditId))
				{
					OutFailureReason = TEXT("editId is invalid.");
					return false;
				}
				OutManifestPath = CodeToolsCombinePath(CodeToolsBackupsRoot(), FPaths::Combine(EditId, TEXT("Manifest.json")));
				return true;
			}
			OutFailureReason = TEXT("Provide editId or manifestPath.");
			return false;
		}

		bool CodeToolsReadManifestEdits(const TSharedPtr<FJsonObject>& Manifest, TArray<FCodeToolsEditPlan>& OutEdits, FString& OutFailureReason)
		{
			OutEdits.Reset();
			const TArray<TSharedPtr<FJsonValue>>* TouchedFiles = nullptr;
			if (!Manifest.IsValid() || !Manifest->TryGetArrayField(TEXT("touchedFiles"), TouchedFiles) || !TouchedFiles)
			{
				OutFailureReason = TEXT("Manifest is missing touchedFiles.");
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Value : *TouchedFiles)
			{
				const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
				if (!Object.IsValid())
				{
					OutFailureReason = TEXT("Manifest touchedFiles contains a non-object entry.");
					return false;
				}
				FCodeToolsEditPlan Edit;
				Object->TryGetStringField(TEXT("sourcePath"), Edit.Path);
				Object->TryGetStringField(TEXT("projectRelativePath"), Edit.ProjectRelativePath);
				Object->TryGetStringField(TEXT("backupPath"), Edit.BackupPath);
				Object->TryGetStringField(TEXT("shaBefore"), Edit.ShaBefore);
				Object->TryGetStringField(TEXT("shaAfter"), Edit.ShaAfter);
				Object->TryGetStringField(TEXT("operation"), Edit.Operation);
				Object->TryGetBoolField(TEXT("originalExisted"), Edit.bOriginalExisted);
				if (Edit.Path.IsEmpty() || Edit.ProjectRelativePath.IsEmpty())
				{
					OutFailureReason = TEXT("Manifest touched file is missing sourcePath or projectRelativePath.");
					return false;
				}
				OutEdits.Add(Edit);
			}
			return true;
		}

		bool CodeToolsBuildRollbackPlan(const FJsonObject& Arguments, FCodeToolsRollbackPlan& OutPlan, bool bForce, FString& OutFailureCode, FString& OutFailureReason)
		{
			OutPlan = FCodeToolsRollbackPlan();
			if (!CodeToolsResolveRollbackManifestPath(Arguments, OutPlan.ManifestPath, OutFailureReason))
			{
				OutFailureCode = TEXT("missingMatch");
				return false;
			}
			if (!LoadJsonObjectFromFile(OutPlan.ManifestPath, OutPlan.ManifestObject, OutFailureReason) || !OutPlan.ManifestObject.IsValid())
			{
				OutFailureCode = TEXT("missingMatch");
				OutFailureReason = FString::Printf(TEXT("Manifest not found or unreadable: %s"), *OutPlan.ManifestPath);
				return false;
			}
			OutPlan.ManifestObject->TryGetStringField(TEXT("editId"), OutPlan.EditId);
			if (OutPlan.EditId.IsEmpty())
			{
				OutFailureCode = TEXT("missingMatch");
				OutFailureReason = TEXT("Manifest is missing editId.");
				return false;
			}
			if (!CodeToolsReadManifestEdits(OutPlan.ManifestObject, OutPlan.Edits, OutFailureReason))
			{
				OutFailureCode = TEXT("missingMatch");
				return false;
			}

			FString ApplyState;
			OutPlan.ManifestObject->TryGetStringField(TEXT("applyState"), ApplyState);
			for (const FCodeToolsEditPlan& Edit : OutPlan.Edits)
			{
				const bool bPreWriteState = ApplyState.Equals(TEXT("started"), ESearchCase::IgnoreCase)
					|| ApplyState.Equals(TEXT("backupComplete"), ESearchCase::IgnoreCase);
				if (Edit.bOriginalExisted)
				{
					TArray<uint8> CurrentBytes;
					FString CurrentSha;
					if (!CodeToolsReadFileBytes(Edit.Path, CurrentBytes, CurrentSha))
					{
						OutPlan.bDriftDetected = true;
						OutPlan.DriftFiles.Add(Edit.ProjectRelativePath);
						continue;
					}
					const bool bAcceptable = bPreWriteState
						? (CurrentSha == Edit.ShaBefore || CurrentSha == Edit.ShaAfter)
						: (CurrentSha == Edit.ShaAfter);
					if (!bAcceptable)
					{
						OutPlan.bDriftDetected = true;
						OutPlan.DriftFiles.Add(Edit.ProjectRelativePath);
					}
				}
				else
				{
					if (!FPaths::FileExists(Edit.Path))
					{
						if (!bPreWriteState)
						{
							OutPlan.bDriftDetected = true;
							OutPlan.DriftFiles.Add(Edit.ProjectRelativePath);
						}
						continue;
					}
					TArray<uint8> CurrentBytes;
					FString CurrentSha;
					if (!CodeToolsReadFileBytes(Edit.Path, CurrentBytes, CurrentSha) || CurrentSha != Edit.ShaAfter)
					{
						OutPlan.bDriftDetected = true;
						OutPlan.DriftFiles.Add(Edit.ProjectRelativePath);
					}
				}
			}
			if (OutPlan.bDriftDetected && !bForce)
			{
				OutFailureCode = TEXT("driftDetected");
				OutFailureReason = TEXT("Rollback target drifted from the applied sha; pass force=true to restore over drift.");
				return false;
			}
			OutPlan.RollbackId = FString::Printf(
				TEXT("rollback-%s-%s"),
				*FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")),
				*FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
			return true;
		}

		TSharedPtr<FJsonObject> CodeToolsMakeRollbackResponse(
			const FCodeToolsRollbackPlan& Plan,
			const FString& RollbackManifestPath,
			bool bDryRun,
			bool bForcedOverDrift,
			const TArray<TSharedPtr<FJsonValue>>& RestoredFiles)
		{
			TArray<TSharedPtr<FJsonValue>> DriftValues;
			for (const FString& DriftFile : Plan.DriftFiles)
			{
				DriftValues.Add(MakeShared<FJsonValueString>(DriftFile));
			}
			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("rollbackId"), Plan.RollbackId);
			StructuredContent->SetArrayField(TEXT("restoredFiles"), RestoredFiles);
			if (RollbackManifestPath.IsEmpty())
			{
				StructuredContent->SetField(TEXT("rollbackManifestPath"), MakeShared<FJsonValueNull>());
			}
			else
			{
				StructuredContent->SetStringField(TEXT("rollbackManifestPath"), RollbackManifestPath);
			}
			StructuredContent->SetBoolField(TEXT("driftDetected"), Plan.bDriftDetected);
			StructuredContent->SetArrayField(TEXT("driftFiles"), DriftValues);
			StructuredContent->SetBoolField(TEXT("dryRun"), bDryRun);
			if (bForcedOverDrift)
			{
				StructuredContent->SetBoolField(TEXT("forcedOverDrift"), true);
			}
			return StructuredContent;
		}

		FUnrealMcpExecutionResult CodeToolsRollbackChange(const FJsonObject& Arguments)
		{
			bool bDryRun = true;
			bool bForce = false;
			Arguments.TryGetBoolField(TEXT("dryRun"), bDryRun);
			Arguments.TryGetBoolField(TEXT("force"), bForce);

			FCodeToolsRollbackPlan Plan;
			FString FailureCode;
			FString FailureReason;
			if (!CodeToolsBuildRollbackPlan(Arguments, Plan, bForce, FailureCode, FailureReason))
			{
				return CodeToolsMakeErrorResult(FailureCode, FailureReason);
			}

			TArray<TSharedPtr<FJsonValue>> RestoredFiles;
			for (const FCodeToolsEditPlan& Edit : Plan.Edits)
			{
				TSharedPtr<FJsonObject> FileObject = MakeShared<FJsonObject>();
				FileObject->SetStringField(TEXT("path"), Edit.ProjectRelativePath);
				FileObject->SetStringField(TEXT("shaBefore"), Edit.ShaAfter);
				FileObject->SetStringField(TEXT("shaAfter"), Edit.ShaBefore);
				FileObject->SetStringField(TEXT("operation"), Edit.Operation);
				FileObject->SetBoolField(TEXT("originalExisted"), Edit.bOriginalExisted);
				RestoredFiles.Add(MakeShared<FJsonValueObject>(FileObject));
			}

			if (bDryRun)
			{
				return MakeExecutionResult(
					FString::Printf(TEXT("Dry run: would roll back %d code file(s)."), Plan.Edits.Num()),
					CodeToolsMakeRollbackResponse(Plan, FString(), true, false, RestoredFiles),
					false);
			}

			FScopedCodeChangeLock ScopedLock(TEXT("unreal.code_rollback_change"));
			if (!ScopedLock.IsAcquired())
			{
				return MakeExecutionResult(
					ScopedLock.GetFailureReason(),
					ScopedLock.MakeStructuredContent(TEXT("code_change_lock_failed")),
					true);
			}
			if (!CodeToolsBuildRollbackPlan(Arguments, Plan, bForce, FailureCode, FailureReason))
			{
				return CodeToolsMakeErrorResult(FailureCode, FailureReason);
			}

			for (const FCodeToolsEditPlan& Edit : Plan.Edits)
			{
				if (!CodeToolsRestoreEditFromBackup(Edit, bForce, FailureReason))
				{
					return CodeToolsMakeErrorResult(TEXT("transactionRolledBack"), FailureReason);
				}
			}

			TSharedPtr<FJsonObject> OriginalManifest = Plan.ManifestObject;
			FString PreviewId;
			FString PathRisk = TEXT("low");
			bool bRequiresBuild = false;
			bool bRequiresRestart = false;
			if (OriginalManifest.IsValid())
			{
				OriginalManifest->TryGetStringField(TEXT("previewId"), PreviewId);
				OriginalManifest->TryGetStringField(TEXT("pathRisk"), PathRisk);
				OriginalManifest->TryGetBoolField(TEXT("requiresBuild"), bRequiresBuild);
				OriginalManifest->TryGetBoolField(TEXT("requiresRestart"), bRequiresRestart);
			}

			const FString RollbackManifestPath = CodeToolsCombinePath(CodeToolsRollbacksRoot(), Plan.RollbackId + TEXT(".json"));
			TSharedPtr<FJsonObject> RollbackManifest = CodeToolsMakeManifestObject(
				TEXT("code_rollback_change"),
				Plan.EditId,
				PreviewId,
				TEXT("verified"),
				ScopedLock.GetSessionId(),
				Plan.Edits,
				PathRisk,
				false,
				bRequiresBuild,
				bRequiresRestart,
				bForce && Plan.bDriftDetected);
			RollbackManifest->SetStringField(TEXT("rollbackId"), Plan.RollbackId);
			RollbackManifest->SetStringField(TEXT("rolledBackManifestPath"), Plan.ManifestPath);
			if (!CodeToolsWriteJsonObjectAtomic(RollbackManifest, RollbackManifestPath, FailureReason))
			{
				return CodeToolsMakeErrorResult(TEXT("transactionRolledBack"), FailureReason);
			}
			if (!CodeToolsWriteJsonObjectAtomic(RollbackManifest, CodeToolsLastChangePath(), FailureReason))
			{
				return CodeToolsMakeErrorResult(TEXT("transactionRolledBack"), FailureReason);
			}

			return MakeExecutionResult(
				FString::Printf(TEXT("Rolled back %d code file(s)."), Plan.Edits.Num()),
				CodeToolsMakeRollbackResponse(Plan, RollbackManifestPath, false, bForce && Plan.bDriftDetected, RestoredFiles),
				false);
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

#if WITH_DEV_AUTOMATION_TESTS
	void SetCodeToolsApplyTestHooks(const FCodeToolsApplyTestHooks& Hooks)
	{
		GCodeToolsApplyTestHooks = Hooks;
	}

	void ClearCodeToolsApplyTestHooks()
	{
		GCodeToolsApplyTestHooks = FCodeToolsApplyTestHooks();
	}
#endif

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
			OutResult = CodeToolsPreviewChange(Arguments);
			return true;
		}
		if (ToolName == TEXT("unreal.code_apply_change"))
		{
			OutResult = CodeToolsApplyChange(Arguments);
			return true;
		}
		if (ToolName == TEXT("unreal.code_rollback_change"))
		{
			OutResult = CodeToolsRollbackChange(Arguments);
			return true;
		}
		return false;
	}
}
