#pragma once

#include "CoreMinimal.h"
#include "UnrealMcpExtensionLifecycle.h"

class FJsonObject;
class FJsonValue;

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

	enum class EToolImplementationTrack : uint8
	{
		Cpp,
		Python
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
		bool bDryRunSupport = false;
		bool bPreflightSupport = false;
		bool bPostcheckSupport = false;
		FString Category;
		FString TestCoverage;
		FString Owner;
		FString DocsPath;
		FString Reason;
		FString SummaryTemplate;
	};

	struct FToolRegistryEntry
	{
		FString Name;
		FString Category;
		FString HandlerName;
		FString Title;
		FString Description;
		TSharedPtr<FJsonObject> InputSchema;
		EToolExposure Exposure = EToolExposure::Visible;
		EToolImplementationTrack ImplementationTrack = EToolImplementationTrack::Cpp;
		FString PythonHandlerPath;
		FString PythonHandlerSha256;
		TArray<FString> PythonImportAllowList;
		FString Notes;
		FToolPolicy Policy;
		bool bLoadedFromExplicitRegistry = false;
		bool bLoadedFromDescriptor = false;
	};

	const TArray<FToolRegistryEntry>& GetToolRegistryEntries();
	const FToolRegistryEntry* FindToolRegistryEntry(const FString& ToolName);
	bool HasExplicitToolRegistryEntry(const FString& ToolName);
	FString GetToolRegistrySourcePath();
	bool ShouldExposeToolToAi(const FString& ToolName);
	FString ResolveToolHandlerName(const FString& ToolName);
	Extension::ESourceKind ResolveToolSourceKind(const FString& ToolName);
	int32 GetCoreToolCount();
	int32 GetUserToolCount();
	FToolPolicy GetToolPolicy(const FString& ToolName);
	FString LexToString(EToolRiskLevel RiskLevel);
	FString LexToString(EToolImplementationTrack ImplementationTrack);
	TSharedPtr<FJsonObject> MakeToolPolicyObject(const FString& ToolName);
	TSharedPtr<FJsonObject> MakeToolRegistryValidationObject(const TArray<TSharedPtr<FJsonValue>>* VisibleToolsArray = nullptr);
	void AddToolRegistryStatus(const TSharedPtr<FJsonObject>& StructuredContent);
}
