#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FUnrealMcpExecutionResult;

namespace UnrealMcp
{
	void RegisterDiagnosticsListener();
	void UnregisterDiagnosticsListener();
	bool TryExecuteDiagnosticsTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult);

#if WITH_DEV_AUTOMATION_TESTS
	void ResetDiagnosticsStateForTests(const FDateTime& StartedAtUtc);
	void AppendDiagnosticLogLineForTests(
		const FString& Message,
		ELogVerbosity::Type Verbosity,
		const FName& Category,
		const FDateTime& TimestampUtc);
#endif
}
