#include "STaskAtlasWindow.h"

#include "UnrealMcpModule.h"
#include "UnrealMcpToolRegistry.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "TaskAtlasWindow"

namespace TaskAtlasWindow
{
	FString JsonObjectToPrettyString(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return TEXT("{}");
		}

		FString Output;
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Output;
	}

	FString GetStringField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, const FString& Fallback = FString())
	{
		FString Value;
		if (Object.IsValid() && Object->TryGetStringField(FieldName, Value))
		{
			return Value;
		}
		return Fallback;
	}

	bool GetBoolField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, bool bFallback = false)
	{
		bool bValue = bFallback;
		if (Object.IsValid())
		{
			Object->TryGetBoolField(FieldName, bValue);
		}
		return bValue;
	}

	TArray<FString> GetStringArrayField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName)
	{
		TArray<FString> Values;
		const TArray<TSharedPtr<FJsonValue>>* ArrayValues = nullptr;
		if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, ArrayValues) || !ArrayValues)
		{
			return Values;
		}
		for (const TSharedPtr<FJsonValue>& Value : *ArrayValues)
		{
			if (Value.IsValid() && Value->Type == EJson::String)
			{
				Values.Add(Value->AsString());
			}
		}
		return Values;
	}

	int32 WorkflowSortRank(const STaskAtlasWindow::FWorkflowRow& Row)
	{
		if (Row.bPinned)
		{
			return 0;
		}
		if (Row.Rating == TEXT("success"))
		{
			return 1;
		}
		return 2;
	}

	FString JoinCriticalPath(const TArray<FString>& CriticalPath)
	{
		TArray<FString> DisplayTools;
		for (int32 Index = 0; Index < CriticalPath.Num() && Index < 10; ++Index)
		{
			DisplayTools.Add(CriticalPath[Index]);
		}
		FString Text = FString::Join(DisplayTools, TEXT(" -> "));
		if (CriticalPath.Num() > 10)
		{
			Text += TEXT(" -> ...");
		}
		return Text;
	}
}

void STaskAtlasWindow::Construct(const FArguments& InArgs, FUnrealMcpModule* InOwnerModule)
{
	OwnerModule = InOwnerModule;

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(12.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "Task Atlas"))
					.Font(FAppStyle::GetFontStyle("HeadingMedium"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Refresh", "Refresh"))
					.OnClicked(this, &STaskAtlasWindow::HandleRefreshClicked)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 8.0f)
			[
				SAssignNew(StatusText, STextBlock)
				.AutoWrapText(true)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("StatusInitial", "Loading Task Atlas."))
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SSplitter)
				+ SSplitter::Slot()
				.Value(0.64f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.FillHeight(0.58f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(LOCTEXT("Workflows", "Workflows"))
								.Font(FAppStyle::GetFontStyle("NormalFontBold"))
							]
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							.Padding(0.0f, 6.0f, 0.0f, 0.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(WorkflowListBox, SVerticalBox)
								]
							]
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(0.42f)
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(8.0f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(STextBlock)
								.Text(LOCTEXT("UnusedTools", "Unused Tools"))
								.Font(FAppStyle::GetFontStyle("NormalFontBold"))
							]
							+ SVerticalBox::Slot()
							.FillHeight(1.0f)
							.Padding(0.0f, 6.0f, 0.0f, 0.0f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()
								[
									SAssignNew(UnusedToolListBox, SVerticalBox)
								]
							]
						]
					]
				]
				+ SSplitter::Slot()
				.Value(0.36f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(10.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SAssignNew(ToolDetailTitle, STextBlock)
							.Text(LOCTEXT("ToolDetailTitle", "Tool Details"))
							.Font(FAppStyle::GetFontStyle("NormalFontBold"))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 6.0f, 0.0f, 0.0f)
						[
							SAssignNew(ToolDetailMeta, STextBlock)
							.AutoWrapText(true)
							.ColorAndOpacity(FSlateColor::UseSubduedForeground())
							.Text(LOCTEXT("ToolDetailMeta", "Select a tool name to inspect registry metadata."))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 10.0f, 0.0f, 0.0f)
						[
							SAssignNew(ToolDetailDescription, STextBlock)
							.AutoWrapText(true)
							.Text(FText::GetEmpty())
						]
						+ SVerticalBox::Slot()
						.FillHeight(1.0f)
						.Padding(0.0f, 10.0f, 0.0f, 0.0f)
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SAssignNew(ToolDetailSchema, STextBlock)
								.AutoWrapText(true)
								.Font(FAppStyle::GetFontStyle("SmallFont"))
								.Text(FText::GetEmpty())
							]
						]
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(SEditableTextBox)
				.HintText(LOCTEXT("SearchHint", "Search workflows and tools"))
				.OnTextChanged(this, &STaskAtlasWindow::HandleSearchChanged)
			]
		]
	];

	RefreshData();
}

