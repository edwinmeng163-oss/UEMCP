#include "UnrealMcpMemoryTools.h"

#include "UnrealMcpModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace UnrealMcp
{
		bool LoadJsonObject(const FString& JsonText, TSharedPtr<FJsonObject>& OutObject);
		bool TryGetStringArrayField(const FJsonObject& Arguments, const FString& FieldName, TArray<FString>& OutValues);
		FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);

		FString GetProjectMemoryFilePath()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/ProjectMemory.json")));
		}

		bool ResolveProjectPathInsideProject(const FString& RequestedPath, FString& OutPath, FString& OutFailureReason);
		TSharedPtr<FJsonObject> MakeFileInfoObject(const FString& Path);

		TSharedPtr<FJsonObject> MakeEmptyProjectMemory()
		{
			TSharedPtr<FJsonObject> MemoryObject = MakeShared<FJsonObject>();
			MemoryObject->SetNumberField(TEXT("version"), 1);
			MemoryObject->SetStringField(TEXT("projectName"), FApp::GetProjectName());
			MemoryObject->SetArrayField(TEXT("entries"), TArray<TSharedPtr<FJsonValue>>());
			return MemoryObject;
		}

		bool LoadProjectMemory(TSharedPtr<FJsonObject>& OutMemory, FString& OutFailureReason)
		{
			const FString MemoryPath = GetProjectMemoryFilePath();
			if (!FPaths::FileExists(MemoryPath))
			{
				OutMemory = MakeEmptyProjectMemory();
				return true;
			}

			FString MemoryText;
			if (!FFileHelper::LoadFileToString(MemoryText, *MemoryPath))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to read project memory file '%s'."), *MemoryPath);
				return false;
			}

			if (!LoadJsonObject(MemoryText, OutMemory) || !OutMemory.IsValid())
			{
				OutFailureReason = FString::Printf(TEXT("Project memory file '%s' is not valid JSON."), *MemoryPath);
				return false;
			}

			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if (!OutMemory->TryGetArrayField(TEXT("entries"), Entries) || !Entries)
			{
				OutMemory->SetArrayField(TEXT("entries"), TArray<TSharedPtr<FJsonValue>>());
			}
			return true;
		}

		bool SaveProjectMemory(const TSharedPtr<FJsonObject>& MemoryObject, FString& OutFailureReason)
		{
			const FString MemoryPath = GetProjectMemoryFilePath();
			const FString Directory = FPaths::GetPath(MemoryPath);
			if (!IFileManager::Get().MakeDirectory(*Directory, true))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to create project memory directory '%s'."), *Directory);
				return false;
			}

			FString MemoryText;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&MemoryText);
			if (!FJsonSerializer::Serialize(MemoryObject.ToSharedRef(), Writer))
			{
				OutFailureReason = TEXT("Failed to serialize project memory JSON.");
				return false;
			}

			if (!FFileHelper::SaveStringToFile(MemoryText, *MemoryPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to write project memory file '%s'."), *MemoryPath);
				return false;
			}
			return true;
		}

		FUnrealMcpExecutionResult ProjectMemoryWrite(const FJsonObject& Arguments)
		{
			FString Key = TEXT("current");
			FString Summary;
			FString Status;
			FString NextStep;
			FString ContentJson;
			Arguments.TryGetStringField(TEXT("key"), Key);
			Arguments.TryGetStringField(TEXT("summary"), Summary);
			Arguments.TryGetStringField(TEXT("status"), Status);
			Arguments.TryGetStringField(TEXT("nextStep"), NextStep);
			Arguments.TryGetStringField(TEXT("contentJson"), ContentJson);
			Key = Key.TrimStartAndEnd();
			if (Key.IsEmpty())
			{
				return MakeExecutionResult(TEXT("key must not be empty."), nullptr, true);
			}

			TArray<FString> Tags;
			TryGetStringArrayField(Arguments, TEXT("tags"), Tags);

			TSharedPtr<FJsonObject> ContentObject = MakeShared<FJsonObject>();
			if (!ContentJson.TrimStartAndEnd().IsEmpty())
			{
				if (!LoadJsonObject(ContentJson, ContentObject) || !ContentObject.IsValid())
				{
					return MakeExecutionResult(TEXT("contentJson must be a valid JSON object."), nullptr, true);
				}
			}

			FString FailureReason;
			TSharedPtr<FJsonObject> MemoryObject;
			if (!LoadProjectMemory(MemoryObject, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			const FString Timestamp = FDateTime::UtcNow().ToIso8601();
			TArray<TSharedPtr<FJsonValue>> Entries;
			const TArray<TSharedPtr<FJsonValue>>* ExistingEntries = nullptr;
			if (MemoryObject->TryGetArrayField(TEXT("entries"), ExistingEntries) && ExistingEntries)
			{
				Entries = *ExistingEntries;
			}

			int32 ExistingIndex = INDEX_NONE;
			for (int32 Index = 0; Index < Entries.Num(); ++Index)
			{
				if (!Entries[Index].IsValid() || Entries[Index]->Type != EJson::Object || !Entries[Index]->AsObject().IsValid())
				{
					continue;
				}

				FString ExistingKey;
				if (Entries[Index]->AsObject()->TryGetStringField(TEXT("key"), ExistingKey) && ExistingKey == Key)
				{
					ExistingIndex = Index;
					break;
				}
			}

			TArray<TSharedPtr<FJsonValue>> TagValues;
			for (const FString& Tag : Tags)
			{
				TagValues.Add(MakeShared<FJsonValueString>(Tag));
			}

			TSharedPtr<FJsonObject> EntryObject = MakeShared<FJsonObject>();
			EntryObject->SetStringField(TEXT("key"), Key);
			EntryObject->SetStringField(TEXT("summary"), Summary);
			EntryObject->SetStringField(TEXT("status"), Status);
			EntryObject->SetStringField(TEXT("nextStep"), NextStep);
			EntryObject->SetStringField(TEXT("updatedAtUtc"), Timestamp);
			EntryObject->SetArrayField(TEXT("tags"), TagValues);
			EntryObject->SetObjectField(TEXT("content"), ContentObject);

			if (ExistingIndex == INDEX_NONE)
			{
				Entries.Add(MakeShared<FJsonValueObject>(EntryObject));
			}
			else
			{
				Entries[ExistingIndex] = MakeShared<FJsonValueObject>(EntryObject);
			}

			MemoryObject->SetStringField(TEXT("updatedAtUtc"), Timestamp);
			MemoryObject->SetArrayField(TEXT("entries"), Entries);

			if (!SaveProjectMemory(MemoryObject, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), TEXT("project_memory_write"));
			StructuredContent->SetStringField(TEXT("path"), GetProjectMemoryFilePath());
			StructuredContent->SetStringField(TEXT("key"), Key);
			StructuredContent->SetBoolField(TEXT("created"), ExistingIndex == INDEX_NONE);
			StructuredContent->SetObjectField(TEXT("entry"), EntryObject);

			return MakeExecutionResult(
				FString::Printf(TEXT("Wrote project memory key '%s' to %s."), *Key, *GetProjectMemoryFilePath()),
				StructuredContent,
				false);
		}

		FUnrealMcpExecutionResult ProjectMemoryRead(const FJsonObject& Arguments)
		{
			FString Key;
			bool bIncludeContent = true;
			Arguments.TryGetStringField(TEXT("key"), Key);
			Arguments.TryGetBoolField(TEXT("includeContent"), bIncludeContent);
			Key = Key.TrimStartAndEnd();

			FString FailureReason;
			TSharedPtr<FJsonObject> MemoryObject;
			if (!LoadProjectMemory(MemoryObject, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			TArray<TSharedPtr<FJsonValue>> MatchingEntries;
			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if (MemoryObject->TryGetArrayField(TEXT("entries"), Entries) && Entries)
			{
				for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
				{
					if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object || !EntryValue->AsObject().IsValid())
					{
						continue;
					}

					TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
					FString ExistingKey;
					EntryObject->TryGetStringField(TEXT("key"), ExistingKey);
					if (!Key.IsEmpty() && ExistingKey != Key)
					{
						continue;
					}

					if (bIncludeContent)
					{
						MatchingEntries.Add(MakeShared<FJsonValueObject>(EntryObject));
					}
					else
					{
						TSharedPtr<FJsonObject> SummaryObject = MakeShared<FJsonObject>();
						SummaryObject->SetStringField(TEXT("key"), ExistingKey);
						FString Summary;
						FString Status;
						FString NextStep;
						FString UpdatedAtUtc;
						EntryObject->TryGetStringField(TEXT("summary"), Summary);
						EntryObject->TryGetStringField(TEXT("status"), Status);
						EntryObject->TryGetStringField(TEXT("nextStep"), NextStep);
						EntryObject->TryGetStringField(TEXT("updatedAtUtc"), UpdatedAtUtc);
						SummaryObject->SetStringField(TEXT("summary"), Summary);
						SummaryObject->SetStringField(TEXT("status"), Status);
						SummaryObject->SetStringField(TEXT("nextStep"), NextStep);
						SummaryObject->SetStringField(TEXT("updatedAtUtc"), UpdatedAtUtc);
						MatchingEntries.Add(MakeShared<FJsonValueObject>(SummaryObject));
					}
				}
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), TEXT("project_memory_read"));
			StructuredContent->SetStringField(TEXT("path"), GetProjectMemoryFilePath());
			StructuredContent->SetStringField(TEXT("key"), Key);
			StructuredContent->SetNumberField(TEXT("entryCount"), MatchingEntries.Num());
			StructuredContent->SetArrayField(TEXT("entries"), MatchingEntries);

			const FString KeySuffix = Key.IsEmpty() ? FString() : FString::Printf(TEXT(" for key '%s'"), *Key);
			return MakeExecutionResult(
				FString::Printf(TEXT("Read %d project memory entr%s%s."),
					MatchingEntries.Num(),
					MatchingEntries.Num() == 1 ? TEXT("y") : TEXT("ies"),
					*KeySuffix),
				StructuredContent,
				false);
		}

		TArray<TSharedPtr<FJsonValue>> GetMemoryEntryTags(const TSharedPtr<FJsonObject>& EntryObject)
		{
			TArray<TSharedPtr<FJsonValue>> TagValues;
			const TArray<TSharedPtr<FJsonValue>>* ExistingTags = nullptr;
			if (EntryObject.IsValid() && EntryObject->TryGetArrayField(TEXT("tags"), ExistingTags) && ExistingTags)
			{
				TagValues = *ExistingTags;
			}
			return TagValues;
		}

		bool MemoryEntryHasTag(const TSharedPtr<FJsonObject>& EntryObject, const FString& Tag)
		{
			if (!EntryObject.IsValid() || Tag.TrimStartAndEnd().IsEmpty())
			{
				return true;
			}

			const TArray<TSharedPtr<FJsonValue>> Tags = GetMemoryEntryTags(EntryObject);
			for (const TSharedPtr<FJsonValue>& TagValue : Tags)
			{
				if (TagValue.IsValid() && TagValue->Type == EJson::String && TagValue->AsString().Equals(Tag, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
			return false;
		}

		TSharedPtr<FJsonObject> MakeProjectMemorySummary(const TSharedPtr<FJsonObject>& EntryObject, bool bIncludeContent)
		{
			TSharedPtr<FJsonObject> SummaryObject = MakeShared<FJsonObject>();
			if (!EntryObject.IsValid())
			{
				return SummaryObject;
			}

			FString Key;
			FString Summary;
			FString Status;
			FString NextStep;
			FString UpdatedAtUtc;
			EntryObject->TryGetStringField(TEXT("key"), Key);
			EntryObject->TryGetStringField(TEXT("summary"), Summary);
			EntryObject->TryGetStringField(TEXT("status"), Status);
			EntryObject->TryGetStringField(TEXT("nextStep"), NextStep);
			EntryObject->TryGetStringField(TEXT("updatedAtUtc"), UpdatedAtUtc);
			SummaryObject->SetStringField(TEXT("key"), Key);
			SummaryObject->SetStringField(TEXT("summary"), Summary);
			SummaryObject->SetStringField(TEXT("status"), Status);
			SummaryObject->SetStringField(TEXT("nextStep"), NextStep);
			SummaryObject->SetStringField(TEXT("updatedAtUtc"), UpdatedAtUtc);
			SummaryObject->SetArrayField(TEXT("tags"), GetMemoryEntryTags(EntryObject));
			if (bIncludeContent)
			{
				const TSharedPtr<FJsonObject>* ContentObject = nullptr;
				if (EntryObject->TryGetObjectField(TEXT("content"), ContentObject) && ContentObject && (*ContentObject).IsValid())
				{
					SummaryObject->SetObjectField(TEXT("content"), *ContentObject);
				}
			}
			return SummaryObject;
		}

		FUnrealMcpExecutionResult ProjectMemoryView(const FJsonObject& Arguments)
		{
			FString KeyFilter;
			FString StatusFilter;
			FString TagFilter;
			bool bIncludeContent = false;
			bool bSortDescending = true;
			double MaxEntriesDouble = 50.0;
			Arguments.TryGetStringField(TEXT("keyFilter"), KeyFilter);
			Arguments.TryGetStringField(TEXT("status"), StatusFilter);
			Arguments.TryGetStringField(TEXT("tag"), TagFilter);
			Arguments.TryGetBoolField(TEXT("includeContent"), bIncludeContent);
			Arguments.TryGetBoolField(TEXT("sortDescending"), bSortDescending);
			Arguments.TryGetNumberField(TEXT("maxEntries"), MaxEntriesDouble);
			KeyFilter = KeyFilter.TrimStartAndEnd();
			StatusFilter = StatusFilter.TrimStartAndEnd();
			TagFilter = TagFilter.TrimStartAndEnd();
			const int32 MaxEntries = FMath::Clamp(static_cast<int32>(MaxEntriesDouble), 1, 500);

			FString FailureReason;
			TSharedPtr<FJsonObject> MemoryObject;
			if (!LoadProjectMemory(MemoryObject, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			TArray<TSharedPtr<FJsonObject>> MatchedObjects;
			const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
			if (MemoryObject->TryGetArrayField(TEXT("entries"), Entries) && Entries)
			{
				for (const TSharedPtr<FJsonValue>& EntryValue : *Entries)
				{
					if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object || !EntryValue->AsObject().IsValid())
					{
						continue;
					}

					TSharedPtr<FJsonObject> EntryObject = EntryValue->AsObject();
					FString ExistingKey;
					FString ExistingStatus;
					EntryObject->TryGetStringField(TEXT("key"), ExistingKey);
					EntryObject->TryGetStringField(TEXT("status"), ExistingStatus);
					if (!KeyFilter.IsEmpty() && !ExistingKey.Contains(KeyFilter, ESearchCase::IgnoreCase))
					{
						continue;
					}
					if (!StatusFilter.IsEmpty() && !ExistingStatus.Equals(StatusFilter, ESearchCase::IgnoreCase))
					{
						continue;
					}
					if (!MemoryEntryHasTag(EntryObject, TagFilter))
					{
						continue;
					}
					MatchedObjects.Add(EntryObject);
				}
			}

			MatchedObjects.Sort([bSortDescending](const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
			{
				FString ATime;
				FString BTime;
				if (A.IsValid())
				{
					A->TryGetStringField(TEXT("updatedAtUtc"), ATime);
				}
				if (B.IsValid())
				{
					B->TryGetStringField(TEXT("updatedAtUtc"), BTime);
				}
				return bSortDescending ? ATime > BTime : ATime < BTime;
			});

			TArray<TSharedPtr<FJsonValue>> Results;
			const int32 ResultCount = FMath::Min(MaxEntries, MatchedObjects.Num());
			for (int32 Index = 0; Index < ResultCount; ++Index)
			{
				Results.Add(MakeShared<FJsonValueObject>(MakeProjectMemorySummary(MatchedObjects[Index], bIncludeContent)));
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), TEXT("project_memory_view"));
			StructuredContent->SetStringField(TEXT("path"), GetProjectMemoryFilePath());
			StructuredContent->SetStringField(TEXT("keyFilter"), KeyFilter);
			StructuredContent->SetStringField(TEXT("status"), StatusFilter);
			StructuredContent->SetStringField(TEXT("tag"), TagFilter);
			StructuredContent->SetBoolField(TEXT("includeContent"), bIncludeContent);
			StructuredContent->SetNumberField(TEXT("matchedCount"), MatchedObjects.Num());
			StructuredContent->SetNumberField(TEXT("returnedCount"), Results.Num());
			StructuredContent->SetArrayField(TEXT("entries"), Results);

			return MakeExecutionResult(
				FString::Printf(TEXT("Viewed %d project memory entr%s (%d returned)."),
					MatchedObjects.Num(),
					MatchedObjects.Num() == 1 ? TEXT("y") : TEXT("ies"),
					Results.Num()),
				StructuredContent,
				false);
		}

		void JsonObjectMergeInto(TSharedPtr<FJsonObject>& TargetObject, const TSharedPtr<FJsonObject>& PatchObject)
		{
			if (!TargetObject.IsValid())
			{
				TargetObject = MakeShared<FJsonObject>();
			}
			if (!PatchObject.IsValid())
			{
				return;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PatchObject->Values)
			{
				TargetObject->SetField(Pair.Key, Pair.Value);
			}
		}

		FUnrealMcpExecutionResult ProjectMemoryEdit(const FJsonObject& Arguments)
		{
			FString Key;
			FString Summary;
			FString Status;
			FString NextStep;
			FString ContentJson;
			FString ContentMode = TEXT("merge");
			FString TagsMode = TEXT("replace");
			bool bCreateIfMissing = false;
			bool bDryRun = false;
			Arguments.TryGetStringField(TEXT("key"), Key);
			Arguments.TryGetStringField(TEXT("summary"), Summary);
			Arguments.TryGetStringField(TEXT("status"), Status);
			Arguments.TryGetStringField(TEXT("nextStep"), NextStep);
			Arguments.TryGetStringField(TEXT("contentJson"), ContentJson);
			Arguments.TryGetStringField(TEXT("contentMode"), ContentMode);
			Arguments.TryGetStringField(TEXT("tagsMode"), TagsMode);
			Arguments.TryGetBoolField(TEXT("createIfMissing"), bCreateIfMissing);
			Arguments.TryGetBoolField(TEXT("dryRun"), bDryRun);
			Key = Key.TrimStartAndEnd();
			ContentMode = ContentMode.TrimStartAndEnd().ToLower();
			TagsMode = TagsMode.TrimStartAndEnd().ToLower();
			if (Key.IsEmpty())
			{
				return MakeExecutionResult(TEXT("key is required."), nullptr, true);
			}

			TArray<FString> Tags;
			const bool bHasTags = TryGetStringArrayField(Arguments, TEXT("tags"), Tags);

			TSharedPtr<FJsonObject> PatchContent;
			if (!ContentJson.TrimStartAndEnd().IsEmpty())
			{
				if (!LoadJsonObject(ContentJson, PatchContent) || !PatchContent.IsValid())
				{
					return MakeExecutionResult(TEXT("contentJson must be a valid JSON object."), nullptr, true);
				}
			}

			FString FailureReason;
			TSharedPtr<FJsonObject> MemoryObject;
			if (!LoadProjectMemory(MemoryObject, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			TArray<TSharedPtr<FJsonValue>> Entries;
			const TArray<TSharedPtr<FJsonValue>>* ExistingEntries = nullptr;
			if (MemoryObject->TryGetArrayField(TEXT("entries"), ExistingEntries) && ExistingEntries)
			{
				Entries = *ExistingEntries;
			}

			int32 ExistingIndex = INDEX_NONE;
			TSharedPtr<FJsonObject> EntryObject;
			for (int32 Index = 0; Index < Entries.Num(); ++Index)
			{
				if (!Entries[Index].IsValid() || Entries[Index]->Type != EJson::Object || !Entries[Index]->AsObject().IsValid())
				{
					continue;
				}
				FString ExistingKey;
				Entries[Index]->AsObject()->TryGetStringField(TEXT("key"), ExistingKey);
				if (ExistingKey == Key)
				{
					ExistingIndex = Index;
					EntryObject = Entries[Index]->AsObject();
					break;
				}
			}

			if (!EntryObject.IsValid())
			{
				if (!bCreateIfMissing)
				{
					return MakeExecutionResult(FString::Printf(TEXT("No project memory entry exists for key '%s'."), *Key), nullptr, true);
				}
				EntryObject = MakeShared<FJsonObject>();
				EntryObject->SetStringField(TEXT("key"), Key);
				EntryObject->SetObjectField(TEXT("content"), MakeShared<FJsonObject>());
			}

			TSharedPtr<FJsonObject> BeforeObject = MakeProjectMemorySummary(EntryObject, true);
			if (Arguments.HasField(TEXT("summary")))
			{
				EntryObject->SetStringField(TEXT("summary"), Summary);
			}
			if (Arguments.HasField(TEXT("status")))
			{
				EntryObject->SetStringField(TEXT("status"), Status);
			}
			if (Arguments.HasField(TEXT("nextStep")))
			{
				EntryObject->SetStringField(TEXT("nextStep"), NextStep);
			}
			if (PatchContent.IsValid())
			{
				if (ContentMode == TEXT("replace"))
				{
					EntryObject->SetObjectField(TEXT("content"), PatchContent);
				}
				else
				{
					const TSharedPtr<FJsonObject>* ExistingContent = nullptr;
					TSharedPtr<FJsonObject> ContentObject = MakeShared<FJsonObject>();
					if (EntryObject->TryGetObjectField(TEXT("content"), ExistingContent) && ExistingContent && (*ExistingContent).IsValid())
					{
						ContentObject = *ExistingContent;
					}
					JsonObjectMergeInto(ContentObject, PatchContent);
					EntryObject->SetObjectField(TEXT("content"), ContentObject);
				}
			}
			if (bHasTags)
			{
				TArray<TSharedPtr<FJsonValue>> ExistingTagValues = TagsMode == TEXT("append") || TagsMode == TEXT("remove")
					? GetMemoryEntryTags(EntryObject)
					: TArray<TSharedPtr<FJsonValue>>();
				if (TagsMode == TEXT("remove"))
				{
					ExistingTagValues.RemoveAll([&Tags](const TSharedPtr<FJsonValue>& TagValue)
					{
						if (!TagValue.IsValid() || TagValue->Type != EJson::String)
						{
							return false;
						}
						for (const FString& Tag : Tags)
						{
							if (TagValue->AsString().Equals(Tag, ESearchCase::IgnoreCase))
							{
								return true;
							}
						}
						return false;
					});
				}
				else
				{
					for (const FString& Tag : Tags)
					{
						const FString CleanTag = Tag.TrimStartAndEnd();
						if (CleanTag.IsEmpty())
						{
							continue;
						}
						bool bAlreadyExists = false;
						for (const TSharedPtr<FJsonValue>& ExistingTagValue : ExistingTagValues)
						{
							if (ExistingTagValue.IsValid() && ExistingTagValue->Type == EJson::String && ExistingTagValue->AsString().Equals(CleanTag, ESearchCase::IgnoreCase))
							{
								bAlreadyExists = true;
								break;
							}
						}
						if (!bAlreadyExists)
						{
							ExistingTagValues.Add(MakeShared<FJsonValueString>(CleanTag));
						}
					}
				}
				EntryObject->SetArrayField(TEXT("tags"), ExistingTagValues);
			}

			const FString Timestamp = FDateTime::UtcNow().ToIso8601();
			EntryObject->SetStringField(TEXT("updatedAtUtc"), Timestamp);
			if (ExistingIndex == INDEX_NONE)
			{
				Entries.Add(MakeShared<FJsonValueObject>(EntryObject));
			}
			else
			{
				Entries[ExistingIndex] = MakeShared<FJsonValueObject>(EntryObject);
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), TEXT("project_memory_edit"));
			StructuredContent->SetStringField(TEXT("path"), GetProjectMemoryFilePath());
			StructuredContent->SetStringField(TEXT("key"), Key);
			StructuredContent->SetBoolField(TEXT("created"), ExistingIndex == INDEX_NONE);
			StructuredContent->SetBoolField(TEXT("dryRun"), bDryRun);
			StructuredContent->SetObjectField(TEXT("before"), BeforeObject);
			StructuredContent->SetObjectField(TEXT("after"), MakeProjectMemorySummary(EntryObject, true));

			if (!bDryRun)
			{
				MemoryObject->SetStringField(TEXT("updatedAtUtc"), Timestamp);
				MemoryObject->SetArrayField(TEXT("entries"), Entries);
				if (!SaveProjectMemory(MemoryObject, FailureReason))
				{
					return MakeExecutionResult(FailureReason, StructuredContent, true);
				}
			}

			return MakeExecutionResult(
				bDryRun
					? FString::Printf(TEXT("Dry run edited project memory key '%s'."), *Key)
					: FString::Printf(TEXT("Edited project memory key '%s'."), *Key),
				StructuredContent,
				false);
		}

		FUnrealMcpExecutionResult ProjectMemoryDelete(const FJsonObject& Arguments)
		{
			FString Key;
			bool bDryRun = true;
			Arguments.TryGetStringField(TEXT("key"), Key);
			Arguments.TryGetBoolField(TEXT("dryRun"), bDryRun);
			Key = Key.TrimStartAndEnd();
			if (Key.IsEmpty())
			{
				return MakeExecutionResult(TEXT("key is required."), nullptr, true);
			}

			FString FailureReason;
			TSharedPtr<FJsonObject> MemoryObject;
			if (!LoadProjectMemory(MemoryObject, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			TArray<TSharedPtr<FJsonValue>> Entries;
			const TArray<TSharedPtr<FJsonValue>>* ExistingEntries = nullptr;
			if (MemoryObject->TryGetArrayField(TEXT("entries"), ExistingEntries) && ExistingEntries)
			{
				Entries = *ExistingEntries;
			}

			TSharedPtr<FJsonObject> DeletedEntry;
			const int32 RemovedCount = Entries.RemoveAll([&Key, &DeletedEntry](const TSharedPtr<FJsonValue>& EntryValue)
			{
				if (!EntryValue.IsValid() || EntryValue->Type != EJson::Object || !EntryValue->AsObject().IsValid())
				{
					return false;
				}
				FString ExistingKey;
				EntryValue->AsObject()->TryGetStringField(TEXT("key"), ExistingKey);
				if (ExistingKey == Key)
				{
					DeletedEntry = EntryValue->AsObject();
					return true;
				}
				return false;
			});

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), TEXT("project_memory_delete"));
			StructuredContent->SetStringField(TEXT("path"), GetProjectMemoryFilePath());
			StructuredContent->SetStringField(TEXT("key"), Key);
			StructuredContent->SetBoolField(TEXT("dryRun"), bDryRun);
			StructuredContent->SetNumberField(TEXT("removedCount"), RemovedCount);
			if (DeletedEntry.IsValid())
			{
				StructuredContent->SetObjectField(TEXT("deletedEntry"), MakeProjectMemorySummary(DeletedEntry, true));
			}

			if (RemovedCount == 0)
			{
				return MakeExecutionResult(FString::Printf(TEXT("No project memory entry exists for key '%s'."), *Key), StructuredContent, true);
			}

			if (!bDryRun)
			{
				MemoryObject->SetStringField(TEXT("updatedAtUtc"), FDateTime::UtcNow().ToIso8601());
				MemoryObject->SetArrayField(TEXT("entries"), Entries);
				if (!SaveProjectMemory(MemoryObject, FailureReason))
				{
					return MakeExecutionResult(FailureReason, StructuredContent, true);
				}
			}

			return MakeExecutionResult(
				bDryRun
					? FString::Printf(TEXT("Dry run would delete project memory key '%s'."), *Key)
					: FString::Printf(TEXT("Deleted project memory key '%s'."), *Key),
				StructuredContent,
				false);
		}

	bool TryExecuteMemoryTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult)
	{
		if (ToolName == TEXT("unreal.project_memory_write"))
		{
			OutResult = ProjectMemoryWrite(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.project_memory_read"))
		{
			OutResult = ProjectMemoryRead(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.project_memory_view"))
		{
			OutResult = ProjectMemoryView(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.project_memory_edit"))
		{
			OutResult = ProjectMemoryEdit(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.project_memory_delete"))
		{
			OutResult = ProjectMemoryDelete(Arguments);
			return true;
		}

		return false;
	}
}
