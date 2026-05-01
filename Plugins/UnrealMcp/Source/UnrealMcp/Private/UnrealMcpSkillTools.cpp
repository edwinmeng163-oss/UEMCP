#include "UnrealMcpSkillTools.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace UnrealMcp
{
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);
	bool ResolveProjectPathInsideProject(const FString& RequestedPath, FString& OutPath, FString& OutFailureReason);
	TSharedPtr<FJsonObject> MakeFileInfoObject(const FString& Path);
	bool TryGetStringArrayField(const FJsonObject& Arguments, const FString& FieldName, TArray<FString>& OutValues);
	FString JsonObjectToString(const TSharedPtr<FJsonObject>& JsonObject);
	TArray<TSharedPtr<FJsonValue>> MakeJsonStringArray(const TArray<FString>& Values);
	FUnrealMcpExecutionResult ProjectMemoryWrite(const FJsonObject& Arguments);

		FString GetDefaultProjectSkillRoot()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/UnrealMcpSkills")));
		}

		FString SkillNameFromPath(const FString& SkillPath)
		{
			if (FPaths::GetCleanFilename(SkillPath).Equals(TEXT("SKILL.md"), ESearchCase::IgnoreCase))
			{
				return FPaths::GetCleanFilename(FPaths::GetPath(SkillPath));
			}
			FString Name = FPaths::GetBaseFilename(SkillPath);
			Name.RemoveFromEnd(TEXT(".skill"), ESearchCase::IgnoreCase);
			return Name;
		}

		FString ExtractSkillTitle(const FString& SkillText, const FString& Fallback)
		{
			TArray<FString> Lines;
			SkillText.ParseIntoArrayLines(Lines, false);
			for (const FString& Line : Lines)
			{
				FString CleanLine = Line.TrimStartAndEnd();
				if (CleanLine.StartsWith(TEXT("#")))
				{
					CleanLine.RemoveFromStart(TEXT("#"));
					return CleanLine.TrimStartAndEnd();
				}
			}
			return Fallback;
		}

		FString ExtractSkillDescription(const FString& SkillText)
		{
			TArray<FString> Lines;
			SkillText.ParseIntoArrayLines(Lines, false);
			bool bPassedTitle = false;
			for (const FString& Line : Lines)
			{
				const FString CleanLine = Line.TrimStartAndEnd();
				if (CleanLine.IsEmpty())
				{
					continue;
				}
				if (CleanLine.StartsWith(TEXT("#")))
				{
					bPassedTitle = true;
					continue;
				}
				if (bPassedTitle)
				{
					return CleanLine.Left(600);
				}
			}
			return FString();
		}

		void CollectSkillPathsFromRoot(const FString& RootPath, TArray<FString>& OutSkillPaths)
		{
			if (!FPaths::DirectoryExists(RootPath))
			{
				return;
			}
			TArray<FString> SkillMdFiles;
			TArray<FString> SkillFiles;
			IFileManager::Get().FindFilesRecursive(SkillMdFiles, *RootPath, TEXT("SKILL.md"), true, false);
			IFileManager::Get().FindFilesRecursive(SkillFiles, *RootPath, TEXT("*.skill"), true, false);
			for (const FString& SkillPath : SkillMdFiles)
			{
				OutSkillPaths.AddUnique(SkillPath);
			}
			for (const FString& SkillPath : SkillFiles)
			{
				OutSkillPaths.AddUnique(SkillPath);
			}
			OutSkillPaths.Sort();
		}

		TSharedPtr<FJsonObject> MakeSkillInfoObject(const FString& SkillPath, bool bIncludeText, int32 MaxPreviewChars)
		{
			TSharedPtr<FJsonObject> SkillObject = MakeShared<FJsonObject>();
			const FString SkillName = SkillNameFromPath(SkillPath);
			SkillObject->SetStringField(TEXT("name"), SkillName);
			SkillObject->SetStringField(TEXT("path"), SkillPath);
			SkillObject->SetObjectField(TEXT("file"), MakeFileInfoObject(SkillPath));

			FString SkillText;
			if (FFileHelper::LoadFileToString(SkillText, *SkillPath))
			{
				SkillObject->SetStringField(TEXT("title"), ExtractSkillTitle(SkillText, SkillName));
				SkillObject->SetStringField(TEXT("description"), ExtractSkillDescription(SkillText));
				SkillObject->SetStringField(TEXT("preview"), SkillText.Left(FMath::Max(0, MaxPreviewChars)));
				if (bIncludeText)
				{
					SkillObject->SetStringField(TEXT("text"), SkillText);
				}
			}
			else
			{
				SkillObject->SetStringField(TEXT("title"), SkillName);
				SkillObject->SetStringField(TEXT("description"), TEXT("Failed to read skill text."));
			}
			return SkillObject;
		}

		bool ResolveSkillPathFromArguments(const FJsonObject& Arguments, FString& OutSkillPath, FString& OutFailureReason)
		{
			FString SkillPath;
			FString SkillName;
			Arguments.TryGetStringField(TEXT("skillPath"), SkillPath);
			Arguments.TryGetStringField(TEXT("skillName"), SkillName);
			SkillPath = SkillPath.TrimStartAndEnd();
			SkillName = SkillName.TrimStartAndEnd();

			if (!SkillPath.IsEmpty())
			{
				return ResolveProjectPathInsideProject(SkillPath, OutSkillPath, OutFailureReason);
			}

			if (SkillName.IsEmpty())
			{
				OutFailureReason = TEXT("Provide either skillPath or skillName.");
				return false;
			}

			TArray<FString> Roots;
			TryGetStringArrayField(Arguments, TEXT("roots"), Roots);
			if (Roots.Num() == 0)
			{
				Roots.Add(TEXT("Tools/UnrealMcpSkills"));
			}

			TArray<FString> SkillPaths;
			for (const FString& Root : Roots)
			{
				FString ResolvedRoot;
				if (ResolveProjectPathInsideProject(Root, ResolvedRoot, OutFailureReason))
				{
					CollectSkillPathsFromRoot(ResolvedRoot, SkillPaths);
				}
			}

			for (const FString& CandidatePath : SkillPaths)
			{
				if (SkillNameFromPath(CandidatePath).Equals(SkillName, ESearchCase::IgnoreCase))
				{
					OutSkillPath = CandidatePath;
					return true;
				}
			}

			OutFailureReason = FString::Printf(TEXT("No project skill named '%s' was found."), *SkillName);
			return false;
		}

		FUnrealMcpExecutionResult SkillList(const FJsonObject& Arguments)
		{
			TArray<FString> Roots;
			TryGetStringArrayField(Arguments, TEXT("roots"), Roots);
			if (Roots.Num() == 0)
			{
				Roots.Add(TEXT("Tools/UnrealMcpSkills"));
			}
			FString NameFilter;
			bool bIncludeText = false;
			double MaxPreviewCharsDouble = 1200.0;
			Arguments.TryGetStringField(TEXT("nameFilter"), NameFilter);
			Arguments.TryGetBoolField(TEXT("includeText"), bIncludeText);
			Arguments.TryGetNumberField(TEXT("maxPreviewChars"), MaxPreviewCharsDouble);
			const int32 MaxPreviewChars = FMath::Clamp(static_cast<int32>(MaxPreviewCharsDouble), 0, 20000);

			TArray<TSharedPtr<FJsonValue>> RootObjects;
			TArray<FString> SkillPaths;
			FString FailureReason;
			for (const FString& Root : Roots)
			{
				FString ResolvedRoot;
				if (!ResolveProjectPathInsideProject(Root, ResolvedRoot, FailureReason))
				{
					return MakeExecutionResult(FailureReason, nullptr, true);
				}
				TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
				RootObject->SetStringField(TEXT("root"), ResolvedRoot);
				RootObject->SetBoolField(TEXT("exists"), FPaths::DirectoryExists(ResolvedRoot));
				RootObjects.Add(MakeShared<FJsonValueObject>(RootObject));
				CollectSkillPathsFromRoot(ResolvedRoot, SkillPaths);
			}

			TArray<TSharedPtr<FJsonValue>> SkillObjects;
			for (const FString& SkillPath : SkillPaths)
			{
				const FString SkillName = SkillNameFromPath(SkillPath);
				if (!NameFilter.TrimStartAndEnd().IsEmpty() && !SkillName.Contains(NameFilter.TrimStartAndEnd(), ESearchCase::IgnoreCase))
				{
					continue;
				}
				SkillObjects.Add(MakeShared<FJsonValueObject>(MakeSkillInfoObject(SkillPath, bIncludeText, MaxPreviewChars)));
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), TEXT("skill_list"));
			StructuredContent->SetArrayField(TEXT("roots"), RootObjects);
			StructuredContent->SetStringField(TEXT("nameFilter"), NameFilter);
			StructuredContent->SetNumberField(TEXT("skillCount"), SkillObjects.Num());
			StructuredContent->SetArrayField(TEXT("skills"), SkillObjects);
			return MakeExecutionResult(FString::Printf(TEXT("Found %d project skill%s."), SkillObjects.Num(), SkillObjects.Num() == 1 ? TEXT("") : TEXT("s")), StructuredContent, false);
		}

		FUnrealMcpExecutionResult SkillRead(const FJsonObject& Arguments)
		{
			bool bIncludeText = true;
			double MaxPreviewCharsDouble = 4000.0;
			Arguments.TryGetBoolField(TEXT("includeText"), bIncludeText);
			Arguments.TryGetNumberField(TEXT("maxPreviewChars"), MaxPreviewCharsDouble);

			FString SkillPath;
			FString FailureReason;
			if (!ResolveSkillPathFromArguments(Arguments, SkillPath, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			TSharedPtr<FJsonObject> SkillObject = MakeSkillInfoObject(SkillPath, bIncludeText, FMath::Clamp(static_cast<int32>(MaxPreviewCharsDouble), 0, 50000));
			SkillObject->SetStringField(TEXT("action"), TEXT("skill_read"));
			return MakeExecutionResult(FString::Printf(TEXT("Read project skill '%s'."), *SkillObject->GetStringField(TEXT("name"))), SkillObject, false);
		}

		FUnrealMcpExecutionResult SkillApply(const FJsonObject& Arguments)
		{
			FString Task;
			FString MemoryKey;
			bool bWriteMemory = true;
			bool bIncludeFullText = true;
			Arguments.TryGetStringField(TEXT("task"), Task);
			Arguments.TryGetStringField(TEXT("memoryKey"), MemoryKey);
			Arguments.TryGetBoolField(TEXT("writeMemory"), bWriteMemory);
			Arguments.TryGetBoolField(TEXT("includeFullText"), bIncludeFullText);

			FString SkillPath;
			FString FailureReason;
			if (!ResolveSkillPathFromArguments(Arguments, SkillPath, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			FString SkillText;
			if (!FFileHelper::LoadFileToString(SkillText, *SkillPath))
			{
				return MakeExecutionResult(FString::Printf(TEXT("Failed to read skill '%s'."), *SkillPath), nullptr, true);
			}

			const FString SkillName = SkillNameFromPath(SkillPath);
			if (MemoryKey.TrimStartAndEnd().IsEmpty())
			{
				MemoryKey = FString::Printf(TEXT("skill.%s.last_apply"), *SkillName);
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), TEXT("skill_apply"));
			StructuredContent->SetStringField(TEXT("skillName"), SkillName);
			StructuredContent->SetStringField(TEXT("skillPath"), SkillPath);
			StructuredContent->SetStringField(TEXT("task"), Task);
			StructuredContent->SetStringField(TEXT("memoryKey"), MemoryKey);
			StructuredContent->SetStringField(TEXT("title"), ExtractSkillTitle(SkillText, SkillName));
			StructuredContent->SetStringField(TEXT("description"), ExtractSkillDescription(SkillText));
			StructuredContent->SetStringField(TEXT("applicationPrompt"), FString::Printf(
				TEXT("Apply the project skill '%s' from %s to the current task. Follow the SKILL.md instructions first, then continue with normal MCP tool safety checks."),
				*SkillName,
				*SkillPath));
			if (bIncludeFullText)
			{
				StructuredContent->SetStringField(TEXT("skillText"), SkillText);
			}
			else
			{
				StructuredContent->SetStringField(TEXT("skillPreview"), SkillText.Left(4000));
			}

			if (bWriteMemory)
			{
				TSharedPtr<FJsonObject> ContentObject = MakeShared<FJsonObject>();
				ContentObject->SetStringField(TEXT("skillName"), SkillName);
				ContentObject->SetStringField(TEXT("skillPath"), SkillPath);
				ContentObject->SetStringField(TEXT("task"), Task);
				ContentObject->SetStringField(TEXT("appliedAtUtc"), FDateTime::UtcNow().ToIso8601());
				TSharedPtr<FJsonObject> MemoryArgs = MakeShared<FJsonObject>();
				MemoryArgs->SetStringField(TEXT("key"), MemoryKey);
				MemoryArgs->SetStringField(TEXT("summary"), FString::Printf(TEXT("Applied project skill %s."), *SkillName));
				MemoryArgs->SetStringField(TEXT("status"), TEXT("applied"));
				MemoryArgs->SetStringField(TEXT("nextStep"), TEXT("Use returned skillText/applicationPrompt as the instruction context for this task."));
				MemoryArgs->SetStringField(TEXT("contentJson"), JsonObjectToString(ContentObject));
				MemoryArgs->SetArrayField(TEXT("tags"), MakeJsonStringArray({ TEXT("skill"), SkillName }));
				FUnrealMcpExecutionResult MemoryResult = ProjectMemoryWrite(*MemoryArgs);
				if (MemoryResult.StructuredContent.IsValid())
				{
					StructuredContent->SetObjectField(TEXT("memoryWrite"), MemoryResult.StructuredContent);
				}
			}

			return MakeExecutionResult(FString::Printf(TEXT("Applied project skill '%s'."), *SkillName), StructuredContent, false);
		}
}
