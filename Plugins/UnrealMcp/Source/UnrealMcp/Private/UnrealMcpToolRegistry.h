#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace UnrealMcp
{
	enum class EToolExposure : uint8
	{
		Visible,
		LegacyHidden
	};

	enum class EToolRiskLevel : uint8
	{
		ReadOnly,
		Low,
		Medium,
		High,
		Critical
	};

	struct FToolPolicy
	{
		EToolRiskLevel RiskLevel = EToolRiskLevel::Low;
		bool bRequiresWrite = false;
		bool bRequiresBuild = false;
		bool bRequiresExternalProcess = false;
		bool bRequiresRestart = false;
		bool bRequiresProjectMemory = false;
		bool bRequiresLock = false;
		FString Reason;
	};

	struct FToolRegistryEntry
	{
		FString Name;
		FString Category;
		FString HandlerName;
		EToolExposure Exposure = EToolExposure::Visible;
		FString Notes;
	};

	const TArray<FToolRegistryEntry>& GetToolRegistryEntries();
	const FToolRegistryEntry* FindToolRegistryEntry(const FString& ToolName);
	bool ShouldExposeToolToAi(const FString& ToolName);
	FString ResolveToolHandlerName(const FString& ToolName);
	FToolPolicy GetToolPolicy(const FString& ToolName);
	FString LexToString(EToolRiskLevel RiskLevel);
	TSharedPtr<FJsonObject> MakeToolPolicyObject(const FString& ToolName);
	void AddToolRegistryStatus(const TSharedPtr<FJsonObject>& StructuredContent);
}
