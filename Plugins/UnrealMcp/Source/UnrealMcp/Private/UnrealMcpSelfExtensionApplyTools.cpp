#include "UnrealMcpSelfExtensionTools.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UnrealMcp
{
	int32 GetPositiveIntArgument(const FJsonObject& Arguments, const FString& FieldName, int32 DefaultValue);
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);
	FString HashTextForManifest(const FString& Text);
	FString GetMcpModuleSourcePath();
	FString GetMcpExtensionBackupRoot();
	FString GetLatestMcpExtensionManifestPath();
	FString GetMcpExtensionLockPath();
	FString SanitizeMcpToolIdForPath(const FString& ToolName);
	bool LoadJsonObjectFromFile(const FString& FilePath, TSharedPtr<FJsonObject>& OutObject, FString& OutFailureReason);
	bool SaveJsonObjectToFile(const TSharedPtr<FJsonObject>& Object, const FString& FilePath, FString& OutFailureReason);
	bool ResolveMcpScaffoldDirectory(const FJsonObject& Arguments, FString& OutDirectory, FString& OutToolName, FString& OutFailureReason);
	bool LoadScaffoldSnippet(
		const FString& ScaffoldDirectory,
		const FString& FileName,
		bool bRequired,
		FString& OutSnippet,
		TArray<TSharedPtr<FJsonValue>>& Issues,
		FString& OutFailureReason);
	TSharedPtr<FJsonObject> ValidateCppSnippetText(
		const FString& SnippetText,
		const FString& SnippetName,
		const FString& ToolName);
	TSharedPtr<FJsonObject> MakeTextDiffObject(const FString& BeforeText, const FString& AfterText, int32 MaxPreviewLines);

		enum class EMcpScaffoldInsertionStatus
		{
			WillInsert,
			Inserted,
			SkippedAlreadyIntegrated,
			SkippedOptionalMissing,
			Conflict,
			MissingAnchor
		};

		const TCHAR* LexToString(EMcpScaffoldInsertionStatus Status)
		{
			switch (Status)
			{
			case EMcpScaffoldInsertionStatus::WillInsert:
				return TEXT("will_insert");
			case EMcpScaffoldInsertionStatus::Inserted:
				return TEXT("inserted");
			case EMcpScaffoldInsertionStatus::SkippedAlreadyIntegrated:
				return TEXT("skipped_already_integrated");
			case EMcpScaffoldInsertionStatus::SkippedOptionalMissing:
				return TEXT("skipped_optional_missing");
			case EMcpScaffoldInsertionStatus::Conflict:
				return TEXT("conflict");
			case EMcpScaffoldInsertionStatus::MissingAnchor:
				return TEXT("missing_anchor");
			default:
				return TEXT("unknown");
			}
		}

		static constexpr int32 GUnrealMcpExtensionManifestSchemaVersion = 1;

		const TCHAR* GetUnrealMcpExtensionManifestSchemaName()
		{
			return TEXT("UnrealMcpExtensionManifest.v1");
		}

		TSharedPtr<FJsonObject> MakeInsertionChangeObject(
			const FString& Section,
			EMcpScaffoldInsertionStatus Status,
			const FString& Message,
			int32 Offset,
			const FString& Preview)
		{
			TSharedPtr<FJsonObject> ChangeObject = MakeShared<FJsonObject>();
			ChangeObject->SetStringField(TEXT("section"), Section);
			ChangeObject->SetStringField(TEXT("status"), LexToString(Status));
			ChangeObject->SetStringField(TEXT("message"), Message);
			ChangeObject->SetNumberField(TEXT("offset"), Offset);
			ChangeObject->SetStringField(TEXT("preview"), Preview);
			return ChangeObject;
		}

		int32 CountScaffoldChangesByStatus(const TArray<TSharedPtr<FJsonValue>>& Changes, const FString& Status)
		{
			int32 Count = 0;
			for (const TSharedPtr<FJsonValue>& ChangeValue : Changes)
			{
				TSharedPtr<FJsonObject> ChangeObject;
				if (ChangeValue.IsValid())
				{
					ChangeObject = ChangeValue->AsObject();
				}
				if (!ChangeObject.IsValid())
				{
					continue;
				}

				FString ChangeStatus;
				if (ChangeObject->TryGetStringField(TEXT("status"), ChangeStatus) && ChangeStatus == Status)
				{
					++Count;
				}
			}
			return Count;
		}

		TSharedPtr<FJsonObject> MakeScaffoldConflictPolicyObject()
		{
			TSharedPtr<FJsonObject> PolicyObject = MakeShared<FJsonObject>();
			PolicyObject->SetBoolField(TEXT("exactSnippetIsIdempotent"), true);
			PolicyObject->SetBoolField(TEXT("conflictNeedleBlocksApply"), true);
			PolicyObject->SetBoolField(TEXT("missingAnchorBlocksApply"), true);
			PolicyObject->SetBoolField(TEXT("unsafeSnippetBlocksApplyByDefault"), true);
			PolicyObject->SetStringField(TEXT("conflictDetector"), TEXT("PlanOrApplyScaffoldInsertion"));
			return PolicyObject;
		}

		FString GetActiveExtensionSessionIdForManifest()
		{
			TSharedPtr<FJsonObject> LockObject;
			FString FailureReason;
			if (!LoadJsonObjectFromFile(GetMcpExtensionLockPath(), LockObject, FailureReason) || !LockObject.IsValid())
			{
				return FString();
			}

			FString SessionId;
			LockObject->TryGetStringField(TEXT("sessionId"), SessionId);
			return SessionId;
		}

		bool PlanOrApplyScaffoldInsertion(
			FString& SourceText,
			const FString& ConflictSourceText,
			const FString& Section,
			const FString& ToolName,
			const FString& Snippet,
			const FString& Anchor,
			const FString& ConflictNeedle,
			bool bDryRun,
			TArray<TSharedPtr<FJsonValue>>& Changes,
			bool& bOutChanged)
		{
			const FString TrimmedSnippet = Snippet.TrimStartAndEnd();
			if (TrimmedSnippet.IsEmpty())
			{
				Changes.Add(MakeShared<FJsonValueObject>(MakeInsertionChangeObject(
					Section,
					EMcpScaffoldInsertionStatus::Conflict,
					TEXT("Snippet is empty."),
					INDEX_NONE,
					FString())));
				return false;
			}

			if (SourceText.Contains(TrimmedSnippet, ESearchCase::CaseSensitive))
			{
				Changes.Add(MakeShared<FJsonValueObject>(MakeInsertionChangeObject(
					Section,
					EMcpScaffoldInsertionStatus::SkippedAlreadyIntegrated,
					TEXT("Exact snippet is already present."),
					INDEX_NONE,
					FString())));
				return true;
			}

			if (!ConflictNeedle.IsEmpty() && ConflictSourceText.Contains(ConflictNeedle, ESearchCase::CaseSensitive))
			{
				Changes.Add(MakeShared<FJsonValueObject>(MakeInsertionChangeObject(
					Section,
					EMcpScaffoldInsertionStatus::Conflict,
					FString::Printf(TEXT("Source already contains conflict marker '%s' but not the exact snippet."), *ConflictNeedle),
					INDEX_NONE,
					TrimmedSnippet.Left(800))));
				return false;
			}

			const int32 AnchorOffset = SourceText.Find(Anchor, ESearchCase::CaseSensitive);
			int32 ResolvedAnchorOffset = AnchorOffset;
			if (ResolvedAnchorOffset == INDEX_NONE && Section == TEXT("AppendToolDefinitions"))
			{
				const int32 CompileBlueprintMarkerOffset = SourceText.Find(TEXT("TEXT(\"unreal.compile_blueprint\")"), ESearchCase::CaseSensitive);
				if (CompileBlueprintMarkerOffset != INDEX_NONE)
				{
					const FString Prefix = SourceText.Left(CompileBlueprintMarkerOffset);
					ResolvedAnchorOffset = Prefix.Find(TEXT("\n\t\t\t{"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
					if (ResolvedAnchorOffset == INDEX_NONE)
					{
						ResolvedAnchorOffset = Prefix.Find(TEXT("\n\t\t{"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
					}
				}
			}
			if (ResolvedAnchorOffset == INDEX_NONE)
			{
				Changes.Add(MakeShared<FJsonValueObject>(MakeInsertionChangeObject(
					Section,
					EMcpScaffoldInsertionStatus::MissingAnchor,
					TEXT("Insertion anchor was not found in UnrealMcpModule.cpp."),
					INDEX_NONE,
					TrimmedSnippet.Left(800))));
				return false;
			}

			const FString InsertionText = FString::Printf(TEXT("\n%s\n"), *TrimmedSnippet);
			Changes.Add(MakeShared<FJsonValueObject>(MakeInsertionChangeObject(
				Section,
				bDryRun ? EMcpScaffoldInsertionStatus::WillInsert : EMcpScaffoldInsertionStatus::Inserted,
				bDryRun ? TEXT("Would insert snippet before anchor.") : TEXT("Inserted snippet before anchor."),
				ResolvedAnchorOffset,
				TrimmedSnippet.Left(800))));

			if (!bDryRun)
			{
				SourceText.InsertAt(ResolvedAnchorOffset, InsertionText);
				bOutChanged = true;
			}
			return true;
		}

		FUnrealMcpExecutionResult ApplyMcpScaffold(const FJsonObject& Arguments)
		{
			FString ScaffoldDirectory;
			FString ToolName;
			FString FailureReason;
			if (!ResolveMcpScaffoldDirectory(Arguments, ScaffoldDirectory, ToolName, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			bool bDryRun = true;
			bool bApplyChatCommand = true;
			bool bCreateBackup = true;
			bool bValidateSnippets = true;
			bool bAllowUnsafeSnippets = false;
			Arguments.TryGetBoolField(TEXT("dryRun"), bDryRun);
			Arguments.TryGetBoolField(TEXT("applyChatCommand"), bApplyChatCommand);
			Arguments.TryGetBoolField(TEXT("createBackup"), bCreateBackup);
			Arguments.TryGetBoolField(TEXT("validateSnippets"), bValidateSnippets);
			Arguments.TryGetBoolField(TEXT("allowUnsafeSnippets"), bAllowUnsafeSnippets);
			const int32 TargetDiffPreviewLines = FMath::Min(GetPositiveIntArgument(Arguments, TEXT("targetDiffPreviewLines"), 120), 1000);

			TArray<TSharedPtr<FJsonValue>> Issues;
			FString DefinitionSnippet;
			FString HandlerSnippet;
			FString ChatCommandSnippet;
			if (!LoadScaffoldSnippet(ScaffoldDirectory, TEXT("ToolDefinition.cpp.snippet"), true, DefinitionSnippet, Issues, FailureReason)
				|| !LoadScaffoldSnippet(ScaffoldDirectory, TEXT("ExecuteToolHandler.cpp.snippet"), true, HandlerSnippet, Issues, FailureReason)
				|| !LoadScaffoldSnippet(ScaffoldDirectory, TEXT("ChatCommand.cpp.snippet"), bApplyChatCommand, ChatCommandSnippet, Issues, FailureReason))
			{
				TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
				StructuredContent->SetStringField(TEXT("action"), TEXT("mcp_apply_scaffold"));
				StructuredContent->SetStringField(TEXT("toolName"), ToolName);
				StructuredContent->SetStringField(TEXT("scaffoldDir"), ScaffoldDirectory);
				StructuredContent->SetArrayField(TEXT("issues"), Issues);
				return MakeExecutionResult(FailureReason, StructuredContent, true);
			}

			TArray<TSharedPtr<FJsonValue>> SnippetValidations;
			bool bSnippetsSafe = true;
			if (bValidateSnippets)
			{
				TSharedPtr<FJsonObject> DefinitionValidation = ValidateCppSnippetText(DefinitionSnippet, TEXT("ToolDefinition.cpp.snippet"), ToolName);
				TSharedPtr<FJsonObject> HandlerValidation = ValidateCppSnippetText(HandlerSnippet, TEXT("ExecuteToolHandler.cpp.snippet"), ToolName);
				bSnippetsSafe &= DefinitionValidation->GetBoolField(TEXT("safe"));
				bSnippetsSafe &= HandlerValidation->GetBoolField(TEXT("safe"));
				SnippetValidations.Add(MakeShared<FJsonValueObject>(DefinitionValidation));
				SnippetValidations.Add(MakeShared<FJsonValueObject>(HandlerValidation));
				if (bApplyChatCommand)
				{
					TSharedPtr<FJsonObject> ChatValidation = ValidateCppSnippetText(ChatCommandSnippet, TEXT("ChatCommand.cpp.snippet"), ToolName);
					bSnippetsSafe &= ChatValidation->GetBoolField(TEXT("safe"));
					SnippetValidations.Add(MakeShared<FJsonValueObject>(ChatValidation));
				}
			}

			const FString SourcePath = GetMcpModuleSourcePath();
			FString SourceText;
			if (!FFileHelper::LoadFileToString(SourceText, *SourcePath))
			{
				return MakeExecutionResult(FString::Printf(TEXT("Failed to read source file '%s'."), *SourcePath), nullptr, true);
			}

			const FString SourceHashBefore = HashTextForManifest(SourceText);
			const FString OriginalSourceText = SourceText;
			TArray<TSharedPtr<FJsonValue>> Changes;
			bool bChanged = false;
			bool bCanApply = true;

			const FString DefinitionAnchor =
				TEXT("\n\t\t\t{\n\t\t\t\tTSharedPtr<FJsonObject> PropertiesObject = MakeShared<FJsonObject>();\n\t\t\t\tPropertiesObject->SetObjectField(TEXT(\"path\"), UnrealMcp::MakeStringProperty(TEXT(\"Blueprint asset path to compile.\")));");
			const FString HandlerAnchor =
				TEXT("\n\treturn UnrealMcp::MakeExecutionResult(FString::Printf(TEXT(\"Unknown tool '%s'.\"), *ToolName), nullptr, true);");
			const FString ChatCommandAnchor =
				TEXT("\n\t\treturn UnrealMcp::MakeExecutionResult(TEXT(\"Unknown command. Try /help.\"), nullptr, true);");
			const FString ToolNameNeedle = FString::Printf(TEXT("TEXT(\"%s\")"), *ToolName);
			const FString ChatCommandNeedle = FString::Printf(TEXT("TEXT(\"/%s\")"), *SanitizeMcpToolIdForPath(ToolName));

			bCanApply &= PlanOrApplyScaffoldInsertion(
				SourceText,
				OriginalSourceText,
				TEXT("AppendToolDefinitions"),
				ToolName,
				DefinitionSnippet,
				DefinitionAnchor,
				ToolNameNeedle,
				bDryRun,
				Changes,
				bChanged);

			bCanApply &= PlanOrApplyScaffoldInsertion(
				SourceText,
				OriginalSourceText,
				TEXT("ExecuteTool"),
				ToolName,
				HandlerSnippet,
				HandlerAnchor,
				ToolNameNeedle,
				bDryRun,
				Changes,
				bChanged);

			if (bApplyChatCommand)
			{
				bCanApply &= PlanOrApplyScaffoldInsertion(
					SourceText,
					OriginalSourceText,
					TEXT("ExecuteChatCommand"),
					ToolName,
					ChatCommandSnippet,
					ChatCommandAnchor,
					ChatCommandNeedle,
					bDryRun,
					Changes,
					bChanged);
			}
			else
			{
				Changes.Add(MakeShared<FJsonValueObject>(MakeInsertionChangeObject(
					TEXT("ExecuteChatCommand"),
					EMcpScaffoldInsertionStatus::SkippedOptionalMissing,
					TEXT("applyChatCommand=false; skipped optional chat command snippet."),
					INDEX_NONE,
					FString())));
			}

			const bool bInsertionCanApply = bCanApply;
			TSharedPtr<FJsonObject> TargetSourceDiff = MakeShared<FJsonObject>();
			if (bInsertionCanApply)
			{
				FString PlannedSourceText = OriginalSourceText;
				TArray<TSharedPtr<FJsonValue>> PlannedChanges;
				bool bPlannedChanged = false;
				PlanOrApplyScaffoldInsertion(
					PlannedSourceText,
					OriginalSourceText,
					TEXT("AppendToolDefinitions"),
					ToolName,
					DefinitionSnippet,
					DefinitionAnchor,
					ToolNameNeedle,
					false,
					PlannedChanges,
					bPlannedChanged);
				PlanOrApplyScaffoldInsertion(
					PlannedSourceText,
					OriginalSourceText,
					TEXT("ExecuteTool"),
					ToolName,
					HandlerSnippet,
					HandlerAnchor,
					ToolNameNeedle,
					false,
					PlannedChanges,
					bPlannedChanged);
				if (bApplyChatCommand)
				{
					PlanOrApplyScaffoldInsertion(
						PlannedSourceText,
						OriginalSourceText,
						TEXT("ExecuteChatCommand"),
						ToolName,
						ChatCommandSnippet,
						ChatCommandAnchor,
						ChatCommandNeedle,
						false,
						PlannedChanges,
						bPlannedChanged);
				}
				TargetSourceDiff = MakeTextDiffObject(OriginalSourceText, PlannedSourceText, TargetDiffPreviewLines);
			}

			if (bValidateSnippets && !bSnippetsSafe && !bAllowUnsafeSnippets)
			{
				bCanApply = false;
			}

			const int32 ConflictCount = CountScaffoldChangesByStatus(Changes, TEXT("conflict"));
			const int32 MissingAnchorCount = CountScaffoldChangesByStatus(Changes, TEXT("missing_anchor"));
			const FString ExtensionSessionId = GetActiveExtensionSessionIdForManifest();
			const TSharedPtr<FJsonObject> ConflictPolicy = MakeScaffoldConflictPolicyObject();

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), TEXT("mcp_apply_scaffold"));
			StructuredContent->SetNumberField(TEXT("manifestSchemaVersion"), GUnrealMcpExtensionManifestSchemaVersion);
			StructuredContent->SetStringField(TEXT("manifestSchema"), GetUnrealMcpExtensionManifestSchemaName());
			StructuredContent->SetStringField(TEXT("sessionId"), ExtensionSessionId);
			StructuredContent->SetStringField(TEXT("toolName"), ToolName);
			StructuredContent->SetStringField(TEXT("toolId"), SanitizeMcpToolIdForPath(ToolName));
			StructuredContent->SetStringField(TEXT("scaffoldDir"), ScaffoldDirectory);
			StructuredContent->SetStringField(TEXT("sourcePath"), SourcePath);
			StructuredContent->SetBoolField(TEXT("dryRun"), bDryRun);
			StructuredContent->SetBoolField(TEXT("canApply"), bCanApply);
			StructuredContent->SetBoolField(TEXT("changed"), bChanged);
			StructuredContent->SetBoolField(TEXT("validateSnippets"), bValidateSnippets);
			StructuredContent->SetBoolField(TEXT("snippetsSafe"), bSnippetsSafe);
			StructuredContent->SetBoolField(TEXT("allowUnsafeSnippets"), bAllowUnsafeSnippets);
			StructuredContent->SetStringField(TEXT("sourceHashBefore"), SourceHashBefore);
			StructuredContent->SetNumberField(TEXT("conflictCount"), ConflictCount);
			StructuredContent->SetNumberField(TEXT("missingAnchorCount"), MissingAnchorCount);
			StructuredContent->SetObjectField(TEXT("conflictPolicy"), ConflictPolicy);
			StructuredContent->SetArrayField(TEXT("issues"), Issues);
			StructuredContent->SetArrayField(TEXT("snippetValidations"), SnippetValidations);
			StructuredContent->SetArrayField(TEXT("changes"), Changes);
			StructuredContent->SetObjectField(TEXT("targetSourceDiff"), TargetSourceDiff);

			if (!bCanApply)
			{
				return MakeExecutionResult(TEXT("Scaffold cannot be applied safely. See changes, issues, snippetValidations, and targetSourceDiff."), StructuredContent, true);
			}

			if (bDryRun)
			{
				return MakeExecutionResult(
					FString::Printf(TEXT("Dry run complete for %s. canApply=true"), *ToolName),
					StructuredContent,
					false);
			}

			if (!bChanged)
			{
				return MakeExecutionResult(
					FString::Printf(TEXT("No source changes needed for %s; scaffold appears already integrated."), *ToolName),
					StructuredContent,
					false);
			}

			const FString Timestamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"));
			const FString BackupDirectory = FPaths::Combine(GetMcpExtensionBackupRoot(), Timestamp + TEXT("_") + SanitizeMcpToolIdForPath(ToolName));
			const FString BackupSourcePath = FPaths::Combine(BackupDirectory, TEXT("UnrealMcpModule.cpp.before"));
			const FString AfterSourcePath = FPaths::Combine(BackupDirectory, TEXT("UnrealMcpModule.cpp.after"));
			if (bCreateBackup)
			{
				if (!IFileManager::Get().MakeDirectory(*BackupDirectory, true))
				{
					return MakeExecutionResult(FString::Printf(TEXT("Failed to create backup directory '%s'."), *BackupDirectory), StructuredContent, true);
				}
				if (!FFileHelper::SaveStringToFile(SourceText, *AfterSourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
				{
					return MakeExecutionResult(FString::Printf(TEXT("Failed to write after snapshot '%s'."), *AfterSourcePath), StructuredContent, true);
				}
				FString BackupOriginalSourceText;
				if (!FFileHelper::LoadFileToString(BackupOriginalSourceText, *SourcePath)
					|| !FFileHelper::SaveStringToFile(BackupOriginalSourceText, *BackupSourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
				{
					return MakeExecutionResult(FString::Printf(TEXT("Failed to write source backup '%s'."), *BackupSourcePath), StructuredContent, true);
				}
			}

			if (!FFileHelper::SaveStringToFile(SourceText, *SourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				return MakeExecutionResult(FString::Printf(TEXT("Failed to write source file '%s'."), *SourcePath), StructuredContent, true);
			}

			const FString SourceHashAfter = HashTextForManifest(SourceText);
			StructuredContent->SetStringField(TEXT("sourceHashAfter"), SourceHashAfter);
			StructuredContent->SetStringField(TEXT("backupDirectory"), BackupDirectory);
			StructuredContent->SetStringField(TEXT("backupSourcePath"), BackupSourcePath);
			StructuredContent->SetStringField(TEXT("afterSourcePath"), AfterSourcePath);

				TSharedPtr<FJsonObject> ManifestObject = MakeShared<FJsonObject>();
				ManifestObject->SetStringField(TEXT("action"), TEXT("mcp_apply_scaffold"));
				ManifestObject->SetNumberField(TEXT("schemaVersion"), GUnrealMcpExtensionManifestSchemaVersion);
				ManifestObject->SetStringField(TEXT("manifestSchema"), GetUnrealMcpExtensionManifestSchemaName());
				ManifestObject->SetStringField(TEXT("sessionId"), ExtensionSessionId);
				ManifestObject->SetStringField(TEXT("toolName"), ToolName);
				ManifestObject->SetStringField(TEXT("toolId"), SanitizeMcpToolIdForPath(ToolName));
				ManifestObject->SetStringField(TEXT("scaffoldDir"), ScaffoldDirectory);
				ManifestObject->SetStringField(TEXT("sourcePath"), SourcePath);
				ManifestObject->SetStringField(TEXT("backupDirectory"), BackupDirectory);
			ManifestObject->SetStringField(TEXT("backupSourcePath"), BackupSourcePath);
			ManifestObject->SetStringField(TEXT("afterSourcePath"), AfterSourcePath);
				ManifestObject->SetStringField(TEXT("sourceHashBefore"), SourceHashBefore);
				ManifestObject->SetStringField(TEXT("sourceHashAfter"), SourceHashAfter);
				ManifestObject->SetStringField(TEXT("appliedAtUtc"), FDateTime::UtcNow().ToIso8601());
				ManifestObject->SetNumberField(TEXT("conflictCount"), ConflictCount);
				ManifestObject->SetNumberField(TEXT("missingAnchorCount"), MissingAnchorCount);
				ManifestObject->SetObjectField(TEXT("conflictPolicy"), ConflictPolicy);
				ManifestObject->SetArrayField(TEXT("changes"), Changes);

			if (bCreateBackup)
			{
				FString ManifestFailure;
				const FString ManifestPath = FPaths::Combine(BackupDirectory, TEXT("Manifest.json"));
				if (!SaveJsonObjectToFile(ManifestObject, ManifestPath, ManifestFailure)
					|| !SaveJsonObjectToFile(ManifestObject, GetLatestMcpExtensionManifestPath(), ManifestFailure))
				{
					return MakeExecutionResult(ManifestFailure, StructuredContent, true);
				}
				StructuredContent->SetStringField(TEXT("manifestPath"), ManifestPath);
				StructuredContent->SetStringField(TEXT("latestManifestPath"), GetLatestMcpExtensionManifestPath());
			}

			return MakeExecutionResult(
				FString::Printf(TEXT("Applied scaffold for %s. Backup: %s"), *ToolName, *BackupDirectory),
				StructuredContent,
				false);
		}

}