FReply STaskAtlasWindow::HandleRefreshClicked()
{
	RefreshData();
	return FReply::Handled();
}

void STaskAtlasWindow::HandleSearchChanged(const FText& NewText)
{
	SearchText = NewText.ToString().TrimStartAndEnd().ToLower();
	RebuildLists();
}

FReply STaskAtlasWindow::HandlePinClicked(FString TaskId, bool bNewPinned)
{
	if (!OwnerModule)
	{
		SetStatus(TEXT("Task Atlas is not connected to the module."));
		return FReply::Handled();
	}

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("taskId"), TaskId);
	Arguments->SetBoolField(TEXT("pinned"), bNewPinned);
	const FUnrealMcpExecutionResult Result = OwnerModule->ExecuteToolFromEditorUI(TEXT("unreal.task_pin"), *Arguments);
	SetStatus(Result.Text);
	if (!Result.bIsError)
	{
		RefreshData();
	}
	return FReply::Handled();
}

FReply STaskAtlasWindow::HandleFutureActionClicked(FString Label)
{
	SetStatus(FString::Printf(TEXT("%s: Coming in %s."), *Label, Label == TEXT("Make Tool") ? TEXT("v0.19") : TEXT("v0.18")));
	return FReply::Handled();
}

FReply STaskAtlasWindow::HandleToolClicked(FString ToolName)
{
	ShowToolDetails(ToolName);
	return FReply::Handled();
}

void STaskAtlasWindow::RefreshData()
{
	Workflows.Reset();
	Tools.Reset();
	ToolsByName.Reset();

	for (const UnrealMcp::FToolRegistryEntry& Entry : UnrealMcp::GetToolRegistryEntries())
	{
		if (Entry.Exposure != UnrealMcp::EToolExposure::Visible)
		{
			continue;
		}

		FToolRow Tool;
		Tool.Name = Entry.Name;
		Tool.Category = Entry.Category;
		Tool.RiskLevel = UnrealMcp::LexToString(Entry.Policy.RiskLevel);
		Tool.Description = Entry.Description;
		Tool.Owner = Entry.Policy.Owner;
		Tool.DocsPath = Entry.Policy.DocsPath;
		Tool.InputSchemaText = TaskAtlasWindow::JsonObjectToPrettyString(Entry.InputSchema);
		Tools.Add(Tool);
		ToolsByName.Add(Tool.Name, Tool);
	}
	Tools.Sort([](const FToolRow& Left, const FToolRow& Right)
	{
		return Left.Name < Right.Name;
	});

	if (!OwnerModule)
	{
		SetStatus(TEXT("Task Atlas is not connected to the module."));
		RebuildLists();
		return;
	}

	TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("filter"), TEXT("all"));
	Arguments->SetNumberField(TEXT("limit"), 200.0);
	const FUnrealMcpExecutionResult Result = OwnerModule->ExecuteToolFromEditorUI(TEXT("unreal.task_list"), *Arguments);
	if (Result.bIsError || !Result.StructuredContent.IsValid())
	{
		SetStatus(Result.Text.IsEmpty() ? TEXT("Task list failed.") : Result.Text);
		RebuildLists();
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* TaskValues = nullptr;
	if (Result.StructuredContent->TryGetArrayField(TEXT("tasks"), TaskValues) && TaskValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *TaskValues)
		{
			if (!Value.IsValid() || Value->Type != EJson::Object || !Value->AsObject().IsValid())
			{
				continue;
			}

			const TSharedPtr<FJsonObject> TaskObject = Value->AsObject();
			FWorkflowRow Row;
			Row.TaskId = TaskAtlasWindow::GetStringField(TaskObject, TEXT("taskId"));
			Row.Label = TaskAtlasWindow::GetStringField(TaskObject, TEXT("label"), Row.TaskId);
			Row.Rating = TaskAtlasWindow::GetStringField(TaskObject, TEXT("rating"), TEXT("unrated"));
			Row.TEndUtc = TaskAtlasWindow::GetStringField(TaskObject, TEXT("tEndUtc"));
			Row.bPinned = TaskAtlasWindow::GetBoolField(TaskObject, TEXT("pinned"));
			Row.CriticalPath = TaskAtlasWindow::GetStringArrayField(TaskObject, TEXT("criticalPath"));
			Row.Json = TaskObject;
			Workflows.Add(Row);
		}
	}

	Workflows.Sort([](const FWorkflowRow& Left, const FWorkflowRow& Right)
	{
		const int32 LeftRank = TaskAtlasWindow::WorkflowSortRank(Left);
		const int32 RightRank = TaskAtlasWindow::WorkflowSortRank(Right);
		if (LeftRank != RightRank)
		{
			return LeftRank < RightRank;
		}
		return Left.TEndUtc > Right.TEndUtc;
	});

	SetStatus(FString::Printf(TEXT("Loaded %d workflow(s) and %d visible tool(s)."), Workflows.Num(), Tools.Num()));
	RebuildLists();
}

