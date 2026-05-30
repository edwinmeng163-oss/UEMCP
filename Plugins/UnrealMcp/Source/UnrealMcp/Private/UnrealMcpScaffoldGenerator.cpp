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
#include "UnrealMcpHashUtils.h"

namespace UnrealMcp::Scaffold
{
	namespace
	{
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
		Result.PythonHandlerSha256 = UnrealMcp::HashUtils::Sha256LowerHex(ScaffoldGeneratorUtf8Bytes(MainPyContent));

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
