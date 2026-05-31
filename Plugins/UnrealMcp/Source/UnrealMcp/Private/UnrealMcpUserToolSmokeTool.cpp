// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.

#include "UnrealMcpExtensionLifecycle.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpPythonToolBridge.h"
#include "UnrealMcpToolHandlerRegistry.h"
#include "UnrealMcpUserToolLock.h"
#include "UnrealMcpUserToolRegistry.h"

#include "Async/Async.h"
#include "Async/Future.h"
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

		class FUserToolSmokeLockRelease
		{
		public:
			explicit FUserToolSmokeLockRelease(const FString& InToolName)
				: ToolName(InToolName)
			{
			}

			~FUserToolSmokeLockRelease()
			{
				if (bSameToolLockAcquired)
				{
					UserToolLock::ReleaseSameToolExecution(ToolName);
				}
				if (bSharedLockAcquired)
				{
					UserToolLock::ReleaseShared();
				}
			}

			void MarkSharedLockAcquired()
			{
				bSharedLockAcquired = true;
			}

			void MarkSameToolLockAcquired()
			{
				bSameToolLockAcquired = true;
			}

		private:
			FString ToolName;
			bool bSharedLockAcquired = false;
			bool bSameToolLockAcquired = false;
		};

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

		bool UserToolSmokeExecutePythonOnGameThread(
			const FToolHandlerRegistryEntry& HandlerEntry,
			TSharedPtr<FJsonObject> SmokeArgs,
			double TimeoutSeconds,
			FUnrealMcpExecutionResult& OutExecutionResult)
		{
			if (IsInGameThread())
			{
				OutExecutionResult = UnrealMcpPythonToolBridge::ExecutePythonRegisteredTool(HandlerEntry, *SmokeArgs);
				return true;
			}

			TSharedRef<TPromise<FUnrealMcpExecutionResult>, ESPMode::ThreadSafe> Promise = MakeShared<TPromise<FUnrealMcpExecutionResult>, ESPMode::ThreadSafe>();
			TFuture<FUnrealMcpExecutionResult> Future = Promise->GetFuture();
			AsyncTask(ENamedThreads::GameThread, [HandlerEntry, SmokeArgs, Promise]() mutable
			{
				// Defensive path only: smoke is always invoked on the game thread in practice (ExecuteToolFromEditorUI is synchronous, MCP dispatches to game thread). The shared lock is held on the calling thread while execution marshals to the game thread, so thread_local lock depth does not cover the marshalled execution. Safe because smoke runs dryRun (reload takes shared only). If a non-game-thread caller is ever added AND a composite triggers reload-apply reentrancy, move lock acquisition into the game-thread task.
				Promise->SetValue(UnrealMcpPythonToolBridge::ExecutePythonRegisteredTool(HandlerEntry, *SmokeArgs));
			});

			if (!Future.WaitFor(FTimespan::FromSeconds(TimeoutSeconds)))
			{
				return false;
			}

			OutExecutionResult = Future.Get();
			return true;
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
			FUserToolSmokeLockRelease LockRelease(ToolName);
			LockRelease.MarkSharedLockAcquired();

			const UserRegistry::FUserToolEntry* UserToolEntry = UserRegistry::FindUserTool(ToolName);
			if (!UserToolEntry)
			{
				return MakeExecutionResult(
					FString::Printf(TEXT("User tool '%s' is not loaded."), *ToolName),
					UserToolSmokeMakeStructuredContent(ToolName, Extension::ELifecycleState::SmokeFailed, false, FPlatformTime::Seconds() - StartSeconds, FString(), TEXT("user tool not loaded")),
					true);
			}

			if (!UserToolLock::SerializeSameToolExecution(ToolName, TimeoutSeconds))
			{
				return MakeExecutionResult(
					FString::Printf(TEXT("Timed out waiting to serialize user tool smoke for '%s'."), *ToolName),
					UserToolSmokeMakeStructuredContent(ToolName, Extension::ELifecycleState::SmokeFailed, false, FPlatformTime::Seconds() - StartSeconds, FString(), TEXT("timed out waiting for same-tool execution lock")),
					true);
			}
			LockRelease.MarkSameToolLockAcquired();

			FToolHandlerRegistryEntry HandlerEntry = UserToolSmokeMakeHandlerEntry(*UserToolEntry);
			TSharedPtr<FJsonObject> SmokeArgs = UserToolSmokeMakeArguments(Arguments);
			FUnrealMcpExecutionResult ExecutionResult;
			if (!UserToolSmokeExecutePythonOnGameThread(HandlerEntry, SmokeArgs, TimeoutSeconds, ExecutionResult))
			{
				return MakeExecutionResult(
					FString::Printf(TEXT("Smoke test timed out for user tool '%s' after %.2f seconds."), *ToolName, TimeoutSeconds),
					UserToolSmokeMakeStructuredContent(ToolName, Extension::ELifecycleState::SmokeFailed, false, FPlatformTime::Seconds() - StartSeconds, FString(), TEXT("timed out")),
					true);
			}

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
