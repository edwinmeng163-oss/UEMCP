// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.

#include "UnrealMcpExtensionLifecycle.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpPythonToolBridge.h"
#include "UnrealMcpToolHandlerRegistry.h"
#include "UnrealMcpUserToolLock.h"
#include "UnrealMcpUserToolRegistry.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealMcp
{
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);

	namespace
	{
		FString UserToolSmokePreviewString(const FString& Text, int32 MaxCharacters = 500)
		{
			const FString Trimmed = Text.TrimStartAndEnd();
			return Trimmed.Len() <= MaxCharacters ? Trimmed : Trimmed.Left(MaxCharacters) + TEXT("...");
		}

		FString UserToolSmokeJsonPreview(const FUnrealMcpExecutionResult& Result)
		{
			if (!Result.StructuredContent.IsValid())
			{
				return UserToolSmokePreviewString(Result.Text);
			}

			FString JsonText;
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonText);
			FJsonSerializer::Serialize(Result.StructuredContent.ToSharedRef(), Writer);
			return UserToolSmokePreviewString(JsonText);
		}

		FString UserToolSmokeRelativeHandlerPath(const UserRegistry::FUserToolEntry& UserToolEntry)
		{
			FString NormalizedScaffoldDir = UserToolEntry.ScaffoldDir;
			FPaths::NormalizeFilename(NormalizedScaffoldDir);
			NormalizedScaffoldDir.RemoveFromEnd(TEXT("/"));
			return FPaths::Combine(
				::UnrealMcp::Extension::UserPyToolsRelativeRoot,
				FPaths::GetCleanFilename(NormalizedScaffoldDir),
				TEXT("main.py"));
		}

		FToolHandlerRegistryEntry UserToolSmokeMakeHandlerEntry(const UserRegistry::FUserToolEntry& UserToolEntry)
		{
			FToolHandlerRegistryEntry HandlerEntry;
			HandlerEntry.HandlerName = UserToolEntry.ToolName;
			HandlerEntry.Category = TEXT("user");
			HandlerEntry.SourceFile = UserToolEntry.PythonHandlerPath;
			HandlerEntry.ImplementationTrack = EToolImplementationTrack::Python;
			HandlerEntry.PythonHandlerPath = UserToolSmokeRelativeHandlerPath(UserToolEntry);
			HandlerEntry.PythonHandlerSha256 = UserToolEntry.PythonHandlerSha256;
			HandlerEntry.PythonImportAllowList = UserToolEntry.ImportAllowlist;
			HandlerEntry.ToolNames.Add(UserToolEntry.ToolName);
			HandlerEntry.bLoadedFromUserRegistry = true;
			HandlerEntry.bUserToolLocksAlreadyHeld = true;
			return HandlerEntry;
		}

		TSharedPtr<FJsonObject> UserToolSmokeMakeArguments(const FJsonObject& Arguments)
		{
			TSharedPtr<FJsonObject> SmokeArgs = MakeShared<FJsonObject>();
			FString DryRunArgsText;
			if (Arguments.TryGetStringField(TEXT("dryRunArgs"), DryRunArgsText))
			{
				TSharedPtr<FJsonObject> ParsedDryRunArgs;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DryRunArgsText);
				if (FJsonSerializer::Deserialize(Reader, ParsedDryRunArgs) && ParsedDryRunArgs.IsValid())
				{
					SmokeArgs->Values = ParsedDryRunArgs->Values;
				}
			}
			const TSharedPtr<FJsonObject>* DryRunArgsObject = nullptr;
			if (SmokeArgs->Values.Num() == 0 && Arguments.TryGetObjectField(TEXT("dryRunArgs"), DryRunArgsObject) && DryRunArgsObject && (*DryRunArgsObject).IsValid())
			{
				SmokeArgs->Values = (*DryRunArgsObject)->Values;
			}
			SmokeArgs->SetBoolField(TEXT("dryRun"), true);
			return SmokeArgs;
		}

		TSharedPtr<FJsonObject> UserToolSmokeMakeStructuredContent(
			const FString& ToolName,
			Extension::ELifecycleState State,
			bool bCallableNow,
			double DurationSeconds,
			const FString& Preview,
			const FString& Error)
		{
			Extension::FToolLifecycle Lifecycle;
			Lifecycle.ToolName = ToolName;
			Lifecycle.ExtensionScope = Extension::EExtensionScope::User;
			Lifecycle.ImplementationTrack = Extension::EImplementationTrack::Python;
			Lifecycle.State = State;
			Lifecycle.bCallableNow = bCallableNow;
			Lifecycle.NextRequiredAction = State == Extension::ELifecycleState::SmokePassed ? FString() : Extension::ControlToolUserToolSmoke;
			Lifecycle.SourceKind = Extension::ESourceKind::UserRegistry;
			Lifecycle.HandlerKind = Extension::EHandlerKind::PythonBridge;

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), TEXT("mcp_user_tool_smoke"));
			StructuredContent->SetStringField(TEXT("toolName"), ToolName);
			StructuredContent->SetObjectField(TEXT("lifecycle"), Extension::BuildLifecycleJson(Lifecycle));
			StructuredContent->SetNumberField(TEXT("executionDurationSeconds"), DurationSeconds);
			if (!Preview.IsEmpty())
			{
				StructuredContent->SetStringField(TEXT("executionResultPreview"), Preview);
			}
			if (!Error.IsEmpty())
			{
				StructuredContent->SetStringField(TEXT("executionError"), Error);
			}
			return StructuredContent;
		}
	}

	namespace UnrealMcpUserToolSmokeTool
	{
		FUnrealMcpExecutionResult Execute(const FJsonObject& Arguments)
		{
			FString ToolName;
			if (!Arguments.TryGetStringField(TEXT("toolName"), ToolName) || ToolName.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(
					TEXT("toolName is required."),
					UserToolSmokeMakeStructuredContent(TEXT(""), Extension::ELifecycleState::SmokeFailed, false, 0.0, FString(), TEXT("toolName is required.")),
					true);
			}
			ToolName = ToolName.TrimStartAndEnd();

			double TimeoutSeconds = 10.0;
			Arguments.TryGetNumberField(TEXT("timeoutSeconds"), TimeoutSeconds);
			TimeoutSeconds = FMath::Max(0.1, TimeoutSeconds);

			const double StartSeconds = FPlatformTime::Seconds();
			if (!UserToolLock::TryAcquireShared(TimeoutSeconds))
			{
				return MakeExecutionResult(
					FString::Printf(TEXT("Timed out waiting for user registry execution lock for '%s'."), *ToolName),
					UserToolSmokeMakeStructuredContent(ToolName, Extension::ELifecycleState::SmokeFailed, false, FPlatformTime::Seconds() - StartSeconds, FString(), TEXT("timed out waiting for registry lock")),
					true);
			}

			const UserRegistry::FUserToolEntry* UserToolEntry = UserRegistry::FindUserTool(ToolName);
			if (!UserToolEntry)
			{
				UserToolLock::ReleaseShared();
				return MakeExecutionResult(
					FString::Printf(TEXT("User tool '%s' is not loaded."), *ToolName),
					UserToolSmokeMakeStructuredContent(ToolName, Extension::ELifecycleState::SmokeFailed, false, FPlatformTime::Seconds() - StartSeconds, FString(), TEXT("user tool not loaded")),
					true);
			}

			if (!UserToolLock::SerializeSameToolExecution(ToolName, TimeoutSeconds))
			{
				UserToolLock::ReleaseShared();
				return MakeExecutionResult(
					FString::Printf(TEXT("Timed out waiting to serialize user tool smoke for '%s'."), *ToolName),
					UserToolSmokeMakeStructuredContent(ToolName, Extension::ELifecycleState::SmokeFailed, false, FPlatformTime::Seconds() - StartSeconds, FString(), TEXT("timed out waiting for same-tool execution lock")),
					true);
			}

			FToolHandlerRegistryEntry HandlerEntry = UserToolSmokeMakeHandlerEntry(*UserToolEntry);
			TSharedPtr<FJsonObject> SmokeArgs = UserToolSmokeMakeArguments(Arguments);
			TFuture<FUnrealMcpExecutionResult> Future = Async(EAsyncExecution::ThreadPool, [HandlerEntry, SmokeArgs, ToolName]()
			{
				FUnrealMcpExecutionResult Result = UnrealMcpPythonToolBridge::ExecutePythonRegisteredTool(HandlerEntry, *SmokeArgs);
				UserToolLock::ReleaseSameToolExecution(ToolName);
				UserToolLock::ReleaseShared();
				return Result;
			});

			if (!Future.WaitFor(FTimespan::FromSeconds(TimeoutSeconds)))
			{
				return MakeExecutionResult(
					FString::Printf(TEXT("Smoke test timed out for user tool '%s' after %.2f seconds."), *ToolName, TimeoutSeconds),
					UserToolSmokeMakeStructuredContent(ToolName, Extension::ELifecycleState::SmokeFailed, false, FPlatformTime::Seconds() - StartSeconds, FString(), TEXT("timed out")),
					true);
			}

			const FUnrealMcpExecutionResult ExecutionResult = Future.Get();
			const double DurationSeconds = FPlatformTime::Seconds() - StartSeconds;
			const bool bSmokePassed = !ExecutionResult.bIsError;
			const FString Preview = UserToolSmokeJsonPreview(ExecutionResult);
			const FString Error = bSmokePassed ? FString() : ExecutionResult.Text;
			return MakeExecutionResult(
				FString::Printf(TEXT("User tool '%s' smoke %s."), *ToolName, bSmokePassed ? TEXT("passed") : TEXT("failed")),
				UserToolSmokeMakeStructuredContent(
					ToolName,
					bSmokePassed ? Extension::ELifecycleState::SmokePassed : Extension::ELifecycleState::SmokeFailed,
					bSmokePassed,
					DurationSeconds,
					Preview,
					Error),
				!bSmokePassed);
		}
	}
}
