#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FJsonObject;
class FUnrealMcpModule;
class SBorder;
class SScrollBox;
class STextBlock;

class SUnrealMcpWorkbenchPanel final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUnrealMcpWorkbenchPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FUnrealMcpModule* InOwnerModule);

private:
	FReply HandleRefreshClicked();
	FReply HandleAuditClicked();
	FReply HandleCoreTestsClicked();
	FReply HandlePipelineStatusClicked();
	FReply HandleLockStatusClicked();
	FReply HandleSkillActivityStatusClicked();
	FReply HandleSkillDistillDraftClicked();
	FReply HandleSkillPromoteDryRunClicked();
	FReply HandleKnowledgeRefreshClicked();
	FReply HandleKnowledgeSearchClicked();
	FReply HandleToolRecommendClicked();
	FReply HandleKnowledgeEvalClicked();
	FReply HandleCopyResultClicked();
	FReply OnRefreshInstallDoctor();
	FReply OnOpenInstallDoctorLatestFolder();

	void RunToolAndDisplay(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments);
	void UpdateWorkbenchSummary(const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);
	void UpdateInstallDoctorCard(const TSharedPtr<FJsonObject>& StructuredContent);
	void UpdateLastResult(const FString& ToolName, const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);

	TSharedRef<SWidget> MakeMetricRow(const FText& Label, const TSharedPtr<STextBlock>& ValueWidget) const;

	FUnrealMcpModule* OwnerModule = nullptr;
	FString LastResultText;
	FString LastSkillName;
	FString LatestInstallDoctorPath;
	FString LatestInstallDoctorValidatorVersion;

	TSharedPtr<STextBlock> HealthValueText;
	TSharedPtr<STextBlock> ToolCountValueText;
	TSharedPtr<STextBlock> TestCountValueText;
	TSharedPtr<STextBlock> SupervisorValueText;
	TSharedPtr<STextBlock> MemoryKeyValueText;
	TSharedPtr<STextBlock> NextStepValueText;
	TSharedPtr<SBorder> StatusBadgeBorder;
	TSharedPtr<STextBlock> StatusBadgeText;
	TSharedPtr<STextBlock> LastRunValueText;
	TSharedPtr<STextBlock> IssueCountSummaryText;
	TSharedPtr<SScrollBox> ChecksScrollBox;
	TSharedPtr<STextBlock> LastActionValueText;
	TSharedPtr<STextBlock> LastResultValueText;
};
