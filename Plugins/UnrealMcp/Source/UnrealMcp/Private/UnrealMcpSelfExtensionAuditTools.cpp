#include "UnrealMcpSelfExtensionTools.h"
#include "UnrealMcpSelfExtensionInternal.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Optional.h"
#include "Misc/Paths.h"
#include "UnrealMcpExtensionLifecycle.h"
#include "UnrealMcpToolHandlerRegistry.h"
#include "UnrealMcpToolRegistry.h"
#include "UnrealMcpUserToolLock.h"
#include "UnrealMcpUserToolRegistry.h"

namespace UnrealMcp
{
		void AddAuditIssue(
			TArray<TSharedPtr<FJsonValue>>& Issues,
			const FString& Severity,
			const FString& Path,
			const FString& Message)
		{
			TSharedPtr<FJsonObject> IssueObject = MakeShared<FJsonObject>();
			IssueObject->SetStringField(TEXT("severity"), Severity);
			IssueObject->SetStringField(TEXT("path"), Path);
			IssueObject->SetStringField(TEXT("message"), Message);
			Issues.Add(MakeShared<FJsonValueObject>(IssueObject));
		}

		bool AnalyzeOpenAiSchemaObject(
			const TSharedPtr<FJsonObject>& SchemaObject,
			const FString& Path,
			TArray<TSharedPtr<FJsonValue>>& Issues);

		bool AnalyzeOpenAiSchemaValue(
			const TSharedPtr<FJsonValue>& SchemaValue,
			const FString& Path,
			TArray<TSharedPtr<FJsonValue>>& Issues)
		{
			if (!SchemaValue.IsValid())
			{
				AddAuditIssue(Issues, TEXT("warning"), Path, TEXT("Schema value is null."));
				return true;
			}

			if (SchemaValue->Type == EJson::Object)
			{
				return AnalyzeOpenAiSchemaObject(SchemaValue->AsObject(), Path, Issues);
			}

			if (SchemaValue->Type == EJson::Array)
			{
				bool bCompatible = true;
				const TArray<TSharedPtr<FJsonValue>>& Items = SchemaValue->AsArray();
				for (int32 Index = 0; Index < Items.Num(); ++Index)
				{
					bCompatible &= AnalyzeOpenAiSchemaValue(
						Items[Index],
						FString::Printf(TEXT("%s[%d]"), *Path, Index),
						Issues);
				}
				return bCompatible;
			}

			return true;
		}

