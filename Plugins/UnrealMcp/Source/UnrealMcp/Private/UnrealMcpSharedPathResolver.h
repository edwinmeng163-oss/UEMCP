#pragma once

#include "CoreMinimal.h"

class FJsonValue;

namespace UnrealMcp
{
	bool ResolveSharedRepoRoot(
		const FString& ToolsSubpath,
		const TArray<FString>& RequiredRecursivePatterns,
		FString& OutRoot,
		TArray<FString>& OutCandidateRoots);
	bool SharedRepoRootHasAny(
		const FString& Root,
		const TArray<FString>& RequiredRecursivePatterns);
	TArray<TSharedPtr<FJsonValue>> MakeSharedRepoRootCandidateValues(
		const TArray<FString>& CandidateRoots,
		const TArray<FString>& RequiredRecursivePatterns);
}
