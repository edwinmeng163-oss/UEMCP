#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FUnrealMcpExecutionResult;

namespace UnrealMcp
{
	bool TryExecutePieSmokeTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult);
	void MarkActivePieSmokeStaleOnShutdown();
	bool MarkActivePieSmokeStaleFromAutomationLock(const FString& RunId, const FString& StaleReason);

#if WITH_DEV_AUTOMATION_TESTS
	void ResetPieSmokeToolStateForTests();
	bool ParsePieSmokeArgumentsForTests(const FJsonObject& Arguments, int32& OutTimeoutSeconds, int32& OutAliveWindowSeconds, FString& OutErrorKind);
	bool ValidatePieSmokeMapPathForTests(const FString& MapPath, FString& OutMatchedMap, FString& OutErrorKind, FString& OutMessage);
	void CreateActivePieSmokeRunWithDelegatesForTests(const FString& RunId);
	bool ArePieSmokeDelegatesRegisteredForTests();
	bool MarkActivePieSmokeStaleForTests(const FString& RunId, const FString& StaleReason);
#endif
}