		bool AnalyzeOpenAiSchemaObject(
			const TSharedPtr<FJsonObject>& SchemaObject,
			const FString& Path,
			TArray<TSharedPtr<FJsonValue>>& Issues)
		{
			if (!SchemaObject.IsValid())
			{
				AddAuditIssue(Issues, TEXT("error"), Path, TEXT("Schema object is invalid."));
				return false;
			}

			bool bCompatible = true;
			FString TypeString;
			const bool bHasStringType = SchemaObject->TryGetStringField(TEXT("type"), TypeString);
			const TSharedPtr<FJsonValue> TypeField = SchemaObject->TryGetField(TEXT("type"));
			if (TypeField.IsValid() && !bHasStringType)
			{
				AddAuditIssue(Issues, TEXT("warning"), Path + TEXT(".type"), TEXT("Non-string JSON schema type values may not be accepted by OpenAI function calling."));
			}

			if (bHasStringType && TypeString == TEXT("object"))
			{
				const TSharedPtr<FJsonValue> AdditionalProperties = SchemaObject->TryGetField(TEXT("additionalProperties"));
				if (!AdditionalProperties.IsValid())
				{
					AddAuditIssue(Issues, TEXT("warning"), Path, TEXT("Object schema does not explicitly set additionalProperties=false."));
				}
				else if (AdditionalProperties->Type == EJson::Boolean)
				{
					if (AdditionalProperties->AsBool())
					{
						AddAuditIssue(Issues, TEXT("error"), Path + TEXT(".additionalProperties"), TEXT("additionalProperties=true is not accepted by the AI function interface."));
						bCompatible = false;
					}
				}
				else
				{
					AddAuditIssue(Issues, TEXT("warning"), Path + TEXT(".additionalProperties"), TEXT("additionalProperties should be boolean false for AI-facing tools."));
				}

				const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;
				if (SchemaObject->TryGetObjectField(TEXT("properties"), PropertiesObject) && PropertiesObject && (*PropertiesObject).IsValid())
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PropertiesObject)->Values)
					{
						bCompatible &= AnalyzeOpenAiSchemaValue(Pair.Value, Path + TEXT(".properties.") + Pair.Key, Issues);
					}
				}
			}
			else if (bHasStringType && TypeString == TEXT("array"))
			{
				const TSharedPtr<FJsonObject>* ItemsObject = nullptr;
				if (SchemaObject->TryGetObjectField(TEXT("items"), ItemsObject) && ItemsObject && (*ItemsObject).IsValid())
				{
					bCompatible &= AnalyzeOpenAiSchemaObject(*ItemsObject, Path + TEXT(".items"), Issues);
				}
				else
				{
					AddAuditIssue(Issues, TEXT("warning"), Path + TEXT(".items"), TEXT("Array schema should define an object-valued items schema."));
				}
			}

			return bCompatible;
		}

		bool AnalyzeOpenAiSchemaCompatibility(
			const TSharedPtr<FJsonObject>& InputSchema,
			TArray<TSharedPtr<FJsonValue>>& Issues,
			FString& OutReason,
			TSharedPtr<FJsonObject>& OutNormalizedSchema)
		{
			OutReason.Reset();
			OutNormalizedSchema = NormalizeOpenAiSchemaObject(InputSchema);

			bool bCompatible = AnalyzeOpenAiSchemaObject(OutNormalizedSchema, TEXT("inputSchema"), Issues);
			FString ExistingCompatibilityReason;
			if (!IsOpenAiSchemaCompatibleObject(OutNormalizedSchema, ExistingCompatibilityReason))
			{
				AddAuditIssue(Issues, TEXT("error"), TEXT("inputSchema"), ExistingCompatibilityReason);
				bCompatible = false;
			}

			if (!bCompatible)
			{
				OutReason = ExistingCompatibilityReason.IsEmpty() ? TEXT("Schema contains AI-incompatible fields.") : ExistingCompatibilityReason;
			}
			return bCompatible;
		}

		TSharedPtr<FJsonObject> FindToolDefinitionByName(
			const TArray<TSharedPtr<FJsonValue>>& ToolsArray,
			const FString& ToolName)
		{
			for (const TSharedPtr<FJsonValue>& ToolValue : ToolsArray)
			{
				if (!ToolValue.IsValid() || ToolValue->Type != EJson::Object || !ToolValue->AsObject().IsValid())
				{
					continue;
				}

				TSharedPtr<FJsonObject> ToolObject = ToolValue->AsObject();
				FString CandidateName;
				if (ToolObject->TryGetStringField(TEXT("name"), CandidateName) && CandidateName == ToolName)
				{
					return ToolObject;
				}
			}

			return nullptr;
		}

		void AddUniqueAuditCandidate(TArray<FString>& Candidates, const FString& Candidate)
		{
			if (Candidate.IsEmpty())
			{
				return;
			}

			FString NormalizedCandidate = FPaths::ConvertRelativePathToFull(Candidate);
			FPaths::NormalizeFilename(NormalizedCandidate);
			FPaths::CollapseRelativeDirectories(NormalizedCandidate);
			for (const FString& Existing : Candidates)
			{
				if (Existing.Equals(NormalizedCandidate, ESearchCase::IgnoreCase))
				{
					return;
				}
			}
			Candidates.Add(NormalizedCandidate);
		}

		FToolsReadResolution ResolveDocsPathForAudit(const FString& DocsPath)
		{
			FToolsReadResolution Resolution;
			FString DocsFilePath = DocsPath;
			FString DocsAnchor;
			if (DocsPath.Split(TEXT("#"), &DocsFilePath, &DocsAnchor))
			{
				// Keep only the versioned file path portion for existence checks.
			}
			DocsFilePath = DocsFilePath.TrimStartAndEnd();
			if (DocsFilePath.IsEmpty())
			{
				return Resolution;
			}

			if (FPaths::IsRelative(DocsFilePath))
			{
				AddUniqueAuditCandidate(Resolution.Candidates, FPaths::Combine(FPaths::ProjectDir(), DocsFilePath));

				const FToolsReadResolution ToolsRoot = ResolveToolsReadSubpath(FString(), TArray<FString>());
				if (!ToolsRoot.Warning.IsEmpty())
				{
					Resolution.Warning = ToolsRoot.Warning;
				}
				for (const FString& ToolsCandidate : ToolsRoot.Candidates)
				{
					AddUniqueAuditCandidate(Resolution.Candidates, FPaths::Combine(FPaths::GetPath(ToolsCandidate), DocsFilePath));
				}

				const FToolsReadResolution PluginBaseDir = ResolvePluginBaseDir();
				if (!PluginBaseDir.Path.IsEmpty())
				{
					AddUniqueAuditCandidate(Resolution.Candidates, FPaths::Combine(PluginBaseDir.Path, DocsFilePath));
					if (DocsFilePath.Equals(TEXT("README.md"), ESearchCase::IgnoreCase)
						|| DocsFilePath.EndsWith(TEXT("/README.md"), ESearchCase::IgnoreCase)
						|| DocsFilePath.EndsWith(TEXT("\\README.md"), ESearchCase::IgnoreCase))
					{
						AddUniqueAuditCandidate(Resolution.Candidates, FPaths::Combine(PluginBaseDir.Path, TEXT("README.md")));
					}
				}
			}
			else
			{
				AddUniqueAuditCandidate(Resolution.Candidates, DocsFilePath);
			}

			const FToolsReadResolution PluginBaseDir = ResolvePluginBaseDir();
			for (int32 CandidateIndex = 0; CandidateIndex < Resolution.Candidates.Num(); ++CandidateIndex)
			{
				if (!FPaths::FileExists(Resolution.Candidates[CandidateIndex]))
				{
					continue;
				}

				Resolution.Path = Resolution.Candidates[CandidateIndex];
				Resolution.bFound = true;
				if (!PluginBaseDir.Path.IsEmpty() && Resolution.Path.StartsWith(PluginBaseDir.Path, ESearchCase::IgnoreCase))
				{
					Resolution.SourceKind = FToolsReadResolution::ESource::PluginResources;
				}
				else
				{
					Resolution.SourceKind = CandidateIndex == 0
						? FToolsReadResolution::ESource::ProjectLocal
						: FToolsReadResolution::ESource::SharedRepoRoot;
				}
				return Resolution;
			}

			if (Resolution.Candidates.Num() > 0)
			{
				Resolution.Path = Resolution.Candidates[0];
			}
			return Resolution;
		}

		const TArray<FString>& GetAuditIssueCodes()
		{
			static const TArray<FString> Codes = {
				TEXT("core_registry_ok"),
				TEXT("user_registry_ok"),
				TEXT("descriptor_only"),
				TEXT("registry_no_handler"),
				TEXT("handler_no_registry"),
				TEXT("python_handler_missing"),
				TEXT("python_sha_mismatch"),
				TEXT("user_registry_invalid"),
				TEXT("reload_required"),
				TEXT("build_required"),
				TEXT("restart_required")
			};
			return Codes;
		}

		bool AuditCodeIsUserScoped(const FString& IssueCode)
		{
			return IssueCode == TEXT("user_registry_ok")
				|| IssueCode == TEXT("python_handler_missing")
				|| IssueCode == TEXT("python_sha_mismatch")
				|| IssueCode == TEXT("user_registry_invalid")
				|| IssueCode == TEXT("reload_required");
		}

		Extension::EImplementationTrack AuditImplementationTrackForEntry(
			const FToolRegistryEntry* RegistryEntry,
			const UserRegistry::FUserToolEntry* UserToolEntry,
			const FString& IssueCode)
		{
			if (UserToolEntry || AuditCodeIsUserScoped(IssueCode))
			{
				return Extension::EImplementationTrack::Python;
			}
			if (RegistryEntry && RegistryEntry->ImplementationTrack == EToolImplementationTrack::Python)
			{
				return Extension::EImplementationTrack::Python;
			}
			return Extension::EImplementationTrack::Cpp;
		}

		Extension::ESourceKind AuditSourceKindForIssue(
			const FString& ToolName,
			const FString& IssueCode,
			const FToolRegistryEntry* RegistryEntry,
			const UserRegistry::FUserToolEntry* UserToolEntry)
		{
			if (IssueCode == TEXT("descriptor_only"))
			{
				return Extension::ESourceKind::DescriptorOnly;
			}
			if (IssueCode == TEXT("handler_no_registry"))
			{
				return Extension::ESourceKind::HandlerOnly;
			}
			if (IssueCode == TEXT("registry_no_handler"))
			{
				return Extension::ESourceKind::MissingHandler;
			}
			if (IssueCode == TEXT("python_handler_missing"))
			{
				return Extension::ESourceKind::PythonHandlerMissing;
			}
			if (IssueCode == TEXT("python_sha_mismatch"))
			{
				return Extension::ESourceKind::PythonShaMismatch;
			}
			if (IssueCode == TEXT("build_required") || IssueCode == TEXT("restart_required"))
			{
				return Extension::ESourceKind::CoreRegistry;
			}
			if (UserToolEntry || AuditCodeIsUserScoped(IssueCode))
			{
				return Extension::ESourceKind::UserRegistry;
			}
			if (RegistryEntry && RegistryEntry->bLoadedFromDescriptor && !RegistryEntry->bLoadedFromExplicitRegistry)
			{
				return Extension::ESourceKind::DescriptorOnly;
			}
			return ResolveToolSourceKind(ToolName);
		}

		Extension::ELifecycleState AuditLifecycleStateForIssue(const FString& IssueCode)
		{
			if (IssueCode == TEXT("core_registry_ok"))
			{
				return Extension::ELifecycleState::LoadedCoreCppAfterRestart;
			}
			if (IssueCode == TEXT("user_registry_ok"))
			{
				return Extension::ELifecycleState::LoadedUserPythonHot;
			}
			if (IssueCode == TEXT("descriptor_only"))
			{
				return Extension::ELifecycleState::DraftScaffolded;
			}
			if (IssueCode == TEXT("python_sha_mismatch") || IssueCode == TEXT("reload_required"))
			{
				return Extension::ELifecycleState::AppliedUserPythonReloadRequired;
			}
			if (IssueCode == TEXT("build_required"))
			{
				return Extension::ELifecycleState::AppliedCoreCppBuildRequired;
			}
			if (IssueCode == TEXT("restart_required"))
			{
				return Extension::ELifecycleState::BuiltRestartRequired;
			}
			return Extension::ELifecycleState::Blocked;
		}

		FString AuditNextActionForIssue(const FString& IssueCode)
		{
			if (IssueCode == TEXT("core_registry_ok"))
			{
				return TEXT("run smoke or automation verification before claiming the core tool callable");
			}
			if (IssueCode == TEXT("user_registry_ok"))
			{
				return TEXT("run unreal.mcp_user_tool_smoke before claiming the user tool callable");
			}
			if (IssueCode == TEXT("descriptor_only"))
			{
				return TEXT("add a reviewed registry entry, rebuild if needed, then restart the editor");
			}
			if (IssueCode == TEXT("registry_no_handler") || IssueCode == TEXT("handler_no_registry"))
			{
				return TEXT("repair registry and handler metadata so they reconcile statically");
			}
			if (IssueCode == TEXT("python_handler_missing"))
			{
				return TEXT("restore main.py, then run unreal.mcp_user_registry_reload");
			}
			if (IssueCode == TEXT("python_sha_mismatch"))
			{
				return TEXT("review main.py, then run unreal.mcp_user_registry_reload with acceptChangedHashes=true if the change is approved");
			}
			if (IssueCode == TEXT("user_registry_invalid"))
			{
				return TEXT("fix tool.json or the user tool directory, then run unreal.mcp_user_registry_reload");
			}
			if (IssueCode == TEXT("reload_required"))
			{
				return TEXT("run unreal.mcp_user_registry_reload");
			}
			if (IssueCode == TEXT("build_required"))
			{
				return TEXT("run UBT to rebuild plugin dylib, then restart editor");
			}
			if (IssueCode == TEXT("restart_required"))
			{
				return TEXT("restart Unreal Editor so the rebuilt plugin dylib is loaded");
			}
			return TEXT("inspect and repair the reported audit mismatch");
		}

		Extension::FToolLifecycle BuildAuditLifecycle(
			const FString& ToolName,
			const FString& IssueCode,
			const FToolRegistryEntry* RegistryEntry,
			const FToolHandlerRegistryEntry* HandlerEntry,
			const UserRegistry::FUserToolEntry* UserToolEntry,
			const FString& ScaffoldDir,
			const FString& ManifestPath)
		{
			Extension::FToolLifecycle Lifecycle;
			Lifecycle.ToolName = ToolName;
			Lifecycle.ExtensionScope = (UserToolEntry || AuditCodeIsUserScoped(IssueCode) || ToolName.StartsWith(TEXT("user."), ESearchCase::CaseSensitive))
				? Extension::EExtensionScope::User
				: Extension::EExtensionScope::Core;
			Lifecycle.ImplementationTrack = AuditImplementationTrackForEntry(RegistryEntry, UserToolEntry, IssueCode);
			Lifecycle.HandlerKind = HandlerEntry
				? (HandlerEntry->ImplementationTrack == EToolImplementationTrack::Python ? Extension::EHandlerKind::PythonBridge : Extension::EHandlerKind::CppDispatcher)
				: Extension::EHandlerKind::None;
			if (IssueCode == TEXT("registry_no_handler")
				|| IssueCode == TEXT("handler_no_registry")
				|| IssueCode == TEXT("python_handler_missing")
				|| IssueCode == TEXT("user_registry_invalid")
				|| IssueCode == TEXT("build_required")
				|| IssueCode == TEXT("restart_required"))
			{
				Lifecycle.HandlerKind = Extension::EHandlerKind::None;
			}
			Lifecycle.SourceKind = AuditSourceKindForIssue(ToolName, IssueCode, RegistryEntry, UserToolEntry);
			Lifecycle.State = AuditLifecycleStateForIssue(IssueCode);
			Lifecycle.bCallableNow = Extension::IsLifecycleStateCallable(Lifecycle.State);
			Lifecycle.NextRequiredAction = AuditNextActionForIssue(IssueCode);
			Lifecycle.ScaffoldDir = UserToolEntry ? UserToolEntry->ScaffoldDir : ScaffoldDir;
			if (UserToolEntry)
			{
				Lifecycle.PythonHandlerPath = UserToolEntry->PythonHandlerPath;
				Lifecycle.RegistryPath = FPaths::Combine(UserToolEntry->ScaffoldDir, TEXT("tool.json"));
			}
			else if (RegistryEntry || Lifecycle.ExtensionScope == Extension::EExtensionScope::Core)
			{
				Lifecycle.RegistryPath = GetToolRegistrySourcePath();
			}
			Lifecycle.ManifestPath = ManifestPath;
			return Lifecycle;
		}

		FString AuditIssueCodeFromUserRejectionReason(const FString& Reason)
		{
			if (Reason.StartsWith(TEXT("python_handler_missing:"), ESearchCase::CaseSensitive))
			{
				return TEXT("python_handler_missing");
			}
			if (Reason.StartsWith(TEXT("python_sha_mismatch:"), ESearchCase::CaseSensitive))
			{
				return TEXT("python_sha_mismatch");
			}
			return TEXT("user_registry_invalid");
		}

		struct FAuditPendingCoreLifecycle
		{
			FString IssueCode;
			FString ToolName;
			FString ScaffoldDir;
			FString ManifestPath;
		};

		bool AuditManifestFilesMatchExpectedAfter(const TSharedPtr<FJsonObject>& ManifestObject)
		{
			if (!ManifestObject.IsValid())
			{
				return false;
			}

			auto FileMatchesExpectedAfter = [](const TSharedPtr<FJsonObject>& FileObject) -> bool
			{
				if (!FileObject.IsValid())
				{
					return false;
				}

				FString SourcePath;
				FString ExpectedAfterHash;
				FileObject->TryGetStringField(TEXT("sourcePath"), SourcePath);
				if (!FileObject->TryGetStringField(TEXT("hashAfter"), ExpectedAfterHash))
				{
					FileObject->TryGetStringField(TEXT("sourceHashAfter"), ExpectedAfterHash);
				}
				if (SourcePath.TrimStartAndEnd().IsEmpty() || ExpectedAfterHash.TrimStartAndEnd().IsEmpty())
				{
					return false;
				}

				FString ResolvedSourcePath;
				FString ResolveFailure;
				FToolsReadResolution::ESource SourceKind = FToolsReadResolution::ESource::Unresolved;
				if (!ResolvePathInsideTrustedSourceDomains(SourcePath, ResolvedSourcePath, SourceKind, ResolveFailure))
				{
					return false;
				}

				FString CurrentText;
				if (!FFileHelper::LoadFileToString(CurrentText, *ResolvedSourcePath))
				{
					return false;
				}
				return HashTextForManifest(CurrentText).Equals(ExpectedAfterHash, ESearchCase::CaseSensitive);
			};

			const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
			if (ManifestObject->TryGetArrayField(TEXT("files"), Files) && Files)
			{
				bool bSawFile = false;
				for (const TSharedPtr<FJsonValue>& FileValue : *Files)
				{
					const TSharedPtr<FJsonObject> FileObject = FileValue.IsValid() ? FileValue->AsObject() : nullptr;
					if (!FileObject.IsValid())
					{
						continue;
					}
					bSawFile = true;
					if (!FileMatchesExpectedAfter(FileObject))
					{
						return false;
					}
				}
				return bSawFile;
			}

			return FileMatchesExpectedAfter(ManifestObject);
		}

		bool AuditProjectMemoryHasBuildSucceededRestartRequired(const FString& ToolName)
		{
			TSharedPtr<FJsonObject> MemoryObject;
			FString FailureReason;
			if (!LoadProjectMemory(MemoryObject, FailureReason) || !MemoryObject.IsValid())
			{
				return false;
			}

			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if (!MemoryObject->TryGetArrayField(TEXT("entries"), Entries) || !Entries)
			{
				return false;
			}

			for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
			{
				const TSharedPtr<FJsonObject> EntryObject = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
				if (!EntryObject.IsValid())
				{
					continue;
				}

				FString Status;
				EntryObject->TryGetStringField(TEXT("status"), Status);
				if (!Status.Equals(TEXT("build_succeeded_restart_required"), ESearchCase::CaseSensitive))
				{
					continue;
				}

				const TSharedPtr<FJsonObject>* ContentObject = nullptr;
				if (EntryObject->TryGetObjectField(TEXT("content"), ContentObject) && ContentObject && (*ContentObject).IsValid())
				{
					FString MemoryToolName;
					(*ContentObject)->TryGetStringField(TEXT("toolName"), MemoryToolName);
					if (MemoryToolName.IsEmpty() || MemoryToolName.Equals(ToolName, ESearchCase::CaseSensitive))
					{
						return true;
					}
				}
				else if (ToolName.IsEmpty())
				{
					return true;
				}
			}

			return false;
		}

		TOptional<FAuditPendingCoreLifecycle> AuditLoadPendingCoreLifecycleFromLatestManifest()
		{
			const FString ManifestPath = GetLatestMcpExtensionManifestPath();
			if (!FPaths::FileExists(ManifestPath))
			{
				return TOptional<FAuditPendingCoreLifecycle>();
			}

			FString FailureReason;
			TSharedPtr<FJsonObject> ManifestObject;
			if (!LoadJsonObjectFromFile(ManifestPath, ManifestObject, FailureReason) || !ManifestObject.IsValid())
			{
				return TOptional<FAuditPendingCoreLifecycle>();
			}

			FString ToolName;
			ManifestObject->TryGetStringField(TEXT("toolName"), ToolName);
			ToolName = ToolName.TrimStartAndEnd();
			if (ToolName.IsEmpty() || !AuditManifestFilesMatchExpectedAfter(ManifestObject))
			{
				return TOptional<FAuditPendingCoreLifecycle>();
			}

			FAuditPendingCoreLifecycle Pending;
			Pending.ToolName = ToolName;
			Pending.ManifestPath = ManifestPath;
			ManifestObject->TryGetStringField(TEXT("scaffoldDir"), Pending.ScaffoldDir);
			Pending.IssueCode = AuditProjectMemoryHasBuildSucceededRestartRequired(ToolName)
				? TEXT("restart_required")
				: TEXT("build_required");
			return Pending;
		}

		FUnrealMcpExecutionResult ValidateMcpToolSchema(
			const FJsonObject& Arguments,
			const TArray<TSharedPtr<FJsonValue>>& ToolsArray)
		{
			FString ToolName;
			FString SchemaJson;
			bool bReturnNormalizedSchema = true;
			Arguments.TryGetStringField(TEXT("toolName"), ToolName);
			Arguments.TryGetStringField(TEXT("schemaJson"), SchemaJson);
			Arguments.TryGetBoolField(TEXT("returnNormalizedSchema"), bReturnNormalizedSchema);
			ToolName = ToolName.TrimStartAndEnd();
			SchemaJson = SchemaJson.TrimStartAndEnd();

			TSharedPtr<FJsonObject> InputSchema;
			FString Source = TEXT("schemaJson");
			if (!SchemaJson.IsEmpty())
			{
				if (!LoadJsonObject(SchemaJson, InputSchema) || !InputSchema.IsValid())
				{
					return MakeExecutionResult(TEXT("schemaJson must be a valid JSON object."), nullptr, true);
				}
			}
			else
			{
				if (ToolName.IsEmpty())
				{
					return MakeExecutionResult(TEXT("Provide either schemaJson or toolName."), nullptr, true);
				}

				const TSharedPtr<FJsonObject> ToolObject = FindToolDefinitionByName(ToolsArray, ToolName);
				if (!ToolObject.IsValid())
				{
					return MakeExecutionResult(FString::Printf(TEXT("Tool '%s' was not found in current tool definitions."), *ToolName), nullptr, true);
				}

				const TSharedPtr<FJsonObject>* SchemaObject = nullptr;
				if (!ToolObject->TryGetObjectField(TEXT("inputSchema"), SchemaObject) || !SchemaObject || !(*SchemaObject).IsValid())
				{
					return MakeExecutionResult(FString::Printf(TEXT("Tool '%s' does not expose an inputSchema object."), *ToolName), nullptr, true);
				}

				InputSchema = *SchemaObject;
				Source = TEXT("toolName");
			}

			TArray<TSharedPtr<FJsonValue>> Issues;
			FString Reason;
			TSharedPtr<FJsonObject> NormalizedSchema;
			const bool bCompatible = AnalyzeOpenAiSchemaCompatibility(InputSchema, Issues, Reason, NormalizedSchema);

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), TEXT("mcp_validate_tool_schema"));
			StructuredContent->SetStringField(TEXT("source"), Source);
			StructuredContent->SetStringField(TEXT("toolName"), ToolName);
			StructuredContent->SetBoolField(TEXT("compatible"), bCompatible);
			StructuredContent->SetStringField(TEXT("reason"), Reason);
			StructuredContent->SetArrayField(TEXT("issues"), Issues);
			if (bReturnNormalizedSchema && NormalizedSchema.IsValid())
			{
				StructuredContent->SetObjectField(TEXT("normalizedSchema"), NormalizedSchema);
			}

			const FString Text = bCompatible
				? FString::Printf(TEXT("Schema is AI-compatible. warnings=%d"), Issues.Num())
				: FString::Printf(TEXT("Schema is not AI-compatible: %s"), Reason.IsEmpty() ? TEXT("see issues") : *Reason);
			return MakeExecutionResult(Text, StructuredContent, !bCompatible);
		}

		FUnrealMcpExecutionResult AuditMcpTools(const TArray<TSharedPtr<FJsonValue>>& ToolsArray)
		{
			TArray<TSharedPtr<FJsonValue>> ToolReports;
			TArray<TSharedPtr<FJsonValue>> TaxonomyIssues;
			TArray<FString> MissingHandlers;
			TArray<FString> MissingDocs;
			TArray<FString> MissingRegistryEntries;
			TArray<FString> IncompatibleSchemas;
			TMap<FString, int32> IssueCodeCounts;
			for (const FString& Code : GetAuditIssueCodes())
			{
				IssueCodeCounts.Add(Code, 0);
			}
			int32 CompatibleCount = 0;
			int32 WarningCount = 0;
			int32 ReadOnlyRiskCount = 0;
			int32 LowRiskCount = 0;
			int32 MediumRiskCount = 0;
			int32 HighRiskCount = 0;
			int32 CriticalRiskCount = 0;
			int32 RequiresWriteCount = 0;
			int32 RequiresBuildCount = 0;
			int32 RequiresExternalProcessCount = 0;
			int32 RequiresRestartCount = 0;
			int32 RequiresProjectMemoryCount = 0;
			int32 RequiresLockCount = 0;
			TSet<FString> ReportedToolNames;

			TMap<FString, UserRegistry::FUserToolEntry> LoadedUserTools;
			TMap<FString, FString> UserIssueOverrides;
			TMap<FString, FString> UserIssueReasons;
			TMap<FString, FString> UserIssueScaffoldDirs;
			const FString UserToolsRootDir = UserRegistry::GetUserToolsRootDir();

			auto DeriveUserScaffoldDir = [&UserToolsRootDir](const FString& ToolNameOrDirectory)
			{
				FString DirectoryName = ToolNameOrDirectory;
				DirectoryName.RemoveFromStart(TEXT("user."));
				return FPaths::Combine(UserToolsRootDir, DirectoryName);
			};

			auto AddUserIssueOverride = [&UserIssueOverrides, &UserIssueReasons, &UserIssueScaffoldDirs, &DeriveUserScaffoldDir](
				const FString& ToolName,
				const FString& IssueCode,
				const FString& Reason)
			{
				if (ToolName.TrimStartAndEnd().IsEmpty())
				{
					return;
				}
				UserIssueOverrides.Add(ToolName, IssueCode);
				UserIssueReasons.Add(ToolName, Reason);
				UserIssueScaffoldDirs.Add(ToolName, DeriveUserScaffoldDir(ToolName));
			};

			{
				UserToolLock::FSharedGuard UserRegistryGuard;
				for (const UserRegistry::FUserToolEntry* UserTool : UserRegistry::GetAllUserTools())
				{
					if (UserTool)
					{
						LoadedUserTools.Add(UserTool->ToolName, *UserTool);
					}
				}

				const UserRegistry::FReloadResult PreviewReload = UserRegistry::PreviewUserToolRegistryReload(false);
				for (const FString& ToolName : PreviewReload.AddedTools)
				{
					AddUserIssueOverride(ToolName, TEXT("reload_required"), TEXT("user tool exists on disk but is not loaded"));
				}
				for (const FString& ToolName : PreviewReload.UpdatedTools)
				{
					AddUserIssueOverride(ToolName, TEXT("reload_required"), TEXT("user tool changed on disk after the last reload"));
				}
				for (const FString& ToolName : PreviewReload.RemovedTools)
				{
					AddUserIssueOverride(ToolName, TEXT("reload_required"), TEXT("loaded user tool was removed on disk"));
				}
				for (const UserRegistry::FReloadResult::FRejection& Rejection : PreviewReload.RejectedTools)
				{
					AddUserIssueOverride(
						Rejection.ToolName,
						AuditIssueCodeFromUserRejectionReason(Rejection.Reason),
						Rejection.Reason);
				}
			}

			const TOptional<FAuditPendingCoreLifecycle> PendingCoreLifecycle = AuditLoadPendingCoreLifecycleFromLatestManifest();

			auto RecordTaxonomyIssue = [&TaxonomyIssues, &IssueCodeCounts](const FString& ToolName, const FString& IssueCode)
			{
				IssueCodeCounts.FindOrAdd(IssueCode)++;
				TSharedPtr<FJsonObject> IssueObject = MakeShared<FJsonObject>();
				IssueObject->SetStringField(TEXT("toolName"), ToolName);
				IssueObject->SetStringField(TEXT("issueCode"), IssueCode);
				TaxonomyIssues.Add(MakeShared<FJsonValueObject>(IssueObject));
			};

			auto MakeIssueCountObject = [&IssueCodeCounts]()
			{
				TSharedPtr<FJsonObject> CountsObject = MakeShared<FJsonObject>();
				for (const FString& Code : GetAuditIssueCodes())
				{
					CountsObject->SetNumberField(Code, IssueCodeCounts.FindRef(Code));
				}
				return CountsObject;
			};

			auto AddSyntheticToolReport = [
				&ToolReports,
				&ReportedToolNames,
				&RecordTaxonomyIssue](
				const FString& ToolName,
				const FString& IssueCode,
				const FString& Reason,
				const FToolHandlerRegistryEntry* HandlerEntry,
				const FString& ScaffoldDir,
				const FString& ManifestPath)
			{
				if (ToolName.TrimStartAndEnd().IsEmpty() || ReportedToolNames.Contains(ToolName))
				{
					return;
				}

				TSharedPtr<FJsonObject> ReportObject = MakeShared<FJsonObject>();
				ReportObject->SetStringField(TEXT("name"), ToolName);
				ReportObject->SetStringField(TEXT("handlerName"), HandlerEntry ? HandlerEntry->HandlerName : ToolName);
				ReportObject->SetStringField(TEXT("title"), ToolName);
				ReportObject->SetStringField(TEXT("description"), Reason);
				ReportObject->SetBoolField(TEXT("hasHandler"), HandlerEntry != nullptr);
				ReportObject->SetStringField(TEXT("handlerCheckSource"), TEXT("explicit_handler_registry"));
				ReportObject->SetStringField(TEXT("handlerCategory"), HandlerEntry ? HandlerEntry->Category : TEXT(""));
				ReportObject->SetStringField(TEXT("handlerSourceFile"), HandlerEntry ? HandlerEntry->SourceFile : TEXT(""));
				ReportObject->SetBoolField(TEXT("hasExplicitRegistryEntry"), false);
				ReportObject->SetStringField(TEXT("registryCategory"), TEXT(""));
				ReportObject->SetStringField(TEXT("docsPath"), TEXT(""));
				ReportObject->SetBoolField(TEXT("docsPathFileExists"), false);
				ReportObject->SetStringField(TEXT("docsPathResolvedPath"), TEXT(""));
				ReportObject->SetStringField(TEXT("docsPathSourceKind"), TEXT("unresolved"));
				ReportObject->SetArrayField(TEXT("docsPathCandidates"), TArray<TSharedPtr<FJsonValue>>());
				ReportObject->SetStringField(TEXT("documentationCheckSource"), TEXT("not_applicable"));
				ReportObject->SetBoolField(TEXT("schemaCompatible"), false);
				ReportObject->SetStringField(TEXT("schemaReason"), Reason);
				ReportObject->SetArrayField(TEXT("schemaIssues"), TArray<TSharedPtr<FJsonValue>>());
				ReportObject->SetObjectField(TEXT("policy"), MakeToolPolicyObject(ToolName));
				ReportObject->SetStringField(TEXT("issueCode"), IssueCode);

				const Extension::FToolLifecycle Lifecycle = BuildAuditLifecycle(
					ToolName,
					IssueCode,
					nullptr,
					HandlerEntry,
					nullptr,
					ScaffoldDir,
					ManifestPath);
				ReportObject->SetObjectField(TEXT("lifecycle"), Extension::BuildLifecycleJson(Lifecycle));

				ToolReports.Add(MakeShared<FJsonValueObject>(ReportObject));
				ReportedToolNames.Add(ToolName);
				RecordTaxonomyIssue(ToolName, IssueCode);
			};

			for (const TSharedPtr<FJsonValue>& ToolValue : ToolsArray)
			{
				if (!ToolValue.IsValid() || ToolValue->Type != EJson::Object || !ToolValue->AsObject().IsValid())
				{
					continue;
				}

				TSharedPtr<FJsonObject> ToolObject = ToolValue->AsObject();
				FString Name;
				FString Title;
				FString Description;
				ToolObject->TryGetStringField(TEXT("name"), Name);
				ToolObject->TryGetStringField(TEXT("title"), Title);
				ToolObject->TryGetStringField(TEXT("description"), Description);

				const FString HandlerName = ResolveToolHandlerName(Name);
				const FToolHandlerRegistryEntry* HandlerEntry = FindToolHandlerRegistryEntry(HandlerName);
				const bool bHasHandler = HandlerEntry != nullptr;
				const FToolRegistryEntry* RegistryEntry = FindToolRegistryEntry(Name);
				const UserRegistry::FUserToolEntry* UserToolEntry = LoadedUserTools.Find(Name);
				const bool bIsUserTool = UserToolEntry != nullptr || Name.StartsWith(TEXT("user."), ESearchCase::CaseSensitive);
				const bool bHasExplicitRegistryEntry = RegistryEntry && RegistryEntry->bLoadedFromExplicitRegistry;
				const FString DocsPath = RegistryEntry ? RegistryEntry->Policy.DocsPath : FString();
				const FToolsReadResolution DocsResolution = ResolveDocsPathForAudit(DocsPath);
				const bool bDocumented = bIsUserTool ? true : DocsResolution.bFound;

				const TSharedPtr<FJsonObject>* SchemaObject = nullptr;
				TArray<TSharedPtr<FJsonValue>> Issues;
				FString Reason;
				TSharedPtr<FJsonObject> NormalizedSchema;
				bool bCompatible = true;
				if (ToolObject->TryGetObjectField(TEXT("inputSchema"), SchemaObject) && SchemaObject && (*SchemaObject).IsValid())
				{
					bCompatible = AnalyzeOpenAiSchemaCompatibility(*SchemaObject, Issues, Reason, NormalizedSchema);
				}
				else
				{
					AddAuditIssue(Issues, TEXT("error"), TEXT("inputSchema"), TEXT("Tool definition does not include an inputSchema object."));
					Reason = TEXT("missing inputSchema");
					bCompatible = false;
				}

				if (bCompatible)
				{
					++CompatibleCount;
				}
				else
				{
					IncompatibleSchemas.Add(Name);
				}
				if (!bHasHandler)
				{
					MissingHandlers.Add(Name);
				}
				if (!bIsUserTool && !bDocumented)
				{
					MissingDocs.Add(Name);
				}
				if (!bIsUserTool && !bHasExplicitRegistryEntry)
				{
					MissingRegistryEntries.Add(Name);
				}
				if (Issues.Num() > 0)
				{
					++WarningCount;
				}

				const FToolPolicy Policy = GetToolPolicy(Name);
				switch (Policy.RiskLevel)
				{
				case EToolRiskLevel::ReadOnly:
					++ReadOnlyRiskCount;
					break;
				case EToolRiskLevel::Low:
					++LowRiskCount;
					break;
				case EToolRiskLevel::Medium:
					++MediumRiskCount;
					break;
				case EToolRiskLevel::High:
					++HighRiskCount;
					break;
				case EToolRiskLevel::Critical:
					++CriticalRiskCount;
					break;
				default:
					break;
				}
				RequiresWriteCount += Policy.bRequiresWrite ? 1 : 0;
				RequiresBuildCount += Policy.bRequiresBuild ? 1 : 0;
				RequiresExternalProcessCount += Policy.bRequiresExternalProcess ? 1 : 0;
				RequiresRestartCount += Policy.bRequiresRestart ? 1 : 0;
				RequiresProjectMemoryCount += Policy.bRequiresProjectMemory ? 1 : 0;
				RequiresLockCount += Policy.bRequiresLock ? 1 : 0;

				FString IssueCode;
				if (const FString* UserOverride = UserIssueOverrides.Find(Name))
				{
					IssueCode = *UserOverride;
				}
				else if (PendingCoreLifecycle.IsSet()
					&& PendingCoreLifecycle.GetValue().ToolName.Equals(Name, ESearchCase::CaseSensitive)
					&& !(bHasExplicitRegistryEntry && bHasHandler))
				{
					IssueCode = PendingCoreLifecycle.GetValue().IssueCode;
				}
				else if (bIsUserTool)
				{
					IssueCode = bHasHandler ? TEXT("user_registry_ok") : TEXT("registry_no_handler");
				}
				else if (RegistryEntry && RegistryEntry->bLoadedFromExplicitRegistry && !bHasHandler)
				{
					IssueCode = TEXT("registry_no_handler");
				}
				else if (RegistryEntry && RegistryEntry->bLoadedFromDescriptor && !RegistryEntry->bLoadedFromExplicitRegistry)
				{
					IssueCode = TEXT("descriptor_only");
				}
				else if (!RegistryEntry && bHasHandler)
				{
					IssueCode = TEXT("handler_no_registry");
				}
				else if (RegistryEntry && bHasHandler)
				{
					IssueCode = TEXT("core_registry_ok");
				}
				else
				{
					IssueCode = TEXT("registry_no_handler");
				}

				TSharedPtr<FJsonObject> ReportObject = MakeShared<FJsonObject>();
				ReportObject->SetStringField(TEXT("name"), Name);
				ReportObject->SetStringField(TEXT("handlerName"), HandlerName);
				ReportObject->SetStringField(TEXT("title"), Title);
				ReportObject->SetStringField(TEXT("description"), Description);
				ReportObject->SetBoolField(TEXT("hasHandler"), bHasHandler);
				ReportObject->SetStringField(TEXT("handlerCheckSource"), TEXT("explicit_handler_registry"));
				ReportObject->SetStringField(TEXT("handlerCategory"), HandlerEntry ? HandlerEntry->Category : TEXT(""));
				ReportObject->SetStringField(TEXT("handlerSourceFile"), HandlerEntry ? HandlerEntry->SourceFile : TEXT(""));
				ReportObject->SetBoolField(TEXT("hasExplicitRegistryEntry"), bHasExplicitRegistryEntry);
				ReportObject->SetStringField(TEXT("registryCategory"), RegistryEntry ? RegistryEntry->Category : TEXT(""));
				ReportObject->SetStringField(TEXT("docsPath"), DocsPath);
				ReportObject->SetBoolField(TEXT("docsPathFileExists"), bDocumented);
				ReportObject->SetStringField(TEXT("docsPathResolvedPath"), DocsResolution.Path);
				ReportObject->SetStringField(TEXT("docsPathSourceKind"), LexToString(DocsResolution.SourceKind));
				ReportObject->SetArrayField(TEXT("docsPathCandidates"), MakeToolsReadCandidateValues(DocsResolution));
				if (!DocsResolution.Warning.IsEmpty())
				{
					ReportObject->SetStringField(TEXT("docsPathResolutionWarning"), DocsResolution.Warning);
				}
				ReportObject->SetStringField(TEXT("documentationCheckSource"), TEXT("explicit_registry_docsPath"));
				ReportObject->SetBoolField(TEXT("schemaCompatible"), bCompatible);
				ReportObject->SetStringField(TEXT("schemaReason"), Reason);
				ReportObject->SetArrayField(TEXT("schemaIssues"), Issues);
				ReportObject->SetObjectField(TEXT("policy"), MakeToolPolicyObject(Name));
				ReportObject->SetStringField(TEXT("issueCode"), IssueCode);
				if (const FString* UserReason = UserIssueReasons.Find(Name))
				{
					ReportObject->SetStringField(TEXT("issueReason"), *UserReason);
				}
				const FString ManifestPath = PendingCoreLifecycle.IsSet()
					&& PendingCoreLifecycle.GetValue().ToolName.Equals(Name, ESearchCase::CaseSensitive)
						? PendingCoreLifecycle.GetValue().ManifestPath
						: FString();
				const FString ScaffoldDir = UserIssueScaffoldDirs.FindRef(Name);
				const Extension::FToolLifecycle Lifecycle = BuildAuditLifecycle(
					Name,
					IssueCode,
					RegistryEntry,
					HandlerEntry,
					UserToolEntry,
					ScaffoldDir,
					ManifestPath);
				ReportObject->SetObjectField(TEXT("lifecycle"), Extension::BuildLifecycleJson(Lifecycle));
				ToolReports.Add(MakeShared<FJsonValueObject>(ReportObject));
				ReportedToolNames.Add(Name);
				RecordTaxonomyIssue(Name, IssueCode);
			}

			for (const TPair<FString, FString>& Pair : UserIssueOverrides)
			{
				const FString& ToolName = Pair.Key;
				if (ReportedToolNames.Contains(ToolName))
				{
					continue;
				}
				const FString Reason = UserIssueReasons.FindRef(ToolName);
				AddSyntheticToolReport(
					ToolName,
					Pair.Value,
					Reason.IsEmpty() ? AuditNextActionForIssue(Pair.Value) : Reason,
					FindToolHandlerRegistryEntry(ResolveToolHandlerName(ToolName)),
					UserIssueScaffoldDirs.FindRef(ToolName),
					FString());
				if (Pair.Value == TEXT("registry_no_handler") || Pair.Value == TEXT("python_handler_missing"))
				{
					MissingHandlers.Add(ToolName);
				}
			}

			if (PendingCoreLifecycle.IsSet() && !ReportedToolNames.Contains(PendingCoreLifecycle.GetValue().ToolName))
			{
				AddSyntheticToolReport(
					PendingCoreLifecycle.GetValue().ToolName,
					PendingCoreLifecycle.GetValue().IssueCode,
					AuditNextActionForIssue(PendingCoreLifecycle.GetValue().IssueCode),
					nullptr,
					PendingCoreLifecycle.GetValue().ScaffoldDir,
					PendingCoreLifecycle.GetValue().ManifestPath);
			}

			for (const FToolHandlerRegistryEntry& HandlerRegistryEntry : GetToolHandlerRegistryEntries())
			{
				for (const FString& ToolName : HandlerRegistryEntry.ToolNames)
				{
					if (ReportedToolNames.Contains(ToolName)
						|| FindToolRegistryEntry(ToolName)
						|| LoadedUserTools.Contains(ToolName))
					{
						continue;
					}
					AddSyntheticToolReport(
						ToolName,
						TEXT("handler_no_registry"),
						TEXT("handler registry contains a routed tool name that is absent from core registry and user overlay"),
						&HandlerRegistryEntry,
						FString(),
						FString());
					MissingRegistryEntries.Add(ToolName);
				}
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), TEXT("mcp_tool_audit"));
			StructuredContent->SetStringField(TEXT("auditMode"), TEXT("static_registry_and_handler_reconciliation"));
			StructuredContent->SetBoolField(TEXT("didInvokeHandlers"), false);
			StructuredContent->SetNumberField(TEXT("coreToolCount"), GetCoreToolCount());
			StructuredContent->SetNumberField(TEXT("userToolCount"), UserRegistry::GetUserToolCount());
			StructuredContent->SetNumberField(TEXT("toolCount"), GetCoreToolCount() + UserRegistry::GetUserToolCount());
			StructuredContent->SetNumberField(TEXT("schemaCompatibleCount"), CompatibleCount);
			StructuredContent->SetNumberField(TEXT("schemaIncompatibleCount"), IncompatibleSchemas.Num());
			StructuredContent->SetNumberField(TEXT("toolsWithSchemaIssues"), WarningCount);
			StructuredContent->SetNumberField(TEXT("missingHandlerCount"), MissingHandlers.Num());
			StructuredContent->SetNumberField(TEXT("missingDocumentationCount"), MissingDocs.Num());
			StructuredContent->SetNumberField(TEXT("missingRegistryEntryCount"), MissingRegistryEntries.Num());
			StructuredContent->SetStringField(TEXT("toolRegistrySourcePath"), GetToolRegistrySourcePath());
			TSharedPtr<FJsonObject> RiskCountsObject = MakeShared<FJsonObject>();
			RiskCountsObject->SetNumberField(TEXT("readOnly"), ReadOnlyRiskCount);
			RiskCountsObject->SetNumberField(TEXT("low"), LowRiskCount);
			RiskCountsObject->SetNumberField(TEXT("medium"), MediumRiskCount);
			RiskCountsObject->SetNumberField(TEXT("high"), HighRiskCount);
			RiskCountsObject->SetNumberField(TEXT("critical"), CriticalRiskCount);
			StructuredContent->SetObjectField(TEXT("riskCounts"), RiskCountsObject);
			StructuredContent->SetNumberField(TEXT("requiresWriteCount"), RequiresWriteCount);
			StructuredContent->SetNumberField(TEXT("requiresBuildCount"), RequiresBuildCount);
			StructuredContent->SetNumberField(TEXT("requiresExternalProcessCount"), RequiresExternalProcessCount);
			StructuredContent->SetNumberField(TEXT("requiresRestartCount"), RequiresRestartCount);
			StructuredContent->SetNumberField(TEXT("requiresProjectMemoryCount"), RequiresProjectMemoryCount);
			StructuredContent->SetNumberField(TEXT("requiresLockCount"), RequiresLockCount);
			StructuredContent->SetObjectField(TEXT("toolRegistryValidation"), MakeToolRegistryValidationObject(&ToolsArray));
			StructuredContent->SetArrayField(TEXT("taxonomyIssues"), TaxonomyIssues);
			StructuredContent->SetObjectField(TEXT("issueCodeCounts"), MakeIssueCountObject());
			StructuredContent->SetObjectField(TEXT("issueCounts"), MakeIssueCountObject());
			StructuredContent->SetArrayField(TEXT("tools"), ToolReports);

			auto AddStringArray = [](TSharedPtr<FJsonObject> Object, const FString& FieldName, const TArray<FString>& Values)
			{
				TArray<TSharedPtr<FJsonValue>> JsonValues;
				for (const FString& Value : Values)
				{
					JsonValues.Add(MakeShared<FJsonValueString>(Value));
				}
				Object->SetArrayField(FieldName, JsonValues);
			};

			AddStringArray(StructuredContent, TEXT("schemaIncompatibleTools"), IncompatibleSchemas);
			AddStringArray(StructuredContent, TEXT("missingHandlerTools"), MissingHandlers);
			AddStringArray(StructuredContent, TEXT("missingDocumentationTools"), MissingDocs);
			AddStringArray(StructuredContent, TEXT("missingRegistryEntryTools"), MissingRegistryEntries);

			const FString Text = FString::Printf(
				TEXT("Audited %d MCP tools. schemaCompatible=%d incompatible=%d missingHandlers=%d missingDocs=%d missingRegistry=%d"),
				ToolsArray.Num(),
				CompatibleCount,
				IncompatibleSchemas.Num(),
				MissingHandlers.Num(),
				MissingDocs.Num(),
				MissingRegistryEntries.Num());
			return MakeExecutionResult(Text, StructuredContent, false);
		}



}
