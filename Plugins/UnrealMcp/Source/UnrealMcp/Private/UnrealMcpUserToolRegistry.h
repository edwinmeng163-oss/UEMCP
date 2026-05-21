// Copyright UnrealMcp contributors. SPDX-License-Identifier: MIT.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "Dom/JsonObject.h"
#include "Templates/SharedPointer.h"
#include "UnrealMcpExtensionLifecycle.h"

namespace UnrealMcp::UserRegistry
{
	struct FUserToolEntry
	{
		FString ToolName;
		FString ScaffoldDir;
		FString PythonHandlerPath;
		FString PythonHandlerSha256;
		TArray<FString> ImportAllowlist;
		TSharedPtr<FJsonObject> ToolJson;
		::UnrealMcp::Extension::ELifecycleState LifecycleState = ::UnrealMcp::Extension::ELifecycleState::LoadedUserPythonHot;
	};

	struct FReloadResult
	{
		int32 BeforeCount = 0;
		int32 AfterCount = 0;
		TArray<FString> AddedTools;
		TArray<FString> UpdatedTools;
		TArray<FString> RemovedTools;
		struct FRejection { FString ToolName; FString Reason; };
		TArray<FRejection> RejectedTools;
		double DurationSeconds = 0.0;
	};

	UNREALMCP_API void InitializeUserToolRegistry();
	UNREALMCP_API FReloadResult ReloadUserToolRegistry(bool bAcceptChangedHashes = false);
	UNREALMCP_API FReloadResult PreviewUserToolRegistryReload(bool bAcceptChangedHashes = false);
	UNREALMCP_API const FUserToolEntry* FindUserTool(const FString& ToolName);
	UNREALMCP_API TArray<const FUserToolEntry*> GetAllUserTools();
	UNREALMCP_API int32 GetUserToolCount();
	UNREALMCP_API FString GetUserToolsRootDir();
}
