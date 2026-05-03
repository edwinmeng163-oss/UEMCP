#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FUnrealMcpExecutionResult;

namespace UnrealMcp
{
	void AttachToolExecutionCheck(
		const FString& RequestedToolName,
		const FJsonObject& Arguments,
		FUnrealMcpExecutionResult& Result);
}
