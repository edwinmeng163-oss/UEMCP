#include "UnrealMcpSelfExtensionTools.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealMcpToolRegistrar.h"
#include "UnrealMcpToolRegistry.h"

namespace UnrealMcp
{
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);
	int32 GetPositiveIntArgument(const FJsonObject& Arguments, const FString& FieldName, int32 DefaultValue);
	TArray<TSharedPtr<FJsonValue>> MakeJsonStringArray(const TArray<FString>& Values);
	bool TryGetStringArrayField(const FJsonObject& Arguments, const FString& FieldName, TArray<FString>& OutValues);
	FString LexToString(EToolRiskLevel RiskLevel);

	namespace
	{
		static constexpr int32 DefaultKnowledgeMaxCards = 2000;
		static constexpr int32 DefaultKnowledgeChunkChars = 1800;
		static constexpr int32 DefaultKnowledgeOverlapChars = 160;
		static constexpr int32 DefaultKnowledgeSearchLimit = 8;
		static constexpr int32 DefaultKnowledgeExcerptChars = 420;

		struct FKnowledgeCard
		{
			FString CardId;
			FString SourceId;
			FString Title;
			FString Category;
			TArray<FString> Tags;
			FString SourceKind;
			FString SourcePath;
			FString Url;
			FString Text;
			int32 ChunkIndex = 0;
			int32 TextLength = 0;
		};

		FString GetKnowledgeIndexRoot()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/KnowledgeIndex")));
		}

		FString GetKnowledgeSourceRoot()
		{
			return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UnrealMcp/KnowledgeSources")));
		}

		FString NormalizePathForJson(const FString& Path)
		{
			FString Normalized = FPaths::ConvertRelativePathToFull(Path);
			FPaths::NormalizeFilename(Normalized);
			return Normalized;
		}

		FString MakeProjectRelativePath(const FString& Path)
		{
			FString FullPath = NormalizePathForJson(Path);
			FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			FPaths::NormalizeDirectoryName(ProjectDir);
			FString Relative = FullPath;
			if (FPaths::MakePathRelativeTo(Relative, *ProjectDir))
			{
				return Relative;
			}
			return FullPath;
		}

		bool LoadJsonObjectFromString(const FString& JsonText, TSharedPtr<FJsonObject>& OutObject)
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
			return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
		}

		bool WriteJsonObjectToFile(const TSharedPtr<FJsonObject>& Object, const FString& Path, FString& OutFailureReason)
		{
			if (!Object.IsValid())
			{
				OutFailureReason = TEXT("Cannot write an invalid JSON object.");
				return false;
			}

			FString JsonText;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
			if (!FJsonSerializer::Serialize(Object.ToSharedRef(), Writer))
			{
				OutFailureReason = TEXT("Failed to serialize JSON object.");
				return false;
			}

			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			if (!FFileHelper::SaveStringToFile(JsonText, *Path))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to write '%s'."), *Path);
				return false;
			}
			return true;
		}

		FString JsonObjectToCondensedString(const TSharedPtr<FJsonObject>& Object)
		{
			FString JsonText;
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonText);
			FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
			return JsonText;
		}

		FString SanitizeKnowledgeId(const FString& Value)
		{
			FString Output;
			for (TCHAR Character : Value.ToLower())
			{
				if ((Character >= TEXT('a') && Character <= TEXT('z'))
					|| (Character >= TEXT('0') && Character <= TEXT('9'))
					|| Character == TEXT('_')
					|| Character == TEXT('-')
					|| Character == TEXT('.'))
				{
					Output.AppendChar(Character);
				}
				else
				{
					Output.AppendChar(TEXT('-'));
				}
			}

			while (Output.Contains(TEXT("--")))
			{
				Output.ReplaceInline(TEXT("--"), TEXT("-"));
			}
			Output.TrimStartAndEndInline();
			bool bTrimmedTrailingDash = false;
			Output.TrimCharInline(static_cast<TCHAR>('-'), &bTrimmedTrailingDash);
			return Output.IsEmpty() ? TEXT("knowledge-card") : Output;
		}

		TArray<FString> ExtractSearchTokens(const FString& Text)
		{
			TArray<FString> Tokens;
			FString Current;
			for (TCHAR Character : Text.ToLower())
			{
				if (FChar::IsAlnum(Character) || Character == TEXT('_'))
				{
					Current.AppendChar(Character);
				}
				else
				{
					if (Current.Len() >= 2)
					{
						Tokens.AddUnique(Current);
					}
					Current.Reset();
				}
			}
			if (Current.Len() >= 2)
			{
				Tokens.AddUnique(Current);
			}
			return Tokens;
		}

		TArray<TSharedPtr<FJsonValue>> StringsToJsonArray(const TArray<FString>& Values)
		{
			return MakeJsonStringArray(Values);
		}

		bool TryGetStringArrayFromObject(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, TArray<FString>& OutValues)
		{
			OutValues.Reset();
			if (!Object.IsValid())
			{
				return false;
			}

			const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
			if (!Object->TryGetArrayField(FieldName, JsonArray) || !JsonArray)
			{
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Value : *JsonArray)
			{
				FString StringValue;
				if (Value.IsValid() && Value->TryGetString(StringValue))
				{
					OutValues.Add(StringValue);
				}
			}
			return true;
		}

		TSharedPtr<FJsonObject> CardToJsonObject(const FKnowledgeCard& Card)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("cardId"), Card.CardId);
			Object->SetStringField(TEXT("sourceId"), Card.SourceId);
			Object->SetStringField(TEXT("title"), Card.Title);
			Object->SetStringField(TEXT("category"), Card.Category);
			Object->SetArrayField(TEXT("tags"), StringsToJsonArray(Card.Tags));
			Object->SetStringField(TEXT("sourceKind"), Card.SourceKind);
			Object->SetStringField(TEXT("sourcePath"), Card.SourcePath);
			Object->SetStringField(TEXT("url"), Card.Url);
			Object->SetStringField(TEXT("text"), Card.Text);
			Object->SetNumberField(TEXT("chunkIndex"), Card.ChunkIndex);
			Object->SetNumberField(TEXT("textLength"), Card.TextLength);
			return Object;
		}

		bool JsonObjectToCard(const TSharedPtr<FJsonObject>& Object, FKnowledgeCard& OutCard)
		{
			if (!Object.IsValid())
			{
				return false;
			}

			Object->TryGetStringField(TEXT("cardId"), OutCard.CardId);
			Object->TryGetStringField(TEXT("sourceId"), OutCard.SourceId);
			Object->TryGetStringField(TEXT("title"), OutCard.Title);
			Object->TryGetStringField(TEXT("category"), OutCard.Category);
			Object->TryGetStringField(TEXT("sourceKind"), OutCard.SourceKind);
			Object->TryGetStringField(TEXT("sourcePath"), OutCard.SourcePath);
			Object->TryGetStringField(TEXT("url"), OutCard.Url);
			Object->TryGetStringField(TEXT("text"), OutCard.Text);
			TryGetStringArrayFromObject(Object, TEXT("tags"), OutCard.Tags);

			double NumberValue = 0.0;
			if (Object->TryGetNumberField(TEXT("chunkIndex"), NumberValue))
			{
				OutCard.ChunkIndex = static_cast<int32>(NumberValue);
			}
			if (Object->TryGetNumberField(TEXT("textLength"), NumberValue))
			{
				OutCard.TextLength = static_cast<int32>(NumberValue);
			}

			return !OutCard.CardId.IsEmpty() && !OutCard.Text.IsEmpty();
		}

		void AddCardsFromText(
			const FString& SourceId,
			const FString& Title,
			const FString& Category,
			const TArray<FString>& Tags,
			const FString& SourceKind,
			const FString& SourcePath,
			const FString& Url,
			const FString& Text,
			int32 MaxChunkChars,
			int32 OverlapChars,
			TArray<FKnowledgeCard>& OutCards)
		{
			const FString CleanText = Text.TrimStartAndEnd();
			if (CleanText.IsEmpty())
			{
				return;
			}

			const int32 SafeMaxChunkChars = FMath::Max(400, MaxChunkChars);
			const int32 SafeOverlapChars = FMath::Clamp(OverlapChars, 0, SafeMaxChunkChars / 2);
			int32 Offset = 0;
			int32 ChunkIndex = 0;
			while (Offset < CleanText.Len())
			{
				int32 ChunkLen = FMath::Min(SafeMaxChunkChars, CleanText.Len() - Offset);
				FString ChunkText = CleanText.Mid(Offset, ChunkLen).TrimStartAndEnd();
				if (!ChunkText.IsEmpty())
				{
					FKnowledgeCard Card;
					Card.SourceId = SourceId;
					Card.Title = Title;
					Card.Category = Category;
					Card.Tags = Tags;
					Card.SourceKind = SourceKind;
					Card.SourcePath = SourcePath;
					Card.Url = Url;
					Card.Text = ChunkText;
					Card.ChunkIndex = ChunkIndex;
					Card.TextLength = ChunkText.Len();
					Card.CardId = FString::Printf(TEXT("%s:%03d"), *SanitizeKnowledgeId(SourceId), ChunkIndex);
					OutCards.Add(MoveTemp(Card));
				}

				ChunkIndex++;
				if (Offset + ChunkLen >= CleanText.Len())
				{
					break;
				}
				Offset += FMath::Max(1, ChunkLen - SafeOverlapChars);
			}
		}

		void AddDocumentationJsonlCards(
			const FString& DocumentsJsonlPath,
			int32 MaxChunkChars,
			int32 OverlapChars,
			bool bSkipLowContent,
			TArray<FKnowledgeCard>& OutCards,
			int32& OutSkippedRows)
		{
			TArray<FString> Lines;
			if (!FFileHelper::LoadFileToStringArray(Lines, *DocumentsJsonlPath))
			{
				return;
			}

			const FString RootDir = FPaths::GetPath(DocumentsJsonlPath);
			for (const FString& Line : Lines)
			{
				if (Line.TrimStartAndEnd().IsEmpty())
				{
					continue;
				}

				TSharedPtr<FJsonObject> Row;
				if (!LoadJsonObjectFromString(Line, Row) || !Row.IsValid())
				{
					OutSkippedRows++;
					continue;
				}

				bool bLowContent = false;
				Row->TryGetBoolField(TEXT("lowContentWarning"), bLowContent);
				if (bSkipLowContent && bLowContent)
				{
					OutSkippedRows++;
					continue;
				}

				FString TextPath;
				if (!Row->TryGetStringField(TEXT("textPath"), TextPath) || TextPath.IsEmpty())
				{
					OutSkippedRows++;
					continue;
				}

				const FString FullTextPath = NormalizePathForJson(FPaths::Combine(RootDir, TextPath));
				FString Text;
				if (!FFileHelper::LoadFileToString(Text, *FullTextPath))
				{
					OutSkippedRows++;
					continue;
				}

				FString Id;
				FString Title;
				FString Category;
				FString Url;
				TArray<FString> Tags;
				Row->TryGetStringField(TEXT("id"), Id);
				Row->TryGetStringField(TEXT("title"), Title);
				Row->TryGetStringField(TEXT("category"), Category);
				Row->TryGetStringField(TEXT("url"), Url);
				TryGetStringArrayFromObject(Row, TEXT("tags"), Tags);
				if (Id.IsEmpty())
				{
					Id = FPaths::GetBaseFilename(FullTextPath);
				}
				if (Title.IsEmpty())
				{
					Title = Id;
				}
				if (Category.IsEmpty())
				{
					Category = TEXT("unreal-docs");
				}

				AddCardsFromText(
					Id,
					Title,
					Category,
					Tags,
					TEXT("official-docs"),
					MakeProjectRelativePath(FullTextPath),
					Url,
					Text,
					MaxChunkChars,
					OverlapChars,
					OutCards);
			}
		}

		void AddMarkdownFileCards(
			const FString& FilePath,
			const FString& SourceId,
			const FString& Category,
			const TArray<FString>& Tags,
			int32 MaxChunkChars,
			int32 OverlapChars,
			TArray<FKnowledgeCard>& OutCards)
		{
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *FilePath))
			{
				return;
			}

			const FString Title = FPaths::GetBaseFilename(FilePath);
			AddCardsFromText(
				SourceId,
				Title,
				Category,
				Tags,
				TEXT("versioned-doc"),
				MakeProjectRelativePath(FilePath),
				FString(),
				Text,
				MaxChunkChars,
				OverlapChars,
				OutCards);
		}

		void AddVersionedDocumentationCards(int32 MaxChunkChars, int32 OverlapChars, TArray<FKnowledgeCard>& OutCards)
		{
			const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			const TArray<FString> RootDocs = {
				FPaths::Combine(ProjectDir, TEXT("README.md")),
				FPaths::Combine(ProjectDir, TEXT("Plugins/UnrealMcp/README.md")),
				FPaths::Combine(ProjectDir, TEXT("Tools/UnrealMcpKnowledge/README.md")),
				FPaths::Combine(ProjectDir, TEXT("Tools/UnrealMcpTests/README.md"))
			};

			for (const FString& FilePath : RootDocs)
			{
				if (FPaths::FileExists(FilePath))
				{
					AddMarkdownFileCards(
						FilePath,
						SanitizeKnowledgeId(MakeProjectRelativePath(FilePath)),
						TEXT("uevolve-docs"),
						{ TEXT("uevolve"), TEXT("docs") },
						MaxChunkChars,
						OverlapChars,
						OutCards);
				}
			}

			TArray<FString> DocFiles;
			IFileManager::Get().FindFilesRecursive(DocFiles, *FPaths::Combine(ProjectDir, TEXT("Docs")), TEXT("*.md"), true, false);
			for (const FString& FilePath : DocFiles)
			{
				AddMarkdownFileCards(
					FilePath,
					SanitizeKnowledgeId(MakeProjectRelativePath(FilePath)),
					TEXT("uevolve-docs"),
					{ TEXT("uevolve"), TEXT("docs") },
					MaxChunkChars,
					OverlapChars,
					OutCards);
			}
		}

		void AddToolRegistryCards(int32 MaxChunkChars, int32 OverlapChars, TArray<FKnowledgeCard>& OutCards)
		{
			for (const FToolRegistryEntry& Entry : GetToolRegistryEntries())
			{
				if (Entry.Exposure == EToolExposure::LegacyHidden)
				{
					continue;
				}

				const FRegisteredUnrealMcpToolDescriptor* Descriptor = FindRegisteredMcpToolDescriptor(Entry.Name);
				FString Description = Descriptor ? Descriptor->Descriptor.Description : Entry.Policy.Reason;
				FString Text = FString::Printf(
					TEXT("Tool: %s\nCategory: %s\nRisk: %s\nRequires write: %s\nRequires build: %s\nRequires external process: %s\nDry-run support: %s\nPreflight support: %s\nPostcheck support: %s\nDescription: %s\nReason: %s\nNotes: %s"),
					*Entry.Name,
					*Entry.Category,
					*LexToString(Entry.Policy.RiskLevel),
					Entry.Policy.bRequiresWrite ? TEXT("true") : TEXT("false"),
					Entry.Policy.bRequiresBuild ? TEXT("true") : TEXT("false"),
					Entry.Policy.bRequiresExternalProcess ? TEXT("true") : TEXT("false"),
					Entry.Policy.bDryRunSupport ? TEXT("true") : TEXT("false"),
					Entry.Policy.bPreflightSupport ? TEXT("true") : TEXT("false"),
					Entry.Policy.bPostcheckSupport ? TEXT("true") : TEXT("false"),
					*Description,
					*Entry.Policy.Reason,
					*Entry.Notes);

				AddCardsFromText(
					Entry.Name,
					Descriptor ? Descriptor->Descriptor.Title : Entry.Name,
					TEXT("mcp-tools"),
					{ Entry.Category, LexToString(Entry.Policy.RiskLevel) },
					TEXT("tool-registry"),
					TEXT("Tools/UnrealMcpToolRegistry/tools.json"),
					FString(),
					Text,
					MaxChunkChars,
					OverlapChars,
					OutCards);
			}
		}

		bool WriteKnowledgeCardsJsonl(const FString& Path, const TArray<FKnowledgeCard>& Cards, FString& OutFailureReason)
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
			FString Output;
			for (const FKnowledgeCard& Card : Cards)
			{
				Output += JsonObjectToCondensedString(CardToJsonObject(Card));
				Output += LINE_TERMINATOR;
			}

			if (!FFileHelper::SaveStringToFile(Output, *Path))
			{
				OutFailureReason = FString::Printf(TEXT("Failed to write knowledge cards '%s'."), *Path);
				return false;
			}
			return true;
		}

		bool LoadKnowledgeCards(const FString& IndexDir, TArray<FKnowledgeCard>& OutCards, FString& OutFailureReason)
		{
			const FString CardsPath = FPaths::Combine(IndexDir, TEXT("cards.jsonl"));
			TArray<FString> Lines;
			if (!FFileHelper::LoadFileToStringArray(Lines, *CardsPath))
			{
				OutFailureReason = FString::Printf(TEXT("Knowledge index not found at '%s'. Run unreal.knowledge_index_refresh first."), *CardsPath);
				return false;
			}

			for (const FString& Line : Lines)
			{
				if (Line.TrimStartAndEnd().IsEmpty())
				{
					continue;
				}
				TSharedPtr<FJsonObject> Object;
				if (!LoadJsonObjectFromString(Line, Object))
				{
					continue;
				}
				FKnowledgeCard Card;
				if (JsonObjectToCard(Object, Card))
				{
					OutCards.Add(MoveTemp(Card));
				}
			}

			if (OutCards.IsEmpty())
			{
				OutFailureReason = FString::Printf(TEXT("Knowledge index '%s' contains no cards."), *CardsPath);
				return false;
			}
			return true;
		}

		double ScoreKnowledgeCard(const FKnowledgeCard& Card, const FString& Query, const TArray<FString>& QueryTokens)
		{
			const FString QueryLower = Query.ToLower().TrimStartAndEnd();
			const FString TitleLower = Card.Title.ToLower();
			const FString CategoryLower = Card.Category.ToLower();
			const FString TextLower = Card.Text.ToLower();
			double Score = 0.0;

			if (!QueryLower.IsEmpty())
			{
				if (TitleLower.Contains(QueryLower))
				{
					Score += 40.0;
				}
				if (CategoryLower.Contains(QueryLower))
				{
					Score += 16.0;
				}
				if (TextLower.Contains(QueryLower))
				{
					Score += 20.0;
				}
			}

			for (const FString& Token : QueryTokens)
			{
				if (TitleLower.Contains(Token))
				{
					Score += 12.0;
				}
				if (CategoryLower.Contains(Token))
				{
					Score += 8.0;
				}
				for (const FString& Tag : Card.Tags)
				{
					if (Tag.ToLower().Contains(Token))
					{
						Score += 8.0;
					}
				}
				if (TextLower.Contains(Token))
				{
					Score += 2.0;
				}
			}
			return Score;
		}

		FString MakeExcerpt(const FString& Text, const FString& Query, const TArray<FString>& QueryTokens, int32 MaxChars)
		{
			const int32 SafeMaxChars = FMath::Clamp(MaxChars, 80, 2400);
			if (Text.Len() <= SafeMaxChars)
			{
				return Text;
			}

			const FString LowerText = Text.ToLower();
			int32 HitIndex = INDEX_NONE;
			if (!Query.TrimStartAndEnd().IsEmpty())
			{
				HitIndex = LowerText.Find(Query.ToLower().TrimStartAndEnd());
			}
			if (HitIndex == INDEX_NONE)
			{
				for (const FString& Token : QueryTokens)
				{
					HitIndex = LowerText.Find(Token);
					if (HitIndex != INDEX_NONE)
					{
						break;
					}
				}
			}

			const int32 Start = HitIndex == INDEX_NONE ? 0 : FMath::Max(0, HitIndex - SafeMaxChars / 3);
			return Text.Mid(Start, SafeMaxChars).TrimStartAndEnd();
		}

		bool CategoryAllowed(const FString& Category, const TArray<FString>& Filters)
		{
			if (Filters.IsEmpty())
			{
				return true;
			}
			for (const FString& Filter : Filters)
			{
				if (Category.Equals(Filter, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
			return false;
		}

		int32 RiskRank(EToolRiskLevel Risk)
		{
			switch (Risk)
			{
			case EToolRiskLevel::ReadOnly:
				return 0;
			case EToolRiskLevel::Low:
				return 1;
			case EToolRiskLevel::Medium:
				return 2;
			case EToolRiskLevel::High:
				return 3;
			case EToolRiskLevel::Critical:
				return 4;
			default:
				return 2;
			}
		}

		int32 RiskRankFromString(const FString& RiskMax)
		{
			if (RiskMax.Equals(TEXT("read_only"), ESearchCase::IgnoreCase) || RiskMax.Equals(TEXT("readonly"), ESearchCase::IgnoreCase))
			{
				return 0;
			}
			if (RiskMax.Equals(TEXT("low"), ESearchCase::IgnoreCase))
			{
				return 1;
			}
			if (RiskMax.Equals(TEXT("medium"), ESearchCase::IgnoreCase))
			{
				return 2;
			}
			if (RiskMax.Equals(TEXT("high"), ESearchCase::IgnoreCase))
			{
				return 3;
			}
			if (RiskMax.Equals(TEXT("critical"), ESearchCase::IgnoreCase))
			{
				return 4;
			}
			return 4;
		}

		double ScoreToolForTask(const FToolRegistryEntry& Entry, const FString& Task, const TArray<FString>& Tokens)
		{
			const FRegisteredUnrealMcpToolDescriptor* Descriptor = FindRegisteredMcpToolDescriptor(Entry.Name);
			FString Haystack = Entry.Name + TEXT(" ") + Entry.Category + TEXT(" ") + Entry.Policy.Reason + TEXT(" ") + Entry.Notes;
			if (Descriptor)
			{
				Haystack += TEXT(" ") + Descriptor->Descriptor.Title + TEXT(" ") + Descriptor->Descriptor.Description;
			}
			Haystack = Haystack.ToLower();

			const FString TaskLower = Task.ToLower();
			double Score = 0.0;
			if (!TaskLower.IsEmpty() && Haystack.Contains(TaskLower))
			{
				Score += 40.0;
			}

			for (const FString& Token : Tokens)
			{
				if (Entry.Name.ToLower().Contains(Token))
				{
					Score += 16.0;
				}
				if (Entry.Category.ToLower().Contains(Token))
				{
					Score += 8.0;
				}
				if (Haystack.Contains(Token))
				{
					Score += 4.0;
				}
			}

			if (TaskLower.Contains(TEXT("blueprint")) || TaskLower.Contains(TEXT("蓝图")))
			{
				Score += Entry.Category == TEXT("blueprint") ? 24.0 : 0.0;
			}
			if (TaskLower.Contains(TEXT("widget")) || TaskLower.Contains(TEXT("umg")) || TaskLower.Contains(TEXT("hud")) || TaskLower.Contains(TEXT("ui")))
			{
				Score += Entry.Category == TEXT("widget") ? 24.0 : 0.0;
			}
			if (TaskLower.Contains(TEXT("actor")) || TaskLower.Contains(TEXT("spawn")) || TaskLower.Contains(TEXT("level")) || TaskLower.Contains(TEXT("场景")))
			{
				Score += Entry.Category == TEXT("actors") || Entry.Category == TEXT("editor") ? 14.0 : 0.0;
			}
			if (TaskLower.Contains(TEXT("mcp")) || TaskLower.Contains(TEXT("tool")) || TaskLower.Contains(TEXT("工具")) || TaskLower.Contains(TEXT("自拓展")))
			{
				Score += Entry.Category == TEXT("self-extension") || Entry.Category == TEXT("scaffold") ? 18.0 : 0.0;
			}
			if (TaskLower.Contains(TEXT("rag")) || TaskLower.Contains(TEXT("knowledge")) || TaskLower.Contains(TEXT("知识库")))
			{
				Score += Entry.Name.Contains(TEXT("knowledge")) || Entry.Name == TEXT("unreal.tool_recommend") ? 30.0 : 0.0;
			}

			if (Entry.Policy.bPostcheckSupport)
			{
				Score += 2.0;
			}
			if (Entry.Policy.bDryRunSupport)
			{
				Score += 2.0;
			}
			return Score;
		}

		TSharedPtr<FJsonObject> MakeToolRecommendationObject(const FToolRegistryEntry& Entry, double Score)
		{
			const FRegisteredUnrealMcpToolDescriptor* Descriptor = FindRegisteredMcpToolDescriptor(Entry.Name);
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("toolName"), Entry.Name);
			Object->SetStringField(TEXT("handlerName"), Entry.HandlerName.IsEmpty() ? Entry.Name : Entry.HandlerName);
			Object->SetStringField(TEXT("category"), Entry.Category);
			Object->SetStringField(TEXT("title"), Descriptor ? Descriptor->Descriptor.Title : Entry.Name);
			Object->SetStringField(TEXT("description"), Descriptor ? Descriptor->Descriptor.Description : Entry.Policy.Reason);
			Object->SetStringField(TEXT("riskLevel"), LexToString(Entry.Policy.RiskLevel));
			Object->SetBoolField(TEXT("requiresWrite"), Entry.Policy.bRequiresWrite);
			Object->SetBoolField(TEXT("dryRunSupport"), Entry.Policy.bDryRunSupport);
			Object->SetBoolField(TEXT("preflightSupport"), Entry.Policy.bPreflightSupport);
			Object->SetBoolField(TEXT("postcheckSupport"), Entry.Policy.bPostcheckSupport);
			Object->SetNumberField(TEXT("score"), Score);
			return Object;
		}
	}

	FUnrealMcpExecutionResult KnowledgeIndexRefresh(const FJsonObject& Arguments)
	{
		FString SourceRoot = GetKnowledgeSourceRoot();
		FString IndexRoot = GetKnowledgeIndexRoot();
		Arguments.TryGetStringField(TEXT("sourceRoot"), SourceRoot);
		Arguments.TryGetStringField(TEXT("indexRoot"), IndexRoot);
		SourceRoot = NormalizePathForJson(SourceRoot);
		IndexRoot = NormalizePathForJson(IndexRoot);

		bool bIncludeOfficialDocs = true;
		bool bIncludeVersionedDocs = true;
		bool bIncludeToolRegistry = true;
		bool bSkipLowContent = true;
		bool bDryRun = false;
		Arguments.TryGetBoolField(TEXT("includeOfficialDocs"), bIncludeOfficialDocs);
		Arguments.TryGetBoolField(TEXT("includeVersionedDocs"), bIncludeVersionedDocs);
		Arguments.TryGetBoolField(TEXT("includeToolRegistry"), bIncludeToolRegistry);
		Arguments.TryGetBoolField(TEXT("skipLowContent"), bSkipLowContent);
		Arguments.TryGetBoolField(TEXT("dryRun"), bDryRun);

		const int32 MaxCards = FMath::Clamp(GetPositiveIntArgument(Arguments, TEXT("maxCards"), DefaultKnowledgeMaxCards), 1, 20000);
		const int32 MaxChunkChars = FMath::Clamp(GetPositiveIntArgument(Arguments, TEXT("maxChunkChars"), DefaultKnowledgeChunkChars), 400, 12000);
		const int32 OverlapChars = FMath::Clamp(GetPositiveIntArgument(Arguments, TEXT("chunkOverlapChars"), DefaultKnowledgeOverlapChars), 0, MaxChunkChars / 2);

		TArray<FKnowledgeCard> Cards;
		int32 SkippedRows = 0;
		TArray<FString> SourceFiles;

		if (bIncludeOfficialDocs && FPaths::DirectoryExists(SourceRoot))
		{
			IFileManager::Get().FindFilesRecursive(SourceFiles, *SourceRoot, TEXT("documents.jsonl"), true, false);
			for (const FString& DocumentsJsonlPath : SourceFiles)
			{
				AddDocumentationJsonlCards(DocumentsJsonlPath, MaxChunkChars, OverlapChars, bSkipLowContent, Cards, SkippedRows);
				if (Cards.Num() >= MaxCards)
				{
					break;
				}
			}
		}

		if (bIncludeVersionedDocs && Cards.Num() < MaxCards)
		{
			AddVersionedDocumentationCards(MaxChunkChars, OverlapChars, Cards);
		}

		if (bIncludeToolRegistry && Cards.Num() < MaxCards)
		{
			AddToolRegistryCards(MaxChunkChars, OverlapChars, Cards);
		}

		if (Cards.Num() > MaxCards)
		{
			Cards.SetNum(MaxCards);
		}

		TArray<TSharedPtr<FJsonValue>> SourceFileValues;
		for (const FString& SourceFile : SourceFiles)
		{
			SourceFileValues.Add(MakeShared<FJsonValueString>(MakeProjectRelativePath(SourceFile)));
		}

		TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
		StructuredContent->SetStringField(TEXT("action"), TEXT("knowledge_index_refresh"));
		StructuredContent->SetStringField(TEXT("indexRoot"), MakeProjectRelativePath(IndexRoot));
		StructuredContent->SetStringField(TEXT("sourceRoot"), MakeProjectRelativePath(SourceRoot));
		StructuredContent->SetBoolField(TEXT("dryRun"), bDryRun);
		StructuredContent->SetNumberField(TEXT("cardCount"), Cards.Num());
		StructuredContent->SetNumberField(TEXT("sourceDocumentsJsonlCount"), SourceFiles.Num());
		StructuredContent->SetNumberField(TEXT("skippedRows"), SkippedRows);
		StructuredContent->SetArrayField(TEXT("sourceDocumentsJsonl"), SourceFileValues);

		if (bDryRun)
		{
			return MakeExecutionResult(
				FString::Printf(TEXT("Knowledge index dry run: would write %d KnowledgeCards from %d source documents.jsonl files."), Cards.Num(), SourceFiles.Num()),
				StructuredContent,
				false);
		}

		const FString CardsPath = FPaths::Combine(IndexRoot, TEXT("cards.jsonl"));
		const FString ManifestPath = FPaths::Combine(IndexRoot, TEXT("index.json"));
		FString FailureReason;
		if (!WriteKnowledgeCardsJsonl(CardsPath, Cards, FailureReason))
		{
			return MakeExecutionResult(FailureReason, StructuredContent, true);
		}

		TSharedPtr<FJsonObject> Manifest = MakeShared<FJsonObject>();
		Manifest->SetStringField(TEXT("schema"), TEXT("UEvolve.KnowledgeIndex.v1"));
		Manifest->SetStringField(TEXT("indexRoot"), MakeProjectRelativePath(IndexRoot));
		Manifest->SetStringField(TEXT("sourceRoot"), MakeProjectRelativePath(SourceRoot));
		Manifest->SetStringField(TEXT("cardsPath"), MakeProjectRelativePath(CardsPath));
		Manifest->SetNumberField(TEXT("cardCount"), Cards.Num());
		Manifest->SetNumberField(TEXT("sourceDocumentsJsonlCount"), SourceFiles.Num());
		Manifest->SetNumberField(TEXT("skippedRows"), SkippedRows);
		Manifest->SetBoolField(TEXT("includeOfficialDocs"), bIncludeOfficialDocs);
		Manifest->SetBoolField(TEXT("includeVersionedDocs"), bIncludeVersionedDocs);
		Manifest->SetBoolField(TEXT("includeToolRegistry"), bIncludeToolRegistry);
		Manifest->SetArrayField(TEXT("sourceDocumentsJsonl"), SourceFileValues);
		if (!WriteJsonObjectToFile(Manifest, ManifestPath, FailureReason))
		{
			return MakeExecutionResult(FailureReason, StructuredContent, true);
		}

		StructuredContent->SetStringField(TEXT("cardsPath"), MakeProjectRelativePath(CardsPath));
		StructuredContent->SetStringField(TEXT("manifestPath"), MakeProjectRelativePath(ManifestPath));
		return MakeExecutionResult(
			FString::Printf(TEXT("Knowledge index refreshed: %d KnowledgeCards written to %s."), Cards.Num(), *MakeProjectRelativePath(CardsPath)),
			StructuredContent,
			false);
	}

	FUnrealMcpExecutionResult KnowledgeSearch(const FJsonObject& Arguments)
	{
		FString Query;
		if (!Arguments.TryGetStringField(TEXT("query"), Query) || Query.TrimStartAndEnd().IsEmpty())
		{
			return MakeExecutionResult(TEXT("Missing required field 'query'."), nullptr, true);
		}

		FString IndexRoot = GetKnowledgeIndexRoot();
		Arguments.TryGetStringField(TEXT("indexRoot"), IndexRoot);
		IndexRoot = NormalizePathForJson(IndexRoot);

		TArray<FString> Categories;
		TryGetStringArrayField(Arguments, TEXT("categories"), Categories);
		bool bIncludeText = false;
		Arguments.TryGetBoolField(TEXT("includeText"), bIncludeText);
		const int32 Limit = FMath::Clamp(GetPositiveIntArgument(Arguments, TEXT("limit"), DefaultKnowledgeSearchLimit), 1, 50);
		const int32 MaxExcerptChars = FMath::Clamp(GetPositiveIntArgument(Arguments, TEXT("maxExcerptChars"), DefaultKnowledgeExcerptChars), 80, 2400);

		TArray<FKnowledgeCard> Cards;
		FString FailureReason;
		if (!LoadKnowledgeCards(IndexRoot, Cards, FailureReason))
		{
			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), TEXT("knowledge_search"));
			StructuredContent->SetStringField(TEXT("indexRoot"), MakeProjectRelativePath(IndexRoot));
			StructuredContent->SetStringField(TEXT("recommendedNextTool"), TEXT("unreal.knowledge_index_refresh"));
			return MakeExecutionResult(FailureReason, StructuredContent, true);
		}

		const TArray<FString> QueryTokens = ExtractSearchTokens(Query);
		struct FScoredCard
		{
			FKnowledgeCard Card;
			double Score = 0.0;
		};
		TArray<FScoredCard> ScoredCards;
		for (const FKnowledgeCard& Card : Cards)
		{
			if (!CategoryAllowed(Card.Category, Categories))
			{
				continue;
			}
			const double Score = ScoreKnowledgeCard(Card, Query, QueryTokens);
			if (Score > 0.0)
			{
				FScoredCard Scored;
				Scored.Card = Card;
				Scored.Score = Score;
				ScoredCards.Add(MoveTemp(Scored));
			}
		}

		ScoredCards.Sort([](const FScoredCard& Left, const FScoredCard& Right)
		{
			if (!FMath::IsNearlyEqual(Left.Score, Right.Score))
			{
				return Left.Score > Right.Score;
			}
			return Left.Card.Title < Right.Card.Title;
		});

		TArray<TSharedPtr<FJsonValue>> ResultValues;
		for (int32 Index = 0; Index < FMath::Min(Limit, ScoredCards.Num()); ++Index)
		{
			const FScoredCard& Scored = ScoredCards[Index];
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("cardId"), Scored.Card.CardId);
			Result->SetStringField(TEXT("title"), Scored.Card.Title);
			Result->SetStringField(TEXT("category"), Scored.Card.Category);
			Result->SetStringField(TEXT("sourceKind"), Scored.Card.SourceKind);
			Result->SetStringField(TEXT("sourcePath"), Scored.Card.SourcePath);
			Result->SetStringField(TEXT("url"), Scored.Card.Url);
			Result->SetArrayField(TEXT("tags"), StringsToJsonArray(Scored.Card.Tags));
			Result->SetNumberField(TEXT("score"), Scored.Score);
			Result->SetStringField(TEXT("excerpt"), MakeExcerpt(Scored.Card.Text, Query, QueryTokens, MaxExcerptChars));
			if (bIncludeText)
			{
				Result->SetStringField(TEXT("text"), Scored.Card.Text);
			}
			ResultValues.Add(MakeShared<FJsonValueObject>(Result));
		}

		TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
		StructuredContent->SetStringField(TEXT("action"), TEXT("knowledge_search"));
		StructuredContent->SetStringField(TEXT("query"), Query);
		StructuredContent->SetStringField(TEXT("indexRoot"), MakeProjectRelativePath(IndexRoot));
		StructuredContent->SetNumberField(TEXT("cardCount"), Cards.Num());
		StructuredContent->SetNumberField(TEXT("matchCount"), ScoredCards.Num());
		StructuredContent->SetArrayField(TEXT("results"), ResultValues);
		return MakeExecutionResult(
			FString::Printf(TEXT("Knowledge search returned %d result(s) for '%s'."), ResultValues.Num(), *Query),
			StructuredContent,
			false);
	}

	FUnrealMcpExecutionResult ToolRecommend(const FJsonObject& Arguments, const TArray<TSharedPtr<FJsonValue>>& ToolsArray)
	{
		FString Task;
		if (!Arguments.TryGetStringField(TEXT("task"), Task) || Task.TrimStartAndEnd().IsEmpty())
		{
			return MakeExecutionResult(TEXT("Missing required field 'task'."), nullptr, true);
		}

		FString RiskMax = TEXT("critical");
		Arguments.TryGetStringField(TEXT("riskMax"), RiskMax);
		const int32 MaxRiskRank = RiskRankFromString(RiskMax);
		const int32 Limit = FMath::Clamp(GetPositiveIntArgument(Arguments, TEXT("limit"), 8), 1, 30);
		bool bIncludeKnowledge = true;
		bool bIncludeWorkflowDraft = true;
		Arguments.TryGetBoolField(TEXT("includeKnowledge"), bIncludeKnowledge);
		Arguments.TryGetBoolField(TEXT("includeWorkflowDraft"), bIncludeWorkflowDraft);

		const TArray<FString> Tokens = ExtractSearchTokens(Task);
		struct FScoredTool
		{
			const FToolRegistryEntry* Entry = nullptr;
			double Score = 0.0;
		};
		TArray<FScoredTool> ScoredTools;
		for (const FToolRegistryEntry& Entry : GetToolRegistryEntries())
		{
			if (Entry.Exposure == EToolExposure::LegacyHidden)
			{
				continue;
			}
			if (RiskRank(Entry.Policy.RiskLevel) > MaxRiskRank)
			{
				continue;
			}

			const double Score = ScoreToolForTask(Entry, Task, Tokens);
			if (Score > 0.0)
			{
				FScoredTool Scored;
				Scored.Entry = &Entry;
				Scored.Score = Score;
				ScoredTools.Add(Scored);
			}
		}

		ScoredTools.Sort([](const FScoredTool& Left, const FScoredTool& Right)
		{
			if (!FMath::IsNearlyEqual(Left.Score, Right.Score))
			{
				return Left.Score > Right.Score;
			}
			return Left.Entry && Right.Entry ? Left.Entry->Name < Right.Entry->Name : false;
		});

		TArray<TSharedPtr<FJsonValue>> RecommendationValues;
		for (int32 Index = 0; Index < FMath::Min(Limit, ScoredTools.Num()); ++Index)
		{
			if (ScoredTools[Index].Entry)
			{
				RecommendationValues.Add(MakeShared<FJsonValueObject>(MakeToolRecommendationObject(*ScoredTools[Index].Entry, ScoredTools[Index].Score)));
			}
		}

		TArray<TSharedPtr<FJsonValue>> KnowledgeValues;
		FString KnowledgeNote;
		if (bIncludeKnowledge)
		{
			TArray<FKnowledgeCard> Cards;
			FString FailureReason;
			if (LoadKnowledgeCards(GetKnowledgeIndexRoot(), Cards, FailureReason))
			{
				TArray<FKnowledgeCard> Matches;
				const TArray<FString> QueryTokens = ExtractSearchTokens(Task);
				struct FScoredCard
				{
					FKnowledgeCard Card;
					double Score = 0.0;
				};
				TArray<FScoredCard> ScoredCards;
				for (const FKnowledgeCard& Card : Cards)
				{
					const double Score = ScoreKnowledgeCard(Card, Task, QueryTokens);
					if (Score > 0.0)
					{
						FScoredCard Scored;
						Scored.Card = Card;
						Scored.Score = Score;
						ScoredCards.Add(MoveTemp(Scored));
					}
				}
				ScoredCards.Sort([](const FScoredCard& Left, const FScoredCard& Right)
				{
					return Left.Score > Right.Score;
				});
				for (int32 Index = 0; Index < FMath::Min(3, ScoredCards.Num()); ++Index)
				{
					TSharedPtr<FJsonObject> CardObject = MakeShared<FJsonObject>();
					CardObject->SetStringField(TEXT("cardId"), ScoredCards[Index].Card.CardId);
					CardObject->SetStringField(TEXT("title"), ScoredCards[Index].Card.Title);
					CardObject->SetStringField(TEXT("category"), ScoredCards[Index].Card.Category);
					CardObject->SetStringField(TEXT("sourcePath"), ScoredCards[Index].Card.SourcePath);
					CardObject->SetStringField(TEXT("excerpt"), MakeExcerpt(ScoredCards[Index].Card.Text, Task, QueryTokens, 320));
					CardObject->SetNumberField(TEXT("score"), ScoredCards[Index].Score);
					KnowledgeValues.Add(MakeShared<FJsonValueObject>(CardObject));
				}
			}
			else
			{
				KnowledgeNote = FailureReason;
			}
		}

		TArray<TSharedPtr<FJsonValue>> WorkflowSteps;
		if (bIncludeWorkflowDraft)
		{
			auto AddStep = [&WorkflowSteps](const FString& ToolName, const FString& Purpose)
			{
				TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
				Step->SetStringField(TEXT("tool"), ToolName);
				Step->SetStringField(TEXT("purpose"), Purpose);
				WorkflowSteps.Add(MakeShared<FJsonValueObject>(Step));
			};

			AddStep(TEXT("unreal.preview_change_plan"), TEXT("Turn the task into a bounded plan with risk and verification gates."));
			if (!KnowledgeNote.IsEmpty())
			{
				AddStep(TEXT("unreal.knowledge_index_refresh"), TEXT("Refresh the local knowledge index before searching docs and tool cards."));
			}
			AddStep(TEXT("unreal.knowledge_search"), TEXT("Retrieve relevant docs, tool cards, tests, and workflow notes."));
			if (RecommendationValues.Num() > 0)
			{
				const TSharedPtr<FJsonObject> FirstTool = RecommendationValues[0]->AsObject();
				if (FirstTool.IsValid())
				{
					AddStep(FirstTool->GetStringField(TEXT("toolName")), TEXT("Run the top recommended task-specific tool, preferably dry-run first when supported."));
				}
			}
			AddStep(TEXT("unreal.verify_task_outcome"), TEXT("Verify the task outcome with evidence rather than relying on a prose summary."));
		}

		TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
		StructuredContent->SetStringField(TEXT("action"), TEXT("tool_recommend"));
		StructuredContent->SetStringField(TEXT("task"), Task);
		StructuredContent->SetStringField(TEXT("riskMax"), RiskMax);
		StructuredContent->SetNumberField(TEXT("visibleToolDefinitionCount"), ToolsArray.Num());
		StructuredContent->SetArrayField(TEXT("recommendations"), RecommendationValues);
		StructuredContent->SetArrayField(TEXT("knowledgeCards"), KnowledgeValues);
		StructuredContent->SetArrayField(TEXT("workflowDraft"), WorkflowSteps);
		if (!KnowledgeNote.IsEmpty())
		{
			StructuredContent->SetStringField(TEXT("knowledgeNote"), KnowledgeNote);
		}
		return MakeExecutionResult(
			FString::Printf(TEXT("Recommended %d tool(s) for this task."), RecommendationValues.Num()),
			StructuredContent,
			false);
	}
}
