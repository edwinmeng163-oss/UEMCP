#pragma once

#include "CoreMinimal.h"
#include "UnrealMcpToolRegistry.h"
class FJsonObject;

namespace UnrealMcp
{
	struct FToolHandlerRegistryEntry
	{
		FString HandlerName;
		FString Category;
		FString SourceFile;
		EToolImplementationTrack ImplementationTrack = EToolImplementationTrack::Cpp;
		FString PythonHandlerPath;
		FString PythonHandlerSha256;
		TArray<FString> PythonImportAllowList;
		TArray<FString> ToolNames;
		bool bLoadedFromExplicitRegistry = false;
		bool bLoadedFromDescriptor = false;
	};

	const TArray<FToolHandlerRegistryEntry>& GetToolHandlerRegistryEntries();
	const FToolHandlerRegistryEntry* FindToolHandlerRegistryEntry(const FString& HandlerName);
	bool IsRegisteredToolHandler(const FString& HandlerName);
	TSharedPtr<FJsonObject> MakeToolHandlerRegistryStatusObject();
}
