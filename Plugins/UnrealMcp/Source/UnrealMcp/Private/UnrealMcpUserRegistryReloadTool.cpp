// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.

#include "UnrealMcpExtensionLifecycle.h"
#include "UnrealMcpModule.h"
#include "UnrealMcpUserToolLock.h"
#include "UnrealMcpUserToolRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace UnrealMcp
{
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);

	namespace
	{
		TArray<TSharedPtr<FJsonValue>> UserRegistryReloadMakeStringArray(const TArray<FString>& Values)
		{
			TArray<TSharedPtr<FJsonValue>> JsonValues;
			for (const FString& Value : Values)
			{
				JsonValues.Add(MakeShared<FJsonValueString>(Value));
			}
			return JsonValues;
		}

		TArray<TSharedPtr<FJsonValue>> UserRegistryReloadMakeRejectionArray(const UserRegistry::FReloadResult& ReloadResult)
		{
			TArray<TSharedPtr<FJsonValue>> JsonValues;
			for (const UserRegistry::FReloadResult::FRejection& Rejection : ReloadResult.RejectedTools)
			{
				TSharedPtr<FJsonObject> RejectionObject = MakeShared<FJsonObject>();
				RejectionObject->SetStringField(TEXT("toolName"), Rejection.ToolName);
				RejectionObject->SetStringField(TEXT("reason"), Rejection.Reason);
				JsonValues.Add(MakeShared<FJsonValueObject>(RejectionObject));
			}
			return JsonValues;
		}

		void UserRegistryReloadAddResultFields(const TSharedPtr<FJsonObject>& StructuredContent, const UserRegistry::FReloadResult& ReloadResult)
		{
			StructuredContent->SetNumberField(TEXT("beforeCount"), ReloadResult.BeforeCount);
			StructuredContent->SetNumberField(TEXT("afterCount"), ReloadResult.AfterCount);
			StructuredContent->SetArrayField(TEXT("added"), UserRegistryReloadMakeStringArray(ReloadResult.AddedTools));
			StructuredContent->SetArrayField(TEXT("updated"), UserRegistryReloadMakeStringArray(ReloadResult.UpdatedTools));
			StructuredContent->SetArrayField(TEXT("removed"), UserRegistryReloadMakeStringArray(ReloadResult.RemovedTools));
			StructuredContent->SetArrayField(TEXT("rejected"), UserRegistryReloadMakeRejectionArray(ReloadResult));
			StructuredContent->SetNumberField(TEXT("rejectedCount"), ReloadResult.RejectedTools.Num());
			StructuredContent->SetNumberField(TEXT("durationSeconds"), ReloadResult.DurationSeconds);
		}
	}

	namespace UnrealMcpUserRegistryReloadTool
	{
		FUnrealMcpExecutionResult Execute(const FJsonObject& Arguments)
		{
			bool bAcceptChangedHashes = false;
			bool bDryRun = false;
			Arguments.TryGetBoolField(TEXT("acceptChangedHashes"), bAcceptChangedHashes);
			Arguments.TryGetBoolField(TEXT("dryRun"), bDryRun);

			UserToolLock::FExclusiveGuard ReloadGuard;
			const UserRegistry::FReloadResult ReloadResult = bDryRun
				? UserRegistry::PreviewUserToolRegistryReload(bAcceptChangedHashes)
				: UserRegistry::ReloadUserToolRegistry(bAcceptChangedHashes);

			Extension::FToolLifecycle Lifecycle;
			Lifecycle.ToolName = Extension::ControlToolUserRegistryReload;
			Lifecycle.ExtensionScope = Extension::EExtensionScope::User;
			Lifecycle.ImplementationTrack = Extension::EImplementationTrack::Python;
			Lifecycle.State = bDryRun ? Extension::ELifecycleState::DryRunReady : Extension::ELifecycleState::LoadedUserPythonHot;
			Lifecycle.bCallableNow = !bDryRun;
			Lifecycle.NextRequiredAction = FString();
			Lifecycle.SourceKind = Extension::ESourceKind::UserRegistry;
			Lifecycle.HandlerKind = Extension::EHandlerKind::PythonBridge;

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), bDryRun ? TEXT("mcp_user_registry_reload_dry_run") : TEXT("mcp_user_registry_reload"));
			StructuredContent->SetBoolField(TEXT("dryRun"), bDryRun);
			StructuredContent->SetBoolField(TEXT("acceptChangedHashes"), bAcceptChangedHashes);
			StructuredContent->SetStringField(TEXT("userToolsRootDir"), UserRegistry::GetUserToolsRootDir());
			StructuredContent->SetStringField(TEXT("hotReloadMode"), TEXT("python_main_reimport"));
			UserRegistryReloadAddResultFields(StructuredContent, ReloadResult);
			StructuredContent->SetObjectField(TEXT("lifecycle"), Extension::BuildLifecycleJson(Lifecycle));

			const FString Text = FString::Printf(
				TEXT("%s user tool registry: before=%d after=%d added=%d updated=%d removed=%d rejected=%d."),
				bDryRun ? TEXT("Previewed") : TEXT("Reloaded"),
				ReloadResult.BeforeCount,
				ReloadResult.AfterCount,
				ReloadResult.AddedTools.Num(),
				ReloadResult.UpdatedTools.Num(),
				ReloadResult.RemovedTools.Num(),
				ReloadResult.RejectedTools.Num());
			return MakeExecutionResult(Text, StructuredContent, false);
		}
	}
}