void STaskAtlasWindow::RebuildLists()
{
	if (WorkflowListBox.IsValid())
	{
		WorkflowListBox->ClearChildren();
	}
	if (UnusedToolListBox.IsValid())
	{
		UnusedToolListBox->ClearChildren();
	}

	TSet<FString> UsedTools;
	int32 DisplayedWorkflowCount = 0;
	for (const FWorkflowRow& Row : Workflows)
	{
		if (Row.Rating == TEXT("failed") || !WorkflowMatchesSearch(Row))
		{
			continue;
		}

		for (const FString& ToolName : Row.CriticalPath)
		{
			UsedTools.Add(ToolName);
		}
		++DisplayedWorkflowCount;
		if (WorkflowListBox.IsValid())
		{
			WorkflowListBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				BuildWorkflowRow(Row)
			];
		}
	}

	if (DisplayedWorkflowCount == 0 && WorkflowListBox.IsValid())
	{
		WorkflowListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("NoWorkflows", "No matching workflows."))
		];
	}

	int32 DisplayedUnusedCount = 0;
	for (const FToolRow& Tool : Tools)
	{
		if (UsedTools.Contains(Tool.Name) || !ToolMatchesSearch(Tool))
		{
			continue;
		}
		++DisplayedUnusedCount;
		if (UnusedToolListBox.IsValid())
		{
			UnusedToolListBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 5.0f)
			[
				BuildUnusedToolRow(Tool)
			];
		}
	}

	if (DisplayedUnusedCount == 0 && UnusedToolListBox.IsValid())
	{
		UnusedToolListBox->AddSlot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("NoUnusedTools", "No matching unused tools."))
		];
	}
}

