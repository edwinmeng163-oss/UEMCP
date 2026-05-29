#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealMcpCallToolLibrary.generated.h"

UCLASS(meta=(ScriptName="UnrealMcpCallTool"))
class UNREALMCP_API UUnrealMcpCallToolLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Unreal MCP|CallTool", meta=(ScriptName="CallTool"))
	static FString CallTool(const FString& ToolName, const FString& ArgumentsJson);
};
