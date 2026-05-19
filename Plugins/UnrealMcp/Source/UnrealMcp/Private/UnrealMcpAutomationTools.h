#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FUnrealMcpExecutionResult;

namespace UnrealMcp
{
	bool TryExecuteAutomationTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult);

#if WITH_DEV_AUTOMATION_TESTS
	void ResetAutomationToolStateForTests();
	void SetAutomationFrameworkStartSuppressedForTests(bool bSuppress);
	FString WriteAutomationRunStateForTests(
		const FString& RunId,
		const FString& Status,
		const FString& FullName,
		const FString& DisplayName,
		const FDateTime& StartedAtUtc,
		int32 TimeoutSeconds);
	bool DeleteAutomationRunStateForTests(const FString& RunId);
#endif
}