TSharedRef<SWidget> STaskAtlasWindow::BuildWorkflowRow(const FWorkflowRow& Row)
{
	const FString PinPrefix = Row.bPinned ? TEXT("[PINNED] ") : FString();
	const FString CriticalPathText = TaskAtlasWindow::JoinCriticalPath(Row.CriticalPath);
	TSharedRef<SWrapBox> CriticalPathWrap = SNew(SWrapBox)
		.UseAllottedSize(true)
		.InnerSlotPadding(FVector2D(4.0f, 4.0f));

	if (Row.CriticalPath.Num() == 0)
	{
		CriticalPathWrap->AddSlot()
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(LOCTEXT("NoCriticalPath", "No tools recorded"))
		];
	}
	else
	{
		const int32 DisplayCount = FMath::Min(Row.CriticalPath.Num(), 10);
		for (int32 Index = 0; Index < DisplayCount; ++Index)
		{
			CriticalPathWrap->AddSlot()
			[
				BuildToolNameButton(Row.CriticalPath[Index])
			];
		}
		if (Row.CriticalPath.Num() > 10)
		{
			CriticalPathWrap->AddSlot()
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(LOCTEXT("MoreTools", "..."))
			];
		}
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Font(FAppStyle::GetFontStyle("NormalFontBold"))
					.Text(FText::FromString(PinPrefix + Row.Label))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(FText::FromString(FString::Printf(TEXT("%s | %s"), *Row.Rating, *CriticalPathText)))
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 5.0f, 0.0f, 0.0f)
				[
					CriticalPathWrap
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SWrapBox)
				.InnerSlotPadding(FVector2D(4.0f, 4.0f))
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(FText::FromString(Row.bPinned ? TEXT("Unpin") : TEXT("Pin")))
					.OnClicked(this, &STaskAtlasWindow::HandlePinClicked, Row.TaskId, !Row.bPinned)
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(LOCTEXT("MakeTool", "Make Tool"))
					.OnClicked(this, &STaskAtlasWindow::HandleFutureActionClicked, FString(TEXT("Make Tool")))
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(LOCTEXT("ToSkills", "To Skills"))
					.OnClicked(this, &STaskAtlasWindow::HandleFutureActionClicked, FString(TEXT("To Skills")))
				]
				+ SWrapBox::Slot()
				[
					SNew(SButton)
					.Text(LOCTEXT("ToRag", "To RAG"))
					.OnClicked(this, &STaskAtlasWindow::HandleFutureActionClicked, FString(TEXT("To RAG")))
				]
			]
		];
}

TSharedRef<SWidget> STaskAtlasWindow::BuildUnusedToolRow(const FToolRow& Row)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				BuildToolNameButton(Row.Name)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(FText::FromString(FString::Printf(TEXT("[%s] [%s]"), *Row.Category, *Row.RiskLevel)))
			]
		];
}

TSharedRef<SWidget> STaskAtlasWindow::BuildToolNameButton(const FString& ToolName)
{
	return SNew(SButton)
		.ContentPadding(FMargin(6.0f, 2.0f))
		.Text(FText::FromString(ToolName))
		.OnClicked(this, &STaskAtlasWindow::HandleToolClicked, ToolName);
}

void STaskAtlasWindow::ShowToolDetails(const FString& ToolName)
{
	const FToolRow* Tool = ToolsByName.Find(ToolName);
	if (!Tool)
	{
		SetStatus(FString::Printf(TEXT("Tool %s was not found in the registry."), *ToolName));
		return;
	}

	if (ToolDetailTitle.IsValid())
	{
		ToolDetailTitle->SetText(FText::FromString(Tool->Name));
	}
	if (ToolDetailMeta.IsValid())
	{
		ToolDetailMeta->SetText(FText::FromString(FString::Printf(
			TEXT("category: %s | risk: %s | owner: %s | docs: %s"),
			*Tool->Category,
			*Tool->RiskLevel,
			*Tool->Owner,
			*Tool->DocsPath)));
	}
	if (ToolDetailDescription.IsValid())
	{
		ToolDetailDescription->SetText(FText::FromString(Tool->Description));
	}
	if (ToolDetailSchema.IsValid())
	{
		ToolDetailSchema->SetText(FText::FromString(Tool->InputSchemaText));
	}
}

bool STaskAtlasWindow::WorkflowMatchesSearch(const FWorkflowRow& Row) const
{
	if (SearchText.IsEmpty())
	{
		return true;
	}

	if (Row.Label.ToLower().Contains(SearchText) || Row.Rating.ToLower().Contains(SearchText))
	{
		return true;
	}
	for (const FString& ToolName : Row.CriticalPath)
	{
		if (ToolName.ToLower().Contains(SearchText))
		{
			return true;
		}
	}
	return false;
}

bool STaskAtlasWindow::ToolMatchesSearch(const FToolRow& Row) const
{
	if (SearchText.IsEmpty())
	{
		return true;
	}

	return Row.Name.ToLower().Contains(SearchText)
		|| Row.Category.ToLower().Contains(SearchText)
		|| Row.RiskLevel.ToLower().Contains(SearchText)
		|| Row.Description.ToLower().Contains(SearchText);
}

void STaskAtlasWindow::SetStatus(const FString& Status)
{
	if (StatusText.IsValid())
	{
		StatusText->SetText(FText::FromString(Status));
	}
}

#undef LOCTEXT_NAMESPACE
