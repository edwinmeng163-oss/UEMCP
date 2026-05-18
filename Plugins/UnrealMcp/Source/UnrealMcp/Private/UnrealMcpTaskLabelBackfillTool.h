#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FUnrealMcpExecutionResult;

namespace UnrealMcp
{
	FUnrealMcpExecutionResult TaskLabelBackfill(const FJsonObject& Arguments);
}
