#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FJsonObject;
class FUnrealMcpModule;
class SEditableTextBox;
class STextBlock;
class SVerticalBox;

class STaskAtlasWindow final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STaskAtlasWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FUnrealMcpModule* InOwnerModule);

	struct FWorkflowRow
	{
		FString TaskId;
		FString Label;
		FString Rating;
		FString TEndUtc;
		bool bPinned = false;
		TArray<FString> CriticalPath;
		TSharedPtr<FJsonObject> Json;
	};

	struct FToolRow
	{
		FString Name;
		FString Category;
		FString RiskLevel;
		FString Description;
		FString Owner;
		FString DocsPath;
		FString InputSchemaText;
	};

private:
	FReply HandleRefreshClicked();
	void HandleSearchChanged(const FText& NewText);
	FReply HandlePinClicked(FString TaskId, bool bNewPinned);
	FReply HandleMakeToolClicked(FWorkflowRow Row);
	FReply HandlePromoteToSkillsClicked(FWorkflowRow Row);
	FReply HandlePromoteToRagClicked(FWorkflowRow Row);
	FReply HandleToolClicked(FString ToolName);

	void RefreshData();
	void RebuildLists();
	TSharedRef<SWidget> BuildWorkflowRow(const FWorkflowRow& Row);
	TSharedRef<SWidget> BuildUnusedToolRow(const FToolRow& Row);
	TSharedRef<SWidget> BuildToolNameButton(const FString& ToolName);
	void ShowToolDetails(const FString& ToolName);
	bool WorkflowMatchesSearch(const FWorkflowRow& Row) const;
	bool ToolMatchesSearch(const FToolRow& Row) const;
	void SetStatus(const FString& Status);

	FUnrealMcpModule* OwnerModule = nullptr;
	FString SearchText;
	TArray<FWorkflowRow> Workflows;
	TArray<FToolRow> Tools;
	TMap<FString, FToolRow> ToolsByName;

	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<SVerticalBox> WorkflowListBox;
	TSharedPtr<SVerticalBox> UnusedToolListBox;
	TSharedPtr<STextBlock> ToolDetailTitle;
	TSharedPtr<STextBlock> ToolDetailMeta;
	TSharedPtr<STextBlock> ToolDetailDescription;
	TSharedPtr<STextBlock> ToolDetailSchema;
};
