#include "UnrealMcpModule.h"

#include "UnrealMcpPythonToolBridge.h"
#include "UnrealMcpActorTools.h"
#include "UnrealMcpAutomationTools.h"
#include "UnrealMcpBlueprintTools.h"
#include "UnrealMcpCodeTools.h"
#include "UnrealMcpDiagnosticsTools.h"
#include "UnrealMcpEditorTools.h"
#include "UnrealMcpMaterialInstanceTools.h"
#include "UnrealMcpMemoryTools.h"
#include "UnrealMcpPieSmokeTools.h"
#include "UnrealMcpScaffoldTools.h"
#include "UnrealMcpSelfExtensionTools.h"
#include "UnrealMcpSkillTools.h"
#include "UnrealMcpTaskAtlasService.h"
#include "UnrealMcpTaskAtlasTools.h"
#include "UnrealMcpToolExecutionGuard.h"
#include "UnrealMcpToolHandlerRegistry.h"
#include "UnrealMcpToolRegistry.h"
#include "UnrealMcpUserToolRegistry.h"
#include "UnrealMcpWidgetTools.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnrealMcp
{
	FUnrealMcpExecutionResult MakeExecutionResult(
		const FString& Text,
		const TSharedPtr<FJsonObject>& StructuredContent = nullptr,
		bool bIsError = false);
	FString GetMcpExtensionLockPath();
	bool TryAcquireExtensionSessionLock(
		const FString& Owner,
		const FString& Reason,
		int32 TtlSeconds,
		bool bForce,
		FString& OutSessionId,
		TSharedPtr<FJsonObject>& OutLockObject,
		FString& OutFailureReason);
	bool ReleaseExtensionSessionLock(const FString& SessionId, bool bForce, FString& OutFailureReason);

	namespace TaskAtlasMcpWrapper
	{
		FString GetStringArgument(const FJsonObject& Arguments, const FString& FieldName, const FString& DefaultValue = FString())
		{
			FString Value = DefaultValue;
			Arguments.TryGetStringField(FieldName, Value);
			return Value.TrimStartAndEnd();
		}

		bool GetBoolArgument(const FJsonObject& Arguments, const FString& FieldName, bool bDefaultValue)
		{
			bool bValue = bDefaultValue;
			Arguments.TryGetBoolField(FieldName, bValue);
			return bValue;
		}

		TArray<TSharedPtr<FJsonValue>> MakeStringArray(const TArray<FString>& Values)
		{
			TArray<TSharedPtr<FJsonValue>> JsonValues;
			JsonValues.Reserve(Values.Num());
			for (const FString& Value : Values)
			{
				JsonValues.Add(MakeShared<FJsonValueString>(Value));
			}
			return JsonValues;
		}

		TSharedPtr<FJsonObject> MakeDeleteContent(
			const FString& ToolName,
			const FString& Outcome,
			const FString& RemovedDir,
			bool bWasStaleEntry,
			int32 ReloadRemovedCount,
			int32 ReloadBeforeCount,
			int32 ReloadAfterCount,
			const TArray<FString>& RejectedReasons,
			const FString& FailureDiagnosticPath,
			const FString& ErrorCode,
			const FString& ErrorMessage)
		{
			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetStringField(TEXT("action"), TEXT("task_atlas_delete_made_tool"));
			Content->SetStringField(TEXT("toolName"), ToolName);
			Content->SetStringField(TEXT("outcome"), Outcome);
			Content->SetStringField(TEXT("removedDir"), RemovedDir);
			Content->SetBoolField(TEXT("wasStaleEntry"), bWasStaleEntry);
			Content->SetNumberField(TEXT("reloadRemovedCount"), ReloadRemovedCount);
			Content->SetNumberField(TEXT("reloadBeforeCount"), ReloadBeforeCount);
			Content->SetNumberField(TEXT("reloadAfterCount"), ReloadAfterCount);
			Content->SetArrayField(TEXT("rejectedReasons"), MakeStringArray(RejectedReasons));
			Content->SetStringField(TEXT("failureDiagnosticPath"), FailureDiagnosticPath);
			Content->SetStringField(TEXT("errorCode"), ErrorCode);
			Content->SetStringField(TEXT("errorMessage"), ErrorMessage);
			return Content;
		}

		bool TryParseOverrideStepArgs(
			const FJsonObject& Arguments,
			UnrealMcp::TaskAtlasService::FMakeCompositeRequest& Request,
			FUnrealMcpExecutionResult& OutResult)
		{
			const TArray<TSharedPtr<FJsonValue>>* OverrideValues = nullptr;
			if (!Arguments.TryGetArrayField(TEXT("overrideStepArgs"), OverrideValues) || !OverrideValues)
			{
				return true;
			}

			for (const TSharedPtr<FJsonValue>& Value : *OverrideValues)
			{
				const TSharedPtr<FJsonObject> OverrideObject = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
				if (!OverrideObject.IsValid())
				{
					OutResult = MakeExecutionResult(TEXT("overrideStepArgs entries must be JSON objects."), nullptr, true);
					return false;
				}

				double OrdinalDouble = -1.0;
				FString ArgumentsJson;
				if (!OverrideObject->TryGetNumberField(TEXT("ordinal"), OrdinalDouble)
					|| OrdinalDouble < 0.0
					|| !OverrideObject->TryGetStringField(TEXT("argumentsJson"), ArgumentsJson)
					|| ArgumentsJson.TrimStartAndEnd().IsEmpty())
				{
					OutResult = MakeExecutionResult(TEXT("overrideStepArgs entries require ordinal >= 0 and non-empty argumentsJson."), nullptr, true);
					return false;
				}

				TSharedPtr<FJsonObject> ParsedArguments;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ArgumentsJson);
				if (!FJsonSerializer::Deserialize(Reader, ParsedArguments) || !ParsedArguments.IsValid())
				{
					OutResult = MakeExecutionResult(TEXT("overrideStepArgs argumentsJson must parse as a JSON object."), nullptr, true);
					return false;
				}

				Request.OverrideStepArgs.Add(FString::FromInt(static_cast<int32>(OrdinalDouble)), ParsedArguments);
			}
			return true;
		}

		const TCHAR* DeleteOutcomeToString(UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome Outcome)
		{
			using UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome;
			switch (Outcome)
			{
			case EDeleteMadeToolOutcome::Deleted: return TEXT("Deleted");
			case EDeleteMadeToolOutcome::DryRun: return TEXT("DryRun");
			case EDeleteMadeToolOutcome::NotFound: return TEXT("NotFound");
			case EDeleteMadeToolOutcome::ReloadRejected: return TEXT("ReloadRejected");
			case EDeleteMadeToolOutcome::Failed: return TEXT("Failed");
			case EDeleteMadeToolOutcome::Refused:
			default: return TEXT("Refused");
			}
		}

		bool IsMakeError(UnrealMcp::TaskAtlasService::EMakeCompositeOutcome Outcome)
		{
			using UnrealMcp::TaskAtlasService::EMakeCompositeOutcome;
			return Outcome == EMakeCompositeOutcome::StagingFailed || Outcome == EMakeCompositeOutcome::ReloadRejected;
		}

		bool IsDeleteError(UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome Outcome)
		{
			using UnrealMcp::TaskAtlasService::EDeleteMadeToolOutcome;
			return Outcome == EDeleteMadeToolOutcome::ReloadRejected || Outcome == EDeleteMadeToolOutcome::Failed;
		}

		bool IsPromoteError(UnrealMcp::TaskAtlasService::EPromoteToRagOutcome Outcome)
		{
			using UnrealMcp::TaskAtlasService::EPromoteToRagOutcome;
			return Outcome == EPromoteToRagOutcome::RefreshFailed || Outcome == EPromoteToRagOutcome::Failed;
		}

		bool IsSmokeError(UnrealMcp::TaskAtlasService::ESmokeOutcome Outcome)
		{
			using UnrealMcp::TaskAtlasService::ESmokeOutcome;
			return Outcome == ESmokeOutcome::Failed || Outcome == ESmokeOutcome::ReloadRejected || Outcome == ESmokeOutcome::FailedToExecute;
		}

		TSharedPtr<FJsonObject> MadeToolEntryToJson(
			const UnrealMcp::TaskAtlasService::FMadeToolEntry& Entry,
			bool bIncludeFailureMarkers)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("toolName"), Entry.ToolName);
			Object->SetStringField(TEXT("scaffoldDir"), Entry.ScaffoldDir);
			Object->SetStringField(TEXT("compositeKind"), Entry.CompositeKind);
			Object->SetStringField(TEXT("replayStatus"), Entry.ReplayStatus);
			Object->SetStringField(TEXT("createdUtc"), Entry.CreatedUtc.ToIso8601());
			Object->SetStringField(TEXT("sourceTaskId"), Entry.SourceTaskId);
			Object->SetStringField(TEXT("generator"), Entry.Generator);
			Object->SetStringField(TEXT("pythonHandlerSha256"), Entry.PythonHandlerSha256);
			Object->SetBoolField(TEXT("loadedInUserRegistry"), Entry.bLoadedInUserRegistry);
			Object->SetBoolField(TEXT("hasFailureMarker"), bIncludeFailureMarkers && Entry.bHasFailureMarker);
			Object->SetStringField(TEXT("failureDiagnosticPath"), bIncludeFailureMarkers ? Entry.FailureDiagnosticPath : FString());
			return Object;
		}

		TSharedPtr<FJsonObject> UserToolViewToJson(
			const UnrealMcp::TaskAtlasService::FUserToolView& View,
			bool bIncludePythonSha,
			bool bIncludeToolJson)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("toolName"), View.ToolName);
			Object->SetStringField(TEXT("scaffoldDir"), View.ScaffoldDir);
			Object->SetStringField(TEXT("generator"), View.Generator);
			Object->SetStringField(TEXT("pythonSha"), bIncludePythonSha ? View.PythonSha : FString());
			Object->SetStringField(TEXT("lifecycleState"), View.LifecycleState);
			Object->SetStringField(TEXT("sourceKind"), View.SourceKind);
			Object->SetStringField(TEXT("sourceTaskId"), View.SourceTaskId);
			Object->SetStringField(TEXT("toolJsonPath"), View.ToolJsonPath);
			Object->SetStringField(TEXT("pythonPath"), View.PythonPath);
			Object->SetBoolField(TEXT("loaded"), View.bLoaded);
			Object->SetBoolField(TEXT("rejected"), View.bRejected);
			Object->SetStringField(TEXT("rejectionReason"), View.RejectionReason);
			if (bIncludeToolJson)
			{
				TSharedPtr<FJsonObject> SanitizedToolJson = MakeShared<FJsonObject>();
				SanitizedToolJson->SetStringField(TEXT("generator"), View.Generator);
				SanitizedToolJson->SetStringField(TEXT("sourceKind"), View.SourceKind);
				SanitizedToolJson->SetStringField(TEXT("sourceTaskId"), View.SourceTaskId);
				SanitizedToolJson->SetStringField(TEXT("pythonHandlerSha256"), bIncludePythonSha ? View.PythonSha : FString());
				Object->SetObjectField(TEXT("toolJson"), SanitizedToolJson);
			}
			return Object;
		}

		bool TryExecute(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult)
		{
			if (ToolName == TEXT("unreal.task_atlas_make_composite"))
			{
				UnrealMcp::TaskAtlasService::FMakeCompositeRequest Request;
				Request.TaskId = GetStringArgument(Arguments, TEXT("taskId"));
				Request.bPreferDocumentOnly = GetBoolArgument(Arguments, TEXT("preferDocumentOnly"), false);
				Request.bForceWriteEvenIfBlocked = GetBoolArgument(Arguments, TEXT("forceWriteEvenIfBlocked"), false);
				if (!TryParseOverrideStepArgs(Arguments, Request, OutResult))
				{
					return true;
				}

				const UnrealMcp::TaskAtlasService::FMakeCompositeResult Result = UnrealMcp::TaskAtlasService::MakeComposite(Request);
				FString OutcomeText = TEXT("unknown");
				if (Result.StructuredContent.IsValid())
				{
					Result.StructuredContent->TryGetStringField(TEXT("outcome"), OutcomeText);
				}
				const FString Text = Result.ErrorMessage.IsEmpty()
					? FString::Printf(TEXT("Task Atlas make composite outcome: %s"), *OutcomeText)
					: Result.ErrorMessage;
				OutResult = MakeExecutionResult(Text, Result.StructuredContent, IsMakeError(Result.Outcome));
				return true;
			}

			if (ToolName == TEXT("unreal.task_atlas_delete_made_tool"))
			{
				const FString TargetToolName = GetStringArgument(Arguments, TEXT("toolName"));
				const bool bConfirm = GetBoolArgument(Arguments, TEXT("confirm"), false);
				if (!bConfirm)
				{
					TSharedPtr<FJsonObject> Content = MakeDeleteContent(
						TargetToolName,
						TEXT("Refused"),
						FString(),
						false,
						0,
						0,
						0,
						TArray<FString>(),
						FString(),
						TEXT("confirm_required"),
						TEXT("Set confirm=true to delete a Task Atlas generated user tool."));
					OutResult = MakeExecutionResult(TEXT("Refused to delete generated user tool: confirm=true is required."), Content, false);
					return true;
				}

				const bool bDryRun = GetBoolArgument(Arguments, TEXT("dryRun"), false);
				if (bDryRun)
				{
					FString TargetDir;
					bool bLoaded = false;
					for (const UnrealMcp::TaskAtlasService::FMadeToolEntry& Entry : UnrealMcp::TaskAtlasService::ListMadeTools())
					{
						if (Entry.ToolName == TargetToolName)
						{
							TargetDir = Entry.ScaffoldDir;
							bLoaded = Entry.bLoadedInUserRegistry;
							break;
						}
					}
					const FString Outcome = TargetDir.IsEmpty() ? FString(TEXT("NotFound")) : FString(TEXT("DryRun"));
					TSharedPtr<FJsonObject> Content = MakeDeleteContent(
						TargetToolName,
						Outcome,
						TargetDir,
						!TargetDir.IsEmpty() && !bLoaded,
						0,
						0,
						0,
						TArray<FString>(),
						FString(),
						TargetDir.IsEmpty() ? FString(TEXT("not_found")) : FString(),
						TargetDir.IsEmpty() ? FString(TEXT("Task Atlas generated user tool was not found.")) : FString());
					OutResult = MakeExecutionResult(
						TargetDir.IsEmpty() ? TEXT("Task Atlas generated user tool was not found.") : TEXT("Dry-run: generated user tool would be deleted."),
						Content,
						false);
					return true;
				}

				const UnrealMcp::TaskAtlasService::FDeleteMadeToolResult Result = UnrealMcp::TaskAtlasService::DeleteMadeTool(TargetToolName);
				const FString Text = Result.ErrorMessage.IsEmpty()
					? FString::Printf(TEXT("Task Atlas delete made tool outcome: %s"), DeleteOutcomeToString(Result.Outcome))
					: Result.ErrorMessage;
				OutResult = MakeExecutionResult(Text, Result.StructuredContent, IsDeleteError(Result.Outcome));
				return true;
			}

			if (ToolName == TEXT("unreal.task_atlas_list_made_tools"))
			{
				const bool bIncludeStale = GetBoolArgument(Arguments, TEXT("includeStale"), true);
				const bool bIncludeFailureMarkers = GetBoolArgument(Arguments, TEXT("includeFailureMarkers"), true);
				const FString SourceTaskId = GetStringArgument(Arguments, TEXT("sourceTaskId"));

				TArray<TSharedPtr<FJsonValue>> ToolValues;
				for (const UnrealMcp::TaskAtlasService::FMadeToolEntry& Entry : UnrealMcp::TaskAtlasService::ListMadeTools())
				{
					if (!bIncludeStale && !Entry.bLoadedInUserRegistry)
					{
						continue;
					}
					if (!SourceTaskId.IsEmpty() && Entry.SourceTaskId != SourceTaskId)
					{
						continue;
					}
					ToolValues.Add(MakeShared<FJsonValueObject>(MadeToolEntryToJson(Entry, bIncludeFailureMarkers)));
				}

				TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
				Content->SetStringField(TEXT("action"), TEXT("task_atlas_list_made_tools"));
				Content->SetNumberField(TEXT("count"), ToolValues.Num());
				Content->SetArrayField(TEXT("madeTools"), ToolValues);
				OutResult = MakeExecutionResult(FString::Printf(TEXT("Listed %d Task Atlas generated user tools."), ToolValues.Num()), Content, false);
				return true;
			}

			if (ToolName == TEXT("unreal.task_atlas_promote_to_rag"))
			{
				UnrealMcp::TaskAtlasService::FPromoteToRagRequest Request;
				Request.TaskId = GetStringArgument(Arguments, TEXT("taskId"));
				Request.bDryRun = GetBoolArgument(Arguments, TEXT("dryRun"), false);

				const bool bRefreshIndex = GetBoolArgument(Arguments, TEXT("refreshIndex"), true);
				if (!Request.bDryRun && !bRefreshIndex)
				{
					TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
					Content->SetStringField(TEXT("action"), TEXT("task_atlas_promote_to_rag"));
					Content->SetStringField(TEXT("taskId"), Request.TaskId);
					Content->SetStringField(TEXT("outcome"), TEXT("Refused"));
					Content->SetBoolField(TEXT("dryRun"), false);
					Content->SetBoolField(TEXT("refreshIndex"), false);
					Content->SetStringField(TEXT("errorCode"), TEXT("refresh_index_false_unsupported"));
					Content->SetStringField(TEXT("errorMessage"), TEXT("TaskAtlasService real promotion always refreshes the knowledge index in this chunk."));
					OutResult = MakeExecutionResult(TEXT("Refused: refreshIndex=false is not supported for real promotion in this wrapper."), Content, false);
					return true;
				}

				const UnrealMcp::TaskAtlasService::FPromoteToRagResult Result = UnrealMcp::TaskAtlasService::PromoteToRag(Request);
				if (Result.StructuredContent.IsValid())
				{
					Result.StructuredContent->SetBoolField(TEXT("refreshIndex"), bRefreshIndex);
				}
				const FString Text = Result.ErrorMessage.IsEmpty()
					? TEXT("Task Atlas RAG promotion completed.")
					: Result.ErrorMessage;
				OutResult = MakeExecutionResult(Text, Result.StructuredContent, IsPromoteError(Result.Outcome));
				return true;
			}

			if (ToolName == TEXT("unreal.task_atlas_smoke_made_tool"))
			{
				UnrealMcp::TaskAtlasService::FSmokeRequest Request;
				Request.ToolName = GetStringArgument(Arguments, TEXT("toolName"));
				Request.bDryRun = GetBoolArgument(Arguments, TEXT("dryRun"), true);

				const UnrealMcp::TaskAtlasService::FSmokeResult Result = UnrealMcp::TaskAtlasService::SmokeMadeTool(Request);
				const FString Text = Result.SmokeText.IsEmpty()
					? Result.ErrorMessage
					: Result.SmokeText;
				OutResult = MakeExecutionResult(Text, Result.StructuredContent, IsSmokeError(Result.Outcome));
				return true;
			}

			if (ToolName == TEXT("unreal.user_registry_introspect"))
			{
				const bool bIncludeToolJson = GetBoolArgument(Arguments, TEXT("includeToolJson"), false);
				const bool bIncludePythonSha = GetBoolArgument(Arguments, TEXT("includePythonSha"), true);
				const FString FilterToolName = GetStringArgument(Arguments, TEXT("toolName"));

				TArray<TSharedPtr<FJsonValue>> ToolValues;
				TArray<TSharedPtr<FJsonValue>> RejectedValues;
				for (const UnrealMcp::TaskAtlasService::FUserToolView& View : UnrealMcp::TaskAtlasService::IntrospectUserRegistry())
				{
					if (!FilterToolName.IsEmpty() && View.ToolName != FilterToolName)
					{
						continue;
					}

					TSharedPtr<FJsonObject> Object = UserToolViewToJson(View, bIncludePythonSha, bIncludeToolJson);
					if (View.bRejected)
					{
						RejectedValues.Add(MakeShared<FJsonValueObject>(Object));
					}
					else
					{
						ToolValues.Add(MakeShared<FJsonValueObject>(Object));
					}
				}

				TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
				Content->SetStringField(TEXT("action"), TEXT("user_registry_introspect"));
				Content->SetNumberField(TEXT("count"), ToolValues.Num());
				UnrealMcp::UserRegistry::InitializeUserToolRegistry();
				Content->SetStringField(TEXT("userToolsRootDir"), UnrealMcp::UserRegistry::GetUserToolsRootDir());
				Content->SetArrayField(TEXT("tools"), ToolValues);
				Content->SetArrayField(TEXT("rejected"), RejectedValues);
				OutResult = MakeExecutionResult(FString::Printf(TEXT("Inspected %d loaded user registry tools."), ToolValues.Num()), Content, false);
				return true;
			}

			return false;
		}
	}

	namespace
	{
		class FScopedSkillPromotionLock
		{
		public:
			FScopedSkillPromotionLock(const FString& ToolName, const FJsonObject& Arguments)
			{
				bool bSkipLock = false;
				bool bForceLock = false;
				double TtlSecondsDouble = 900.0;
				FString Owner = TEXT("Unreal MCP Chat");
				Arguments.TryGetBoolField(TEXT("skipLock"), bSkipLock);
				Arguments.TryGetBoolField(TEXT("forceLock"), bForceLock);
				Arguments.TryGetNumberField(TEXT("lockTtlSeconds"), TtlSecondsDouble);
				Arguments.TryGetStringField(TEXT("lockOwner"), Owner);

				if (bSkipLock)
				{
					bAcquired = true;
					bOwnsLock = false;
					return;
				}

				const int32 TtlSeconds = FMath::Clamp(static_cast<int32>(TtlSecondsDouble), 30, 86400);
				const FString Reason = FString::Printf(TEXT("Executing %s"), *ToolName);
				bAcquired = TryAcquireExtensionSessionLock(Owner, Reason, TtlSeconds, bForceLock, SessionId, LockObject, FailureReason);
				bOwnsLock = bAcquired;
			}

			~FScopedSkillPromotionLock()
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
	}
}

