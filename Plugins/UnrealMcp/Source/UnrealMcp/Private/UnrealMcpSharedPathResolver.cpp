#include "UnrealMcpSharedPathResolver.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace UnrealMcp
{
	namespace
	{
		FString NormalizeSharedPath(const FString& Path)
		{
			FString FullPath = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(FullPath);
			FPaths::CollapseRelativeDirectories(FullPath);
			return FullPath;
		}

		void AddUniqueCandidate(TArray<FString>& Candidates, const FString& Candidate)
		{
			for (const FString& Existing : Candidates)
			{
				if (Existing.Equals(Candidate, ESearchCase::IgnoreCase))
				{
					return;
				}
			}
			Candidates.Add(Candidate);
		}

		void AddSharedRepoCandidates(const FString& ToolsSubpath, TArray<FString>& OutCandidateRoots)
		{
			OutCandidateRoots.Reset();

			FString CleanSubpath = ToolsSubpath;
			FPaths::NormalizeFilename(CleanSubpath);
			CleanSubpath.RemoveFromStart(TEXT("Tools/"));
			CleanSubpath.RemoveFromStart(TEXT("/"));

			FString AncestorDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			FPaths::NormalizeDirectoryName(AncestorDir);
			FPaths::CollapseRelativeDirectories(AncestorDir);

			for (int32 CandidateIndex = 0; CandidateIndex < 9; ++CandidateIndex)
			{
				const FString Candidate = CleanSubpath.IsEmpty()
					? NormalizeSharedPath(FPaths::Combine(AncestorDir, TEXT("Tools")))
					: NormalizeSharedPath(FPaths::Combine(AncestorDir, TEXT("Tools"), CleanSubpath));
				AddUniqueCandidate(OutCandidateRoots, Candidate);

				const FString ParentDir = FPaths::GetPath(AncestorDir);
				if (ParentDir.IsEmpty() || ParentDir.Equals(AncestorDir, ESearchCase::CaseSensitive))
				{
					break;
				}
				AncestorDir = ParentDir;
			}
		}
	}

	bool SharedRepoRootHasAny(
		const FString& Root,
		const TArray<FString>& RequiredRecursivePatterns)
	{
		if (!FPaths::DirectoryExists(Root))
		{
			return false;
		}
		if (RequiredRecursivePatterns.Num() == 0)
		{
			return true;
		}

		for (const FString& Pattern : RequiredRecursivePatterns)
		{
			TArray<FString> Matches;
			IFileManager::Get().FindFilesRecursive(Matches, *Root, *Pattern, true, false);
			if (Matches.Num() > 0)
			{
				return true;
			}
		}
		return false;
	}

	bool ResolveSharedRepoRoot(
		const FString& ToolsSubpath,
		const TArray<FString>& RequiredRecursivePatterns,
		FString& OutRoot,
		TArray<FString>& OutCandidateRoots)
	{
		OutRoot.Reset();
		AddSharedRepoCandidates(ToolsSubpath, OutCandidateRoots);

		for (const FString& Candidate : OutCandidateRoots)
		{
			if (SharedRepoRootHasAny(Candidate, RequiredRecursivePatterns))
			{
				OutRoot = Candidate;
				return true;
			}
		}

		for (const FString& Candidate : OutCandidateRoots)
		{
			if (FPaths::DirectoryExists(Candidate))
			{
				OutRoot = Candidate;
				return false;
			}
		}

		if (OutCandidateRoots.Num() > 0)
		{
			OutRoot = OutCandidateRoots[0];
		}
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> MakeSharedRepoRootCandidateValues(
		const TArray<FString>& CandidateRoots,
		const TArray<FString>& RequiredRecursivePatterns)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& CandidateRoot : CandidateRoots)
		{
			TSharedPtr<FJsonObject> CandidateObject = MakeShared<FJsonObject>();
			CandidateObject->SetStringField(TEXT("root"), CandidateRoot);
			CandidateObject->SetBoolField(TEXT("exists"), FPaths::DirectoryExists(CandidateRoot));
			CandidateObject->SetBoolField(TEXT("hasExpectedContent"), SharedRepoRootHasAny(CandidateRoot, RequiredRecursivePatterns));
			Values.Add(MakeShared<FJsonValueObject>(CandidateObject));
		}
		return Values;
	}
}
