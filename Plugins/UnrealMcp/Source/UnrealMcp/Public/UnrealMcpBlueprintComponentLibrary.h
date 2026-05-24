#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealMcpBlueprintComponentLibrary.generated.h"

class UBlueprint;

UCLASS(meta=(ScriptName="UnrealMcpBlueprintComponentLibrary"))
class UNREALMCP_API UUnrealMcpBlueprintComponentLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Unreal MCP|Components", meta=(ScriptName="AddComponentToBlueprint"))
	static bool AddComponentToBlueprint(
		UBlueprint* Blueprint,
		UClass* ComponentClass,
		FName ComponentName,
		FName AttachParentComponentName,
		FString& OutCreatedNodeName,
		FString& OutFailureReason);

	UFUNCTION(BlueprintCallable, Category="Unreal MCP|Components", meta=(ScriptName="SetBlueprintComponentTemplateProperty"))
	static bool SetBlueprintComponentTemplateProperty(
		UBlueprint* Blueprint,
		FName ComponentName,
		const FString& PropertyName,
		const FString& ValueText,
		FString& OutReadbackValue,
		FString& OutFailureReason);

	static bool SetBlueprintClassDefaultProperty(
		UBlueprint* Blueprint,
		const FString& PropertyName,
		const FString& ValueText,
		FString& OutReadbackValue,
		FString& OutFailureReason);
};
