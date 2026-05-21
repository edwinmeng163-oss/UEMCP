#include "UnrealMcpToolHandlerRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"
#include "UnrealMcpExtensionLifecycle.h"
#include "UnrealMcpToolRegistry.h"
#include "UnrealMcpUserToolLock.h"
#include "UnrealMcpUserToolRegistry.h"

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
			if (Category == TEXT("material"))
			{
				return TEXT("UnrealMcpMaterialInstanceTools.cpp");
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
			if (Category == TEXT("task-atlas"))
			{
				return TEXT("UnrealMcpTaskAtlasTools.cpp");
			}
			if (Category == TEXT("verification"))
			{
				return TEXT("UnrealMcpAutomationTools.cpp");
			}
			if (Category == TEXT("widget"))
			{
				return TEXT("UnrealMcpWidgetTools.cpp");
			}
			return TEXT("<unknown category dispatcher>");
		}

		FString ResolveImplementationSource(const FToolRegistryEntry& ToolEntry)
		{
			if (ToolEntry.ImplementationTrack == EToolImplementationTrack::Python)
			{
				return ToolEntry.PythonHandlerPath.IsEmpty() ? FString(TEXT("<missing python handler>")) : ToolEntry.PythonHandlerPath;
			}
			if (ToolEntry.Category == TEXT("verification")
				&& (ToolEntry.Name == TEXT("unreal.editor_diagnostics") || ToolEntry.HandlerName == TEXT("unreal.editor_diagnostics")))
			{
				return TEXT("UnrealMcpDiagnosticsTools.cpp");
			}
			if (ToolEntry.Category == TEXT("verification")
				&& (ToolEntry.Name == TEXT("unreal.pie_smoke") || ToolEntry.HandlerName == TEXT("unreal.pie_smoke")))
			{
				return TEXT("UnrealMcpPieSmokeTools.cpp");
			}
			return ResolveCategoryDispatcherSource(ToolEntry.Category);
		}

		TArray<TSharedPtr<FJsonValue>> MakeStringArrayValues(const TArray<FString>& Strings)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FString& StringValue : Strings)
			{
				Values.Add(MakeShared<FJsonValueString>(StringValue));
			}
			return Values;
		}

		void MergePythonImplementationMetadata(FToolHandlerRegistryEntry& HandlerEntry, const FToolRegistryEntry& ToolEntry)
		{
			if (ToolEntry.ImplementationTrack != EToolImplementationTrack::Python)
			{
				return;
			}

			HandlerEntry.ImplementationTrack = EToolImplementationTrack::Python;
			HandlerEntry.PythonHandlerPath = ToolEntry.PythonHandlerPath;
			HandlerEntry.PythonHandlerSha256 = ToolEntry.PythonHandlerSha256;
			for (const FString& ModuleName : ToolEntry.PythonImportAllowList)
			{
				HandlerEntry.PythonImportAllowList.AddUnique(ModuleName);
			}
			HandlerEntry.SourceFile = ResolveImplementationSource(ToolEntry);
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
				MergePythonImplementationMetadata(ExistingEntry, ToolEntry);
				return;
			}

			FToolHandlerRegistryEntry Entry;
			Entry.HandlerName = HandlerName;
			Entry.Category = ToolEntry.Category;
			Entry.SourceFile = ResolveImplementationSource(ToolEntry);
			Entry.ImplementationTrack = ToolEntry.ImplementationTrack;
			Entry.PythonHandlerPath = ToolEntry.PythonHandlerPath;
			Entry.PythonHandlerSha256 = ToolEntry.PythonHandlerSha256;
			Entry.PythonImportAllowList = ToolEntry.PythonImportAllowList;
			Entry.ToolNames.Add(ToolEntry.Name);
			Entry.bLoadedFromExplicitRegistry = ToolEntry.bLoadedFromExplicitRegistry;
			Entry.bLoadedFromDescriptor = ToolEntry.bLoadedFromDescriptor;
			HandlerToIndex.Add(HandlerName, Entries.Num());
			Entries.Add(MoveTemp(Entry));
		}

		FString MakeUserRegistryRelativeHandlerPath(const UserRegistry::FUserToolEntry& UserToolEntry)
		{
			FString NormalizedScaffoldDir = UserToolEntry.ScaffoldDir;
			FPaths::NormalizeFilename(NormalizedScaffoldDir);
			NormalizedScaffoldDir.RemoveFromEnd(TEXT("/"));
			return FPaths::Combine(
				::UnrealMcp::Extension::UserPyToolsRelativeRoot,
				FPaths::GetCleanFilename(NormalizedScaffoldDir),
				TEXT("main.py"));
		}

		FToolHandlerRegistryEntry MakeUserHandlerEntry(const UserRegistry::FUserToolEntry& UserToolEntry)
		{
			FToolHandlerRegistryEntry Entry;
			Entry.HandlerName = UserToolEntry.ToolName;
			Entry.Category = TEXT("user");
			Entry.SourceFile = UserToolEntry.PythonHandlerPath;
			Entry.ImplementationTrack = EToolImplementationTrack::Python;
			Entry.PythonHandlerPath = MakeUserRegistryRelativeHandlerPath(UserToolEntry);
			Entry.PythonHandlerSha256 = UserToolEntry.PythonHandlerSha256;
			Entry.PythonImportAllowList = UserToolEntry.ImportAllowlist;
			Entry.ToolNames.Add(UserToolEntry.ToolName);
			Entry.bLoadedFromExplicitRegistry = false;
			Entry.bLoadedFromDescriptor = false;
			Entry.bLoadedFromUserRegistry = true;
			return Entry;
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
			for (const FString& ToolName : Entry.ToolNames)
			{
				if (ToolName.Equals(HandlerName, ESearchCase::CaseSensitive))
				{
					return &Entry;
				}
			}
		}

		UserToolLock::FSharedGuard UserRegistryGuard;
		if (const UserRegistry::FUserToolEntry* UserToolEntry = UserRegistry::FindUserTool(HandlerName))
		{
			static thread_local FToolHandlerRegistryEntry UserHandlerEntry;
			UserHandlerEntry = MakeUserHandlerEntry(*UserToolEntry);
			return &UserHandlerEntry;
		}

		return nullptr;
	}

	bool IsRegisteredToolHandler(const FString& HandlerName)
	{
		return FindToolHandlerRegistryEntry(HandlerName) != nullptr;
	}

	Extension::EHandlerKind ResolveToolHandlerKind(const FString& ToolName)
	{
		if (const FToolHandlerRegistryEntry* HandlerEntry = FindToolHandlerRegistryEntry(ResolveToolHandlerName(ToolName)))
		{
			return HandlerEntry->ImplementationTrack == EToolImplementationTrack::Python
				? Extension::EHandlerKind::PythonBridge
				: Extension::EHandlerKind::CppDispatcher;
		}

		return Extension::EHandlerKind::None;
	}

	TSharedPtr<FJsonObject> MakeToolHandlerRegistryStatusObject()
	{
		TArray<TSharedPtr<FJsonValue>> HandlerValues;
		TMap<FString, int32> CategoryCounts;
		TMap<FString, int32> ImplementationTrackCounts;
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
			EntryObject->SetStringField(TEXT("implementationTrack"), UnrealMcp::LexToString(Entry.ImplementationTrack));
			EntryObject->SetStringField(TEXT("pythonHandlerPath"), Entry.PythonHandlerPath);
			EntryObject->SetStringField(TEXT("pythonHandlerSha256"), Entry.PythonHandlerSha256);
			EntryObject->SetArrayField(TEXT("pythonImportAllowList"), MakeStringArrayValues(Entry.PythonImportAllowList));
			EntryObject->SetBoolField(TEXT("explicitRegistryBacked"), Entry.bLoadedFromExplicitRegistry);
			EntryObject->SetBoolField(TEXT("descriptorBacked"), Entry.bLoadedFromDescriptor);
			EntryObject->SetBoolField(TEXT("userRegistryBacked"), Entry.bLoadedFromUserRegistry);
			EntryObject->SetNumberField(TEXT("routedToolCount"), Entry.ToolNames.Num());
			EntryObject->SetArrayField(TEXT("routedTools"), RoutedToolValues);
			HandlerValues.Add(MakeShared<FJsonValueObject>(EntryObject));
			CategoryCounts.FindOrAdd(Entry.Category)++;
			ImplementationTrackCounts.FindOrAdd(UnrealMcp::LexToString(Entry.ImplementationTrack))++;
		}

		TSharedPtr<FJsonObject> CategoryCountsObject = MakeShared<FJsonObject>();
		for (const TPair<FString, int32>& Pair : CategoryCounts)
		{
			CategoryCountsObject->SetNumberField(Pair.Key, Pair.Value);
		}

		TSharedPtr<FJsonObject> ImplementationTrackCountsObject = MakeShared<FJsonObject>();
		for (const TPair<FString, int32>& Pair : ImplementationTrackCounts)
		{
			ImplementationTrackCountsObject->SetNumberField(Pair.Key, Pair.Value);
		}

		TSharedPtr<FJsonObject> StatusObject = MakeShared<FJsonObject>();
		StatusObject->SetNumberField(TEXT("handlerCount"), GetToolHandlerRegistryEntries().Num());
		StatusObject->SetNumberField(TEXT("coreHandlerCount"), GetToolHandlerRegistryEntries().Num());
		StatusObject->SetNumberField(TEXT("userHandlerOverlayCount"), UserRegistry::GetUserToolCount());
		StatusObject->SetObjectField(TEXT("categoryCounts"), CategoryCountsObject);
		StatusObject->SetObjectField(TEXT("implementationTrackCounts"), ImplementationTrackCountsObject);
		StatusObject->SetArrayField(TEXT("handlers"), HandlerValues);
		StatusObject->SetStringField(TEXT("source"), GetToolRegistrySourcePath());
		return StatusObject;
	}
}
