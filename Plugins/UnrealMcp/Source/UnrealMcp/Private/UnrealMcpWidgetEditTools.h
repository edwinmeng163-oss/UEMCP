#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UWidgetBlueprint;
struct FUnrealMcpExecutionResult;

namespace UnrealMcp
{
	bool TryExecuteWidgetEditTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult);

	int32 CountWidgetBindingReferences(const UWidgetBlueprint* WidgetBlueprint, const TSet<FString>& WidgetNames);
	int32 RenameWidgetBindingObjectReferences(UWidgetBlueprint* WidgetBlueprint, const FString& OldName, const FString& NewName);
	bool ShouldRefuseWidgetDelete(int32 ReferenceCount, bool bForce);
	FString MakeUniqueWidgetDuplicateName(const FString& SourceName, const FString& RequestedName, const TSet<FString>& ExistingNames);
}
