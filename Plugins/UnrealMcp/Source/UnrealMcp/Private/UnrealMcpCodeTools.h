#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class FJsonObject;
struct FUnrealMcpExecutionResult;

namespace UnrealMcp
{
	enum class ECodePathClassification : uint8
	{
		DefaultWritable,
		HighRisk,
		Forbidden,
		OutsideProject
	};

	struct FCodePathPolicy
	{
		ECodePathClassification Classification = ECodePathClassification::Forbidden;
		FString CanonicalPath;
		FString Reason;
	};

	FString LexToString(ECodePathClassification Classification);

	FCodePathPolicy ClassifyCodePath_Pure(
		const FString& ProjectDir,
		const FString& PluginBaseDir,
		const FString& RawInputPath,
		TFunctionRef<bool(const FString&)> PathExists,
		TFunctionRef<bool(const FString& Path, FString& OutTarget)> TryResolveSymlinkTarget);

#if WITH_DEV_AUTOMATION_TESTS
	struct FCodeToolsApplyTestHooks
	{
		TFunction<void(const FString& ProjectRelativePath)> BeforeWrite;
		TFunction<void(const FString& ApplyState, const FString& ManifestPath)> AfterApplyManifestState;
		TFunction<bool(const FString& EditId)> ShouldPretendBackupDirectoryExists;
	};

	void SetCodeToolsApplyTestHooks(const FCodeToolsApplyTestHooks& Hooks);
	void ClearCodeToolsApplyTestHooks();
#endif

	bool TryExecuteCodeTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult);
}
