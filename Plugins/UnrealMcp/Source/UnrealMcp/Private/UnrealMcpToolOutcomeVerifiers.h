#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FUnrealMcpExecutionResult;

namespace UnrealMcp
{
	TSharedPtr<FJsonObject> BuildBlueprintToolPreflight(
		const FString& ToolName,
		const FJsonObject& Arguments,
		const TSharedPtr<FJsonObject>& GenericPreflight);

	TSharedPtr<FJsonObject> BuildWidgetToolPreflight(
		const FString& ToolName,
		const FJsonObject& Arguments,
		const TSharedPtr<FJsonObject>& GenericPreflight);

	TSharedPtr<FJsonObject> BuildActorToolPreflight(
		const FString& ToolName,
		const FJsonObject& Arguments,
		const TSharedPtr<FJsonObject>& GenericPreflight);

	TSharedPtr<FJsonObject> VerifyBlueprintToolOutcome(
		const FString& ToolName,
		const FJsonObject& Arguments,
		const FUnrealMcpExecutionResult& Result);

	TSharedPtr<FJsonObject> VerifyWidgetToolOutcome(
		const FString& ToolName,
		const FJsonObject& Arguments,
		const FUnrealMcpExecutionResult& Result);

	TSharedPtr<FJsonObject> VerifyActorToolOutcome(
		const FString& ToolName,
		const FJsonObject& Arguments,
		const FUnrealMcpExecutionResult& Result);
}
