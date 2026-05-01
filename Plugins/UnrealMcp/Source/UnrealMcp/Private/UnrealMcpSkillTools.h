#pragma once

#include "CoreMinimal.h"
#include "UnrealMcpModule.h"

class FJsonObject;

namespace UnrealMcp
{
	FUnrealMcpExecutionResult SkillList(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult SkillRead(const FJsonObject& Arguments);
	FUnrealMcpExecutionResult SkillApply(const FJsonObject& Arguments);
}
