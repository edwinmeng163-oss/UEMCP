#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FUnrealMcpExecutionResult;

namespace UnrealMcp
{
	bool TryExecuteEditorTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult);
}