FUnrealMcpExecutionResult FUnrealMcpModule::ExecuteToolFromEditorUI(const FString& ToolName, const FJsonObject& Arguments) const
{
	return ExecuteTool(ToolName, Arguments);
}

FUnrealMcpExecutionResult FUnrealMcpModule::ExecuteTool(const FString& ToolName, const FJsonObject& Arguments) const
{
	const FString RegisteredHandlerName = UnrealMcp::ResolveToolHandlerName(ToolName);

	TSharedPtr<FJsonObject> PreflightBeforeExecution = UnrealMcp::BuildToolExecutionPreflight(ToolName, Arguments);
	FUnrealMcpExecutionResult Result = ExecuteToolInternal(RegisteredHandlerName, Arguments);
	UnrealMcp::AttachToolExecutionCheck(ToolName, Arguments, PreflightBeforeExecution, Result);
	return Result;
}

FUnrealMcpExecutionResult FUnrealMcpModule::ExecuteToolInternal(const FString& ToolName, const FJsonObject& Arguments) const
{
	const UnrealMcp::FToolHandlerRegistryEntry* HandlerEntry = UnrealMcp::FindToolHandlerRegistryEntry(ToolName);
	if (!HandlerEntry)
	{
		TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
		StructuredContent->SetStringField(TEXT("action"), TEXT("handler_dispatch_failed"));
		StructuredContent->SetStringField(TEXT("handlerName"), ToolName);
		StructuredContent->SetStringField(TEXT("reason"), TEXT("handler_not_registered"));
		return UnrealMcp::MakeExecutionResult(
			FString::Printf(TEXT("Handler '%s' is not registered in the explicit MCP handler registry."), *ToolName),
			StructuredContent,
			true);
	}

	if (HandlerEntry->ImplementationTrack == UnrealMcp::EToolImplementationTrack::Python)
	{
		return UnrealMcp::UnrealMcpPythonToolBridge::ExecutePythonRegisteredTool(*HandlerEntry, Arguments);
	}

	FUnrealMcpExecutionResult CategoryResult;
	if (UnrealMcp::TaskAtlasMcpWrapper::TryExecute(ToolName, Arguments, CategoryResult))
	{
		return CategoryResult;
	}

	const FString& Category = HandlerEntry->Category;
	if (Category == TEXT("editor"))
	{
		if (UnrealMcp::TryExecuteEditorTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("actors"))
	{
		if (UnrealMcp::TryExecuteActorTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("blueprint"))
	{
		if (UnrealMcp::TryExecuteBlueprintTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("code"))
	{
		if (UnrealMcp::TryExecuteCodeTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("widget"))
	{
		if (UnrealMcp::TryExecuteWidgetTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("material"))
	{
		if (UnrealMcp::TryExecuteMaterialInstanceTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("scaffold"))
	{
		if (UnrealMcp::TryExecuteScaffoldTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("memory"))
	{
		if (UnrealMcp::TryExecuteMemoryTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("skills"))
	{
		if (UnrealMcp::TryExecuteSkillTool(
			ToolName,
			Arguments,
			[&ToolName](const FJsonObject& ToolArguments)
			{
				UnrealMcp::FScopedSkillPromotionLock ScopedLock(ToolName, ToolArguments);
				if (!ScopedLock.IsAcquired())
				{
					return UnrealMcp::MakeExecutionResult(ScopedLock.GetFailureReason(), ScopedLock.MakeStructuredContent(TEXT("mcp_extension_lock_failed")), true);
				}
				return UnrealMcp::SkillPromoteDraft(ToolArguments);
			},
			CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("self-extension"))
	{
		TArray<TSharedPtr<FJsonValue>> ToolDefinitions;
		AppendToolDefinitions(ToolDefinitions);
		if (UnrealMcp::TryExecuteSelfExtensionTool(
				ToolName,
				Arguments,
				ToolDefinitions,
				[this](const FJsonObject& ToolArguments) { return RunMcpToolTest(ToolArguments); },
				[this](const FJsonObject& ToolArguments) { return RunMcpTestSuite(ToolArguments); },
				[this](const FJsonObject& ToolArguments) { return RunMcpExtensionPipeline(ToolArguments); },
				[this](const FJsonObject& ToolArguments) { return RunWorkflow(ToolArguments); },
				CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("task-atlas"))
	{
		if (UnrealMcp::TryExecuteTaskAtlasTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}
	else if (Category == TEXT("verification"))
	{
		// Verification is split across diagnostics and automation handlers; first matching tool branch wins.
		if (UnrealMcp::TryExecuteDiagnosticsTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
		if (UnrealMcp::TryExecuteAutomationTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
		if (UnrealMcp::TryExecutePieSmokeTool(ToolName, Arguments, CategoryResult))
		{
			return CategoryResult;
		}
	}

	TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
	StructuredContent->SetStringField(TEXT("action"), TEXT("handler_dispatch_failed"));
	StructuredContent->SetStringField(TEXT("handlerName"), ToolName);
	StructuredContent->SetStringField(TEXT("category"), Category);
	StructuredContent->SetStringField(TEXT("sourceFile"), HandlerEntry->SourceFile);
	StructuredContent->SetStringField(TEXT("reason"), TEXT("category_dispatcher_did_not_handle_registered_handler"));
	return UnrealMcp::MakeExecutionResult(
		FString::Printf(TEXT("Handler '%s' is registered under category '%s', but that category dispatcher did not handle it."), *ToolName, *Category),
		StructuredContent,
		true);
}
