#include "UnrealMcpToolHandlerRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UnrealMcpToolRegistry.h"

namespace UnrealMcp
{
	namespace
	{
		FString ResolveCategoryDispatcherSource(const FString& Category)
		{
			if (Category == TEXT("actors"))
			{
				return TEXT("UnrealMcpActorTools.cpp");
			}
			if (Category == TEXT("blueprint"))
			{
				return TEXT("UnrealMcpBlueprintTools.cpp");
			}
			if (Category == TEXT("editor"))
			{
				return TEXT("UnrealMcpEditorTools.cpp");
			}
			if (Category == TEXT("memory"))
			{
				return TEXT("UnrealMcpMemoryTools.cpp");
			}
			if (Category == TEXT("scaffold"))
			{
				return TEXT("UnrealMcpScaffoldTools.cpp");
			}
			if (Category == TEXT("self-extension"))
			{
				return TEXT("UnrealMcpSelfExtensionTools.cpp");
			}
			if (Category == TEXT("skills"))
			{
				return TEXT("UnrealMcpSkillTools.cpp");
			}
			if (Category == TEXT("widget"))
			{
				return TEXT("UnrealMcpWidgetTools.cpp");
			}
			return TEXT("<unknown category dispatcher>");
		}

		void AddOrUpdateHandlerEntry(
			TArray<FToolHandlerRegistryEntry>& Entries,
			TMap<FString, int32>& HandlerToIndex,
			const FToolRegistryEntry& ToolEntry)
		{
			const FString HandlerName = ToolEntry.HandlerName.IsEmpty() ? ToolEntry.Name : ToolEntry.HandlerName;
			if (HandlerName.IsEmpty())
			{
				return;
			}

			if (int32* ExistingIndex = HandlerToIndex.Find(HandlerName))
			{
				FToolHandlerRegistryEntry& ExistingEntry = Entries[*ExistingIndex];
				ExistingEntry.bLoadedFromExplicitRegistry |= ToolEntry.bLoadedFromExplicitRegistry;
				ExistingEntry.bLoadedFromDescriptor |= ToolEntry.bLoadedFromDescriptor;
				ExistingEntry.ToolNames.AddUnique(ToolEntry.Name);
				return;
			}

			FToolHandlerRegistryEntry Entry;
			Entry.HandlerName = HandlerName;
			Entry.Category = ToolEntry.Category;
			Entry.SourceFile = ResolveCategoryDispatcherSource(ToolEntry.Category);
			Entry.ToolNames.Add(ToolEntry.Name);
			Entry.bLoadedFromExplicitRegistry = ToolEntry.bLoadedFromExplicitRegistry;
			Entry.bLoadedFromDescriptor = ToolEntry.bLoadedFromDescriptor;
			HandlerToIndex.Add(HandlerName, Entries.Num());
			Entries.Add(MoveTemp(Entry));
		}
	}

	const TArray<FToolHandlerRegistryEntry>& GetToolHandlerRegistryEntries()
	{
		static const TArray<FToolHandlerRegistryEntry> Entries = []()
		{
			TArray<FToolHandlerRegistryEntry> Result;
			TMap<FString, int32> HandlerToIndex;
			for (const FToolRegistryEntry& ToolEntry : GetToolRegistryEntries())
			{
				AddOrUpdateHandlerEntry(Result, HandlerToIndex, ToolEntry);
			}
			return Result;
		}();
		return Entries;
	}

	const FToolHandlerRegistryEntry* FindToolHandlerRegistryEntry(const FString& HandlerName)
	{
		for (const FToolHandlerRegistryEntry& Entry : GetToolHandlerRegistryEntries())
		{
			if (Entry.HandlerName.Equals(HandlerName, ESearchCase::CaseSensitive))
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	bool IsRegisteredToolHandler(const FString& HandlerName)
	{
		return FindToolHandlerRegistryEntry(HandlerName) != nullptr;
	}

	TSharedPtr<FJsonObject> MakeToolHandlerRegistryStatusObject()
	{
		TArray<TSharedPtr<FJsonValue>> HandlerValues;
		TMap<FString, int32> CategoryCounts;
		for (const FToolHandlerRegistryEntry& Entry : GetToolHandlerRegistryEntries())
		{
			TArray<TSharedPtr<FJsonValue>> RoutedToolValues;
			for (const FString& ToolName : Entry.ToolNames)
			{
				RoutedToolValues.Add(MakeShared<FJsonValueString>(ToolName));
			}

			TSharedPtr<FJsonObject> EntryObject = MakeShared<FJsonObject>();
			EntryObject->SetStringField(TEXT("handlerName"), Entry.HandlerName);
			EntryObject->SetStringField(TEXT("category"), Entry.Category);
			EntryObject->SetStringField(TEXT("sourceFile"), Entry.SourceFile);
			EntryObject->SetBoolField(TEXT("explicitRegistryBacked"), Entry.bLoadedFromExplicitRegistry);
			EntryObject->SetBoolField(TEXT("descriptorBacked"), Entry.bLoadedFromDescriptor);
			EntryObject->SetNumberField(TEXT("routedToolCount"), Entry.ToolNames.Num());
			EntryObject->SetArrayField(TEXT("routedTools"), RoutedToolValues);
			HandlerValues.Add(MakeShared<FJsonValueObject>(EntryObject));
			CategoryCounts.FindOrAdd(Entry.Category)++;
		}

		TSharedPtr<FJsonObject> CategoryCountsObject = MakeShared<FJsonObject>();
		for (const TPair<FString, int32>& Pair : CategoryCounts)
		{
			CategoryCountsObject->SetNumberField(Pair.Key, Pair.Value);
		}

		TSharedPtr<FJsonObject> StatusObject = MakeShared<FJsonObject>();
		StatusObject->SetNumberField(TEXT("handlerCount"), GetToolHandlerRegistryEntries().Num());
		StatusObject->SetObjectField(TEXT("categoryCounts"), CategoryCountsObject);
		StatusObject->SetArrayField(TEXT("handlers"), HandlerValues);
		StatusObject->SetStringField(TEXT("source"), GetToolRegistrySourcePath());
		return StatusObject;
	}
}
