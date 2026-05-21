// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.

#include "UnrealMcpScaffoldGenerator.h"

#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealMcp::Scaffold
{
	namespace
	{
		uint32 ScaffoldGeneratorSha256RotateRight(uint32 Value, uint32 Shift)
		{
			return (Value >> Shift) | (Value << (32 - Shift));
		}

		FString ScaffoldGeneratorSha256Bytes(const TArray<uint8>& Bytes)
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
					const uint32 S0 = ScaffoldGeneratorSha256RotateRight(Words[Index - 15], 7) ^ ScaffoldGeneratorSha256RotateRight(Words[Index - 15], 18) ^ (Words[Index - 15] >> 3);
					const uint32 S1 = ScaffoldGeneratorSha256RotateRight(Words[Index - 2], 17) ^ ScaffoldGeneratorSha256RotateRight(Words[Index - 2], 19) ^ (Words[Index - 2] >> 10);
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
					const uint32 S1 = ScaffoldGeneratorSha256RotateRight(E, 6) ^ ScaffoldGeneratorSha256RotateRight(E, 11) ^ ScaffoldGeneratorSha256RotateRight(E, 25);
					const uint32 Choice = (E & F) ^ ((~E) & G);
					const uint32 Temp1 = H + S1 + Choice + Constants[Index] + Words[Index];
					const uint32 S0 = ScaffoldGeneratorSha256RotateRight(A, 2) ^ ScaffoldGeneratorSha256RotateRight(A, 13) ^ ScaffoldGeneratorSha256RotateRight(A, 22);
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

		TArray<uint8> ScaffoldGeneratorUtf8Bytes(const FString& Text)
		{
			TArray<uint8> Bytes;
			FTCHARToUTF8 Converter(*Text);
			Bytes.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
			return Bytes;
		}

		bool ScaffoldGeneratorIsValidToolId(const FString& ToolId, FString& OutReason)
		{
			if (ToolId.IsEmpty())
			{
				OutReason = TEXT("ToolId is required.");
				return false;
			}
			if (ToolId.Len() > 64)
			{
				OutReason = TEXT("ToolId must be 64 characters or fewer.");
				return false;
			}
			if (ToolId.Contains(TEXT("/"), ESearchCase::CaseSensitive)
				|| ToolId.Contains(TEXT("\\"), ESearchCase::CaseSensitive)
				|| ToolId.Contains(TEXT(".."), ESearchCase::CaseSensitive)
				|| ToolId.Contains(TEXT(":"), ESearchCase::CaseSensitive)
				|| ToolId.StartsWith(TEXT("//"), ESearchCase::CaseSensitive)
				|| ToolId.StartsWith(TEXT("\\\\"), ESearchCase::CaseSensitive)
				|| FPaths::IsRelative(ToolId) == false)
			{
				OutReason = TEXT("ToolId must be a safe single directory name without slashes, traversal, drives, or UNC paths.");
				return false;
			}
			for (const TCHAR Character : ToolId)
			{
				const bool bLowerAlpha = Character >= TEXT('a') && Character <= TEXT('z');
				const bool bUpperAlpha = Character >= TEXT('A') && Character <= TEXT('Z');
				const bool bDigit = Character >= TEXT('0') && Character <= TEXT('9');
				if (!bLowerAlpha && !bUpperAlpha && !bDigit && Character != TEXT('_'))
				{
					OutReason = TEXT("ToolId may contain only alphanumeric characters and underscores.");
					return false;
				}
			}
			return true;
		}

		FString ScaffoldGeneratorNormalizeFullPath(const FString& Path)
		{
			FString Normalized = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(Normalized);
			FPaths::CollapseRelativeDirectories(Normalized);
			Normalized.RemoveFromEnd(TEXT("/"));
			return Normalized;
		}

		bool ScaffoldGeneratorLoadTemplate(const FString& TemplateDir, const FString& FileName, FString& OutText, FString& OutError)
		{
			const FString TemplatePath = FPaths::Combine(TemplateDir, FileName);
			if (!FFileHelper::LoadFileToString(OutText, *TemplatePath))
			{
				OutError = FString::Printf(TEXT("Failed to read scaffold template '%s'."), *TemplatePath);
				return false;
			}
			return true;
		}

		FString ScaffoldGeneratorJsonEscapedStringContent(const FString& Value)
		{
			FString Result;
			Result.Reserve(Value.Len());
			for (const TCHAR Character : Value)
			{
				switch (Character)
				{
				case TEXT('\\'): Result.Append(TEXT("\\\\")); break;
				case TEXT('"'): Result.Append(TEXT("\\\"")); break;
				case TEXT('\b'): Result.Append(TEXT("\\b")); break;
				case TEXT('\f'): Result.Append(TEXT("\\f")); break;
				case TEXT('\n'): Result.Append(TEXT("\\n")); break;
				case TEXT('\r'): Result.Append(TEXT("\\r")); break;
				case TEXT('\t'): Result.Append(TEXT("\\t")); break;
				default:
					if (Character < 0x20)
					{
						Result.Appendf(TEXT("\\u%04x"), static_cast<uint32>(Character));
					}
					else
					{
						Result.AppendChar(Character);
					}
					break;
				}
			}
			return Result;
		}

		FString ScaffoldGeneratorApplyTemplate(
			FString TemplateText,
			const FString& ToolId,
			const FString& Description,
			const FString& Sha256,
			bool bDescriptionIsJsonStringContent)
		{
			const FString DescriptionReplacement = bDescriptionIsJsonStringContent
				? ScaffoldGeneratorJsonEscapedStringContent(Description)
				: Description;
			TemplateText.ReplaceInline(TEXT("{{TOOL_ID}}"), *ToolId, ESearchCase::CaseSensitive);
			TemplateText.ReplaceInline(TEXT("{{DESCRIPTION}}"), *DescriptionReplacement, ESearchCase::CaseSensitive);
			TemplateText.ReplaceInline(TEXT("{{SHA256}}"), *Sha256, ESearchCase::CaseSensitive);
			return TemplateText;
		}

		bool ScaffoldGeneratorSaveStringFile(const FString& Path, const FString& Content, FString& OutError)
		{
			if (!FFileHelper::SaveStringToFile(Content, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutError = FString::Printf(TEXT("Failed to write scaffold file '%s'."), *Path);
				return false;
			}
			return true;
		}

		bool ScaffoldGeneratorParseJsonObject(const FString& JsonText, TSharedPtr<FJsonObject>& OutObject, FString& OutError)
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
			if (!FJsonSerializer::Deserialize(Reader, OutObject) || !OutObject.IsValid())
			{
				OutError = TEXT("Generated tool.json template did not parse as JSON.");
				return false;
			}
			return true;
		}

		bool ScaffoldGeneratorJsonObjectToString(const TSharedPtr<FJsonObject>& Object, FString& OutText, FString& OutError)
		{
			const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutText);
			if (!Object.IsValid() || !FJsonSerializer::Serialize(Object.ToSharedRef(), Writer))
			{
				OutError = TEXT("Failed to serialize generated tool.json.");
				return false;
			}
			return true;
		}

		TArray<TSharedPtr<FJsonValue>> ScaffoldGeneratorReadStringArrayField(const FJsonObject& Arguments, const FString& FieldName)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			const TArray<TSharedPtr<FJsonValue>>* InputValues = nullptr;
			if (!Arguments.TryGetArrayField(FieldName, InputValues) || InputValues == nullptr)
			{
				return Values;
			}
			for (const TSharedPtr<FJsonValue>& InputValue : *InputValues)
			{
				FString StringValue;
				if (InputValue.IsValid() && InputValue->TryGetString(StringValue) && !StringValue.TrimStartAndEnd().IsEmpty())
				{
					Values.Add(MakeShared<FJsonValueString>(StringValue.TrimStartAndEnd()));
				}
			}
			return Values;
		}

		void ScaffoldGeneratorApplyOptionalStringField(const FJsonObject& Arguments, const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
		{
			FString Value;
			if (Object.IsValid() && Arguments.TryGetStringField(FieldName, Value) && !Value.TrimStartAndEnd().IsEmpty())
			{
				Object->SetStringField(FieldName, Value.TrimStartAndEnd());
			}
		}

		void ScaffoldGeneratorApplyOptionalBoolField(const FJsonObject& Arguments, const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
		{
			bool bValue = false;
			if (Object.IsValid() && Arguments.TryGetBoolField(FieldName, bValue))
			{
				Object->SetBoolField(FieldName, bValue);
			}
		}

		void ScaffoldGeneratorApplyToolJsonOverrides(const FJsonObject& Arguments, const TSharedPtr<FJsonObject>& ToolJson)
		{
			ScaffoldGeneratorApplyOptionalStringField(Arguments, ToolJson, TEXT("title"));
			ScaffoldGeneratorApplyOptionalStringField(Arguments, ToolJson, TEXT("description"));
			ScaffoldGeneratorApplyOptionalStringField(Arguments, ToolJson, TEXT("category"));
			ScaffoldGeneratorApplyOptionalStringField(Arguments, ToolJson, TEXT("exposure"));
			ScaffoldGeneratorApplyOptionalStringField(Arguments, ToolJson, TEXT("riskLevel"));
			ScaffoldGeneratorApplyOptionalStringField(Arguments, ToolJson, TEXT("owner"));
			ScaffoldGeneratorApplyOptionalBoolField(Arguments, ToolJson, TEXT("requiresWrite"));
			ScaffoldGeneratorApplyOptionalBoolField(Arguments, ToolJson, TEXT("requiresBuild"));
			ScaffoldGeneratorApplyOptionalBoolField(Arguments, ToolJson, TEXT("requiresExternalProcess"));
			ScaffoldGeneratorApplyOptionalBoolField(Arguments, ToolJson, TEXT("requiresRestart"));
			ScaffoldGeneratorApplyOptionalBoolField(Arguments, ToolJson, TEXT("requiresProjectMemory"));
			ScaffoldGeneratorApplyOptionalBoolField(Arguments, ToolJson, TEXT("requiresLock"));
			ScaffoldGeneratorApplyOptionalBoolField(Arguments, ToolJson, TEXT("dryRunSupport"));
			ScaffoldGeneratorApplyOptionalBoolField(Arguments, ToolJson, TEXT("preflightSupport"));
			ScaffoldGeneratorApplyOptionalBoolField(Arguments, ToolJson, TEXT("postcheckSupport"));

			TArray<TSharedPtr<FJsonValue>> ImportAllowlist = ScaffoldGeneratorReadStringArrayField(Arguments, TEXT("importAllowlist"));
			if (ImportAllowlist.Num() > 0)
			{
				ToolJson->SetArrayField(TEXT("importAllowlist"), MoveTemp(ImportAllowlist));
			}

			TArray<TSharedPtr<FJsonValue>> WouldWrite = ScaffoldGeneratorReadStringArrayField(Arguments, TEXT("wouldWriteHints"));
			if (WouldWrite.Num() == 0)
			{
				WouldWrite = ScaffoldGeneratorReadStringArrayField(Arguments, TEXT("wouldWrite"));
			}
			if (WouldWrite.Num() > 0)
			{
				ToolJson->SetArrayField(TEXT("wouldWrite"), MoveTemp(WouldWrite));
			}
		}

		void ScaffoldGeneratorSetBlockedLifecycle(FPythonScaffoldResult& Result)
		{
			Result.Lifecycle.ToolName = Result.ToolName;
			Result.Lifecycle.ExtensionScope = Extension::EExtensionScope::User;
			Result.Lifecycle.ImplementationTrack = Extension::EImplementationTrack::Python;
			Result.Lifecycle.State = Extension::ELifecycleState::Blocked;
			Result.Lifecycle.bCallableNow = false;
			Result.Lifecycle.NextRequiredAction.Reset();
			Result.Lifecycle.SourceKind = Extension::ESourceKind::UserRegistry;
			Result.Lifecycle.HandlerKind = Extension::EHandlerKind::None;
			Result.Lifecycle.ScaffoldDir = Result.ScaffoldDir;
			Result.Lifecycle.RegistryPath = Result.ToolJsonPath;
			Result.Lifecycle.PythonHandlerPath = Result.MainPyPath;
		}

		void ScaffoldGeneratorSetFailure(FPythonScaffoldResult& Result, const FString& Error)
		{
			Result.bSuccess = false;
			Result.Error = Error;
			ScaffoldGeneratorSetBlockedLifecycle(Result);
		}
	}

	FPythonScaffoldResult GeneratePythonScaffoldFiles(const FString& ToolId, const FJsonObject& Arguments)
	{
		FPythonScaffoldResult Result;
		Result.ToolId = ToolId.TrimStartAndEnd();
		Result.ToolName = FString::Printf(TEXT("user.%s"), *Result.ToolId);

		FString ValidationError;
		if (!ScaffoldGeneratorIsValidToolId(Result.ToolId, ValidationError))
		{
			ScaffoldGeneratorSetFailure(Result, ValidationError);
			return Result;
		}

		Result.ScaffoldDir = ScaffoldGeneratorNormalizeFullPath(FPaths::Combine(
			FPaths::ProjectDir(),
			Extension::UserPyToolsRelativeRoot,
			Result.ToolId));
		Result.MainPyPath = FPaths::Combine(Result.ScaffoldDir, TEXT("main.py"));
		Result.ToolJsonPath = FPaths::Combine(Result.ScaffoldDir, TEXT("tool.json"));
		Result.ReadmePath = FPaths::Combine(Result.ScaffoldDir, TEXT("README.md"));
		FPaths::NormalizeFilename(Result.MainPyPath);
		FPaths::NormalizeFilename(Result.ToolJsonPath);
		FPaths::NormalizeFilename(Result.ReadmePath);

		if (IFileManager::Get().DirectoryExists(*Result.ScaffoldDir))
		{
			ScaffoldGeneratorSetFailure(Result, FString::Printf(TEXT("Python scaffold directory already exists: %s."), *Result.ScaffoldDir));
			return Result;
		}

		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealMcp"));
		if (!Plugin.IsValid())
		{
			ScaffoldGeneratorSetFailure(Result, TEXT("Unable to resolve UnrealMcp plugin base directory."));
			return Result;
		}

		const FString TemplateDir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/ScaffoldTemplates/python"));
		FString MainTemplate;
		FString ToolJsonTemplate;
		FString ReadmeTemplate;
		if (!ScaffoldGeneratorLoadTemplate(TemplateDir, TEXT("main.py.template"), MainTemplate, Result.Error)
			|| !ScaffoldGeneratorLoadTemplate(TemplateDir, TEXT("tool.json.template"), ToolJsonTemplate, Result.Error)
			|| !ScaffoldGeneratorLoadTemplate(TemplateDir, TEXT("README.md.template"), ReadmeTemplate, Result.Error))
		{
			ScaffoldGeneratorSetBlockedLifecycle(Result);
			return Result;
		}

		FString Description;
		Arguments.TryGetStringField(TEXT("description"), Description);
		Description = Description.TrimStartAndEnd();
		if (Description.IsEmpty())
		{
			Description = FString::Printf(TEXT("User Python MCP tool scaffold for user.%s."), *Result.ToolId);
		}

		const FString MainPyContent = ScaffoldGeneratorApplyTemplate(MainTemplate, Result.ToolId, Description, FString(), false);
		Result.PythonHandlerSha256 = ScaffoldGeneratorSha256Bytes(ScaffoldGeneratorUtf8Bytes(MainPyContent));

		const FString ToolJsonTemplateText = ScaffoldGeneratorApplyTemplate(ToolJsonTemplate, Result.ToolId, Description, Result.PythonHandlerSha256, true);
		TSharedPtr<FJsonObject> ToolJson;
		if (!ScaffoldGeneratorParseJsonObject(ToolJsonTemplateText, ToolJson, Result.Error))
		{
			ScaffoldGeneratorSetBlockedLifecycle(Result);
			return Result;
		}
		ScaffoldGeneratorApplyToolJsonOverrides(Arguments, ToolJson);

		FString ToolJsonContent;
		if (!ScaffoldGeneratorJsonObjectToString(ToolJson, ToolJsonContent, Result.Error))
		{
			ScaffoldGeneratorSetBlockedLifecycle(Result);
			return Result;
		}
		ToolJsonContent.AppendChar(TEXT('\n'));

		const FString ReadmeContent = ScaffoldGeneratorApplyTemplate(ReadmeTemplate, Result.ToolId, Description, Result.PythonHandlerSha256, false);

		if (!IFileManager::Get().MakeDirectory(*Result.ScaffoldDir, true))
		{
			ScaffoldGeneratorSetFailure(Result, FString::Printf(TEXT("Failed to create Python scaffold directory '%s'."), *Result.ScaffoldDir));
			return Result;
		}
		if (!ScaffoldGeneratorSaveStringFile(Result.MainPyPath, MainPyContent, Result.Error)
			|| !ScaffoldGeneratorSaveStringFile(Result.ToolJsonPath, ToolJsonContent, Result.Error)
			|| !ScaffoldGeneratorSaveStringFile(Result.ReadmePath, ReadmeContent, Result.Error))
		{
			ScaffoldGeneratorSetBlockedLifecycle(Result);
			return Result;
		}

		Result.bSuccess = true;
		Result.Lifecycle.ToolName = Result.ToolName;
		Result.Lifecycle.ExtensionScope = Extension::EExtensionScope::User;
		Result.Lifecycle.ImplementationTrack = Extension::EImplementationTrack::Python;
		Result.Lifecycle.State = Extension::ELifecycleState::DraftScaffolded;
		Result.Lifecycle.bCallableNow = false;
		Result.Lifecycle.NextRequiredAction = Extension::ControlToolUserRegistryReload;
		Result.Lifecycle.SourceKind = Extension::ESourceKind::UserRegistry;
		Result.Lifecycle.HandlerKind = Extension::EHandlerKind::PythonBridge;
		Result.Lifecycle.ScaffoldDir = Result.ScaffoldDir;
		Result.Lifecycle.RegistryPath = Result.ToolJsonPath;
		Result.Lifecycle.PythonHandlerPath = Result.MainPyPath;
		return Result;
	}

	FUnrealMcpExecutionResult BuildScaffoldExecutionResult(const FPythonScaffoldResult& Scaffold)
	{
		TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
		StructuredContent->SetStringField(TEXT("action"), TEXT("scaffold_mcp_tool"));
		StructuredContent->SetStringField(TEXT("implementationTrack"), TEXT("python"));
		StructuredContent->SetStringField(TEXT("toolId"), Scaffold.ToolId);
		StructuredContent->SetStringField(TEXT("toolName"), Scaffold.ToolName);
		StructuredContent->SetStringField(TEXT("scaffoldDir"), Scaffold.ScaffoldDir);
		StructuredContent->SetStringField(TEXT("mainPyPath"), Scaffold.MainPyPath);
		StructuredContent->SetStringField(TEXT("toolJsonPath"), Scaffold.ToolJsonPath);
		StructuredContent->SetStringField(TEXT("readmePath"), Scaffold.ReadmePath);
		StructuredContent->SetStringField(TEXT("pythonHandlerSha256"), Scaffold.PythonHandlerSha256);
		StructuredContent->SetObjectField(TEXT("lifecycle"), Extension::BuildLifecycleJson(Scaffold.Lifecycle));

		TSharedPtr<FJsonObject> Paths = MakeShared<FJsonObject>();
		Paths->SetStringField(TEXT("scaffoldDir"), Scaffold.ScaffoldDir);
		Paths->SetStringField(TEXT("registryPath"), Scaffold.ToolJsonPath);
		Paths->SetStringField(TEXT("pythonHandlerPath"), Scaffold.MainPyPath);
		Paths->SetStringField(TEXT("readmePath"), Scaffold.ReadmePath);
		StructuredContent->SetObjectField(TEXT("paths"), Paths);

		FUnrealMcpExecutionResult Result;
		Result.StructuredContent = StructuredContent;
		Result.bIsError = !Scaffold.bSuccess;
		if (Scaffold.bSuccess)
		{
			Result.Text = FString::Printf(
				TEXT("Drafted user Python tool '%s' at %s. Call unreal.mcp_user_registry_reload to make it callable."),
				*Scaffold.ToolName,
				*Scaffold.ScaffoldDir);
		}
		else
		{
			Result.Text = Scaffold.Error.IsEmpty() ? TEXT("Failed to draft user Python tool scaffold.") : Scaffold.Error;
			StructuredContent->SetStringField(TEXT("error"), Result.Text);
		}
		return Result;
	}
}
