#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FLinearColor;
struct FUnrealMcpExecutionResult;

namespace UnrealMcp
{
	bool TryExecuteMaterialInstanceTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult);

	bool TryGetStrictJsonNumber(const FJsonObject& Arguments, const FString& FieldName, double& OutValue, FString& OutFailureReason);
	bool TryGetStrictJsonLinearColor(const FJsonObject& Arguments, const FString& FieldName, FLinearColor& OutValue, FString& OutFailureReason);
}
