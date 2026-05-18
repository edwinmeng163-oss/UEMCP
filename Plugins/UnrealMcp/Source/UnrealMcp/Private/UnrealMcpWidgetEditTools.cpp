#include "UnrealMcpWidgetEditTools.h"

#include "UnrealMcpModule.h"

#include "Animation/WidgetAnimation.h"
#include "Animation/WidgetAnimationBinding.h"
#include "Blueprint/WidgetNavigation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "K2Node_ComponentBoundEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MovieScene.h"
#include "ScopedTransaction.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "UnrealMcpWidgetEditTools"

namespace UnrealMcp
{
	bool IsEditorPlaying();
	FUnrealMcpExecutionResult MakePieBlockedResult(const FString& ToolName);
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);
	UWidgetBlueprint* LoadWidgetBlueprintAsset(UEditorAssetSubsystem* EditorAssetSubsystem, const FString& WidgetBlueprintPath, FString& OutObjectPath, FString& OutFailureReason);
	UWidget* FindWidgetByName(UWidgetBlueprint* WidgetBlueprint, const FString& WidgetName);
	TSharedPtr<FJsonObject> DescribeWidget(UWidget* Widget);
	TSharedPtr<FJsonObject> DescribeWidgetBlueprint(UWidgetBlueprint* WidgetBlueprint, const FString& Action);
	void EnsureWidgetBlueprintGuid(UWidgetBlueprint* WidgetBlueprint, UWidget* Widget);
	void RemoveWidgetBlueprintGuid(UWidgetBlueprint* WidgetBlueprint, const FName& WidgetName);
	void RenameWidgetBlueprintGuid(UWidgetBlueprint* WidgetBlueprint, const FName& OldWidgetName, UWidget* Widget);
	void MarkWidgetBlueprintModified(UWidgetBlueprint* WidgetBlueprint, bool bStructurallyModified);

	namespace
	{
		UEditorAssetSubsystem* GetEditorAssetSubsystem()
		{
			return GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
		}

		TSharedPtr<FJsonObject> MakeWidgetEditResult(const FString& Action)
		{
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("action"), Action);
			return Result;
		}

		TArray<TSharedPtr<FJsonValue>> MakeWarningsArray(const TArray<FString>& Warnings)
		{
			TArray<TSharedPtr<FJsonValue>> WarningValues;
			for (const FString& Warning : Warnings)
			{
				WarningValues.Add(MakeShared<FJsonValueString>(Warning));
			}
			return WarningValues;
		}

		void GetWidgetSubtreeNames(UWidget* Widget, TSet<FString>& OutNames)
		{
			if (!Widget)
			{
				return;
			}

			OutNames.Add(Widget->GetName());
			TArray<UWidget*> ChildWidgets;
			UWidgetTree::GetChildWidgets(Widget, ChildWidgets);
			for (UWidget* ChildWidget : ChildWidgets)
			{
				GetWidgetSubtreeNames(ChildWidget, OutNames);
			}
		}

		void CollectExistingWidgetNames(UWidgetBlueprint* WidgetBlueprint, TSet<FString>& OutNames)
		{
			if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
			{
				return;
			}

			TArray<UWidget*> Widgets;
			WidgetBlueprint->WidgetTree->GetAllWidgets(Widgets);
			for (UWidget* Widget : Widgets)
			{
				if (Widget)
				{
					OutNames.Add(Widget->GetName());
				}
			}
		}

		int32 CountComponentEventReferences(UWidgetBlueprint* WidgetBlueprint, const TSet<FString>& WidgetNames)
		{
			if (!WidgetBlueprint)
			{
				return 0;
			}

			int32 Count = 0;
			TArray<UEdGraph*> Graphs;
			WidgetBlueprint->GetAllGraphs(Graphs);
			for (UEdGraph* Graph : Graphs)
			{
				if (!Graph)
				{
					continue;
				}

				for (UEdGraphNode* Node : Graph->Nodes)
				{
					const UK2Node_ComponentBoundEvent* EventNode = Cast<UK2Node_ComponentBoundEvent>(Node);
					if (EventNode && WidgetNames.Contains(EventNode->ComponentPropertyName.ToString()))
					{
						++Count;
					}
				}
			}
			return Count;
		}

		int32 CountAnimationReferences(UWidgetBlueprint* WidgetBlueprint, const TSet<FString>& WidgetNames)
		{
			if (!WidgetBlueprint)
			{
				return 0;
			}

			int32 Count = 0;
			for (UWidgetAnimation* Animation : WidgetBlueprint->Animations)
			{
				if (!Animation)
				{
					continue;
				}

				for (const FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
				{
					if (WidgetNames.Contains(Binding.WidgetName.ToString()) || WidgetNames.Contains(Binding.SlotWidgetName.ToString()))
					{
						++Count;
					}
				}
			}
			return Count;
		}

		int32 CountWidgetReferences(UWidgetBlueprint* WidgetBlueprint, const TSet<FString>& WidgetNames)
		{
			return CountWidgetBindingReferences(WidgetBlueprint, WidgetNames)
				+ CountComponentEventReferences(WidgetBlueprint, WidgetNames)
				+ CountAnimationReferences(WidgetBlueprint, WidgetNames);
		}

		int32 UpdateWidgetReferencesForRename(UWidgetBlueprint* WidgetBlueprint, const FName& OldName, const FName& NewName)
		{
			if (!WidgetBlueprint)
			{
				return 0;
			}

			int32 Updated = 0;
			const FString OldNameString = OldName.ToString();
			const FString NewNameString = NewName.ToString();

			Updated += RenameWidgetBindingObjectReferences(WidgetBlueprint, OldNameString, NewNameString);

			TArray<UEdGraph*> Graphs;
			WidgetBlueprint->GetAllGraphs(Graphs);
			for (UEdGraph* Graph : Graphs)
			{
				if (!Graph)
				{
					continue;
				}

				for (UEdGraphNode* Node : Graph->Nodes)
				{
					UK2Node_ComponentBoundEvent* EventNode = Cast<UK2Node_ComponentBoundEvent>(Node);
					if (EventNode && EventNode->ComponentPropertyName == OldName)
					{
						EventNode->Modify();
						EventNode->ComponentPropertyName = NewName;
						EventNode->ReconstructNode();
						++Updated;
					}
				}
			}

			for (UWidgetAnimation* Animation : WidgetBlueprint->Animations)
			{
				if (!Animation)
				{
					continue;
				}

				for (FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
				{
					if (Binding.WidgetName == OldName)
					{
						Binding.WidgetName = NewName;
						++Updated;
					}
					if (Binding.SlotWidgetName == OldName)
					{
						Binding.SlotWidgetName = NewName;
						++Updated;
					}
				}

				if (Animation->MovieScene)
				{
					for (int32 PossIdx = 0; PossIdx < Animation->MovieScene->GetPossessableCount(); ++PossIdx)
					{
						FMovieScenePossessable& Possessable = Animation->MovieScene->GetPossessable(PossIdx);
						if (Possessable.GetName() == OldNameString)
						{
							Possessable.SetName(NewNameString);
							++Updated;
							break;
						}
					}
				}
			}

			if (WidgetBlueprint->WidgetTree)
			{
				WidgetBlueprint->WidgetTree->ForEachWidget([OldName, NewName, &Updated](UWidget* Widget)
				{
					if (Widget && Widget->Navigation)
					{
						Widget->Navigation->SetFlags(RF_Transactional);
						Widget->Navigation->Modify();
						Widget->Navigation->TryToRenameBinding(OldName, NewName);
						++Updated;
					}
				});
			}

			FWidgetBlueprintEditorUtils::ReplaceDesiredFocus(WidgetBlueprint, OldName, NewName);
			FBlueprintEditorUtils::ReplaceVariableReferences(WidgetBlueprint, OldName, NewName);
			FBlueprintEditorUtils::ValidateBlueprintChildVariables(WidgetBlueprint, NewName);
			return Updated;
		}

		int32 RemoveWidgetReferencesForDelete(UWidgetBlueprint* WidgetBlueprint, const TSet<FString>& WidgetNames)
		{
			if (!WidgetBlueprint)
			{
				return 0;
			}

			int32 Removed = 0;
			Removed += WidgetBlueprint->Bindings.RemoveAll([&WidgetNames](const FDelegateEditorBinding& Binding)
			{
				return WidgetNames.Contains(Binding.ObjectName);
			});

			TArray<UEdGraph*> Graphs;
			WidgetBlueprint->GetAllGraphs(Graphs);
			for (UEdGraph* Graph : Graphs)
			{
				if (!Graph)
				{
					continue;
				}

				TArray<UK2Node_ComponentBoundEvent*> NodesToRemove;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					UK2Node_ComponentBoundEvent* EventNode = Cast<UK2Node_ComponentBoundEvent>(Node);
					if (EventNode && WidgetNames.Contains(EventNode->ComponentPropertyName.ToString()))
					{
						NodesToRemove.Add(EventNode);
					}
				}

				for (UK2Node_ComponentBoundEvent* EventNode : NodesToRemove)
				{
					FBlueprintEditorUtils::RemoveNode(WidgetBlueprint, EventNode, true);
					++Removed;
				}
			}

			for (UWidgetAnimation* Animation : WidgetBlueprint->Animations)
			{
				if (!Animation)
				{
					continue;
				}

				Removed += Animation->AnimationBindings.RemoveAll([&WidgetNames](const FWidgetAnimationBinding& Binding)
				{
					return WidgetNames.Contains(Binding.WidgetName.ToString()) || WidgetNames.Contains(Binding.SlotWidgetName.ToString());
				});
			}

			return Removed;
		}

		UWidget* DuplicateWidgetShallow(UWidgetBlueprint* WidgetBlueprint, UWidget* SourceWidget, const FString& NewName)
		{
			if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree || !SourceWidget)
			{
				return nullptr;
			}

			UWidget* DuplicatedWidget = DuplicateObject<UWidget>(SourceWidget, WidgetBlueprint->WidgetTree, FName(*NewName));
			if (!DuplicatedWidget)
			{
				return nullptr;
			}

			DuplicatedWidget->SetFlags(RF_Transactional);
			DuplicatedWidget->Modify();
			DuplicatedWidget->Rename(*NewName, WidgetBlueprint->WidgetTree, REN_DontCreateRedirectors);
			DuplicatedWidget->SetDisplayLabel(NewName);
			DuplicatedWidget->Slot = nullptr;

			if (UPanelWidget* DuplicatedPanel = Cast<UPanelWidget>(DuplicatedWidget))
			{
				DuplicatedPanel->ClearChildren();
			}

			EnsureWidgetBlueprintGuid(WidgetBlueprint, DuplicatedWidget);
			return DuplicatedWidget;
		}

		bool DuplicateChildSubtree(
			UWidgetBlueprint* WidgetBlueprint,
			UWidget* SourceWidget,
			UPanelWidget* DestinationParent,
			TSet<FString>& ExistingNames,
			int32& InOutNodeCount)
		{
			UPanelWidget* SourcePanel = Cast<UPanelWidget>(SourceWidget);
			if (!WidgetBlueprint || !SourcePanel || !DestinationParent)
			{
				return true;
			}

			for (int32 Index = 0; Index < SourcePanel->GetChildrenCount(); ++Index)
			{
				UWidget* SourceChild = SourcePanel->GetChildAt(Index);
				if (!SourceChild)
				{
					continue;
				}

				const FString ChildName = MakeUniqueWidgetDuplicateName(SourceChild->GetName(), FString(), ExistingNames);
				UWidget* ChildDuplicate = DuplicateWidgetShallow(WidgetBlueprint, SourceChild, ChildName);
				if (!ChildDuplicate)
				{
					return false;
				}

				ExistingNames.Add(ChildName);
				++InOutNodeCount;
				if (!DestinationParent->AddChild(ChildDuplicate, SourceChild->Slot))
				{
					return false;
				}

				if (!DuplicateChildSubtree(WidgetBlueprint, SourceChild, Cast<UPanelWidget>(ChildDuplicate), ExistingNames, InOutNodeCount))
				{
					return false;
				}
			}

			return true;
		}

		FUnrealMcpExecutionResult WidgetRenameTool(const FJsonObject& Arguments)
		{
			const FString ToolName = TEXT("unreal.widget_rename");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			FString WidgetBlueprintPath;
			FString OldName;
			FString NewName;
			bool bForce = false;
			if (!Arguments.TryGetStringField(TEXT("widgetBlueprintPath"), WidgetBlueprintPath) || WidgetBlueprintPath.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetBlueprintPath'."), nullptr, true);
			}
			if (!Arguments.TryGetStringField(TEXT("oldName"), OldName) || OldName.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'oldName'."), nullptr, true);
			}
			if (!Arguments.TryGetStringField(TEXT("newName"), NewName) || NewName.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'newName'."), nullptr, true);
			}
			Arguments.TryGetBoolField(TEXT("force"), bForce);

			OldName = OldName.TrimStartAndEnd();
			NewName = NewName.TrimStartAndEnd();
			UEditorAssetSubsystem* EditorAssetSubsystem = GetEditorAssetSubsystem();
			FString ObjectPath;
			FString FailureReason;
			UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintAsset(EditorAssetSubsystem, WidgetBlueprintPath, ObjectPath, FailureReason);
			if (!WidgetBlueprint)
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			UWidget* Widget = FindWidgetByName(WidgetBlueprint, OldName);
			if (!Widget)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Widget '%s' was not found in %s."), *OldName, *ObjectPath), nullptr, true);
			}

			if (WidgetBlueprint->WidgetTree && WidgetBlueprint->WidgetTree->FindWidget(FName(*NewName)))
			{
				TSharedPtr<FJsonObject> StructuredContent = MakeWidgetEditResult(TEXT("widget_rename"));
				StructuredContent->SetBoolField(TEXT("success"), false);
				StructuredContent->SetStringField(TEXT("oldName"), OldName);
				StructuredContent->SetStringField(TEXT("newName"), NewName);
				StructuredContent->SetArrayField(TEXT("warnings"), MakeWarningsArray({ bForce
					? TEXT("force=true cannot replace an existing widget with the requested newName.")
					: TEXT("A widget already exists with the requested newName.") }));
				return MakeExecutionResult(FString::Printf(TEXT("Cannot rename widget '%s' to '%s' because that widget name already exists."), *OldName, *NewName), StructuredContent, true);
			}

			TSet<FString> WidgetNames;
			WidgetNames.Add(OldName);
			const int32 BindingCount = CountWidgetBindingReferences(WidgetBlueprint, WidgetNames);
			const FScopedTransaction Transaction(LOCTEXT("UnrealMcpWidgetRename", "Unreal MCP Rename Widget"));
			WidgetBlueprint->Modify();
			WidgetBlueprint->WidgetTree->Modify();
			Widget->Modify();

			const FName OldFName = Widget->GetFName();
			const FName NewFName(*NewName);
			Widget->SetDisplayLabel(NewName);
			if (!Widget->Rename(*NewName, WidgetBlueprint->WidgetTree, REN_DontCreateRedirectors))
			{
				return MakeExecutionResult(FString::Printf(TEXT("Failed to rename widget '%s' to '%s'."), *OldName, *NewName), nullptr, true);
			}

			RenameWidgetBlueprintGuid(WidgetBlueprint, OldFName, Widget);
			const int32 ReferencesUpdated = UpdateWidgetReferencesForRename(WidgetBlueprint, OldFName, NewFName);
			MarkWidgetBlueprintModified(WidgetBlueprint, true);

			TSharedPtr<FJsonObject> StructuredContent = DescribeWidgetBlueprint(WidgetBlueprint, TEXT("widget_rename"));
			StructuredContent->SetBoolField(TEXT("success"), true);
			StructuredContent->SetStringField(TEXT("oldName"), OldName);
			StructuredContent->SetStringField(TEXT("newName"), NewName);
			StructuredContent->SetNumberField(TEXT("bindingCount"), BindingCount);
			StructuredContent->SetNumberField(TEXT("referencesUpdated"), ReferencesUpdated);
			StructuredContent->SetArrayField(TEXT("warnings"), MakeWarningsArray({}));
			StructuredContent->SetObjectField(TEXT("widget"), DescribeWidget(Widget));
			return MakeExecutionResult(FString::Printf(TEXT("Renamed widget %s to %s in %s."), *OldName, *NewName, *ObjectPath), StructuredContent, false);
		}

		FUnrealMcpExecutionResult WidgetReorderChildTool(const FJsonObject& Arguments)
		{
			const FString ToolName = TEXT("unreal.widget_reorder_child");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			FString WidgetBlueprintPath;
			FString WidgetName;
			double NewIndexValue = 0.0;
			if (!Arguments.TryGetStringField(TEXT("widgetBlueprintPath"), WidgetBlueprintPath) || WidgetBlueprintPath.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetBlueprintPath'."), nullptr, true);
			}
			if (!Arguments.TryGetStringField(TEXT("widgetName"), WidgetName) || WidgetName.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetName'."), nullptr, true);
			}
			if (!Arguments.TryGetNumberField(TEXT("newIndex"), NewIndexValue))
			{
				return MakeExecutionResult(TEXT("Missing or non-numeric required field 'newIndex'."), nullptr, true);
			}

			UEditorAssetSubsystem* EditorAssetSubsystem = GetEditorAssetSubsystem();
			FString ObjectPath;
			FString FailureReason;
			UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintAsset(EditorAssetSubsystem, WidgetBlueprintPath, ObjectPath, FailureReason);
			if (!WidgetBlueprint)
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			UWidget* Widget = FindWidgetByName(WidgetBlueprint, WidgetName);
			if (!Widget)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Widget '%s' was not found in %s."), *WidgetName, *ObjectPath), nullptr, true);
			}

			UPanelWidget* Parent = Widget->GetParent();
			if (!Parent)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Widget '%s' has no parent panel and cannot be reordered."), *Widget->GetName()), nullptr, true);
			}

			const int32 OldIndex = Parent->GetChildIndex(Widget);
			const int32 TotalSiblings = Parent->GetChildrenCount();
			const int32 NewIndex = FMath::Clamp(static_cast<int32>(NewIndexValue), 0, FMath::Max(0, TotalSiblings - 1));
			const FScopedTransaction Transaction(LOCTEXT("UnrealMcpWidgetReorderChild", "Unreal MCP Reorder Widget Child"));
			WidgetBlueprint->Modify();
			WidgetBlueprint->WidgetTree->Modify();
			Parent->Modify();
			Widget->Modify();
			Parent->ShiftChild(NewIndex, Widget);
			MarkWidgetBlueprintModified(WidgetBlueprint, true);

			TSharedPtr<FJsonObject> StructuredContent = DescribeWidgetBlueprint(WidgetBlueprint, TEXT("widget_reorder_child"));
			StructuredContent->SetBoolField(TEXT("success"), true);
			StructuredContent->SetNumberField(TEXT("oldIndex"), OldIndex);
			StructuredContent->SetNumberField(TEXT("newIndex"), NewIndex);
			StructuredContent->SetStringField(TEXT("parentPath"), Parent->GetPathName());
			StructuredContent->SetNumberField(TEXT("totalSiblings"), TotalSiblings);
			StructuredContent->SetObjectField(TEXT("widget"), DescribeWidget(Widget));
			return MakeExecutionResult(FString::Printf(TEXT("Moved widget %s from index %d to %d in %s."), *Widget->GetName(), OldIndex, NewIndex, *ObjectPath), StructuredContent, false);
		}

		FUnrealMcpExecutionResult WidgetDuplicateTool(const FJsonObject& Arguments)
		{
			const FString ToolName = TEXT("unreal.widget_duplicate");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			FString WidgetBlueprintPath;
			FString SourceName;
			FString RequestedName;
			bool bIncludeSubtree = true;
			if (!Arguments.TryGetStringField(TEXT("widgetBlueprintPath"), WidgetBlueprintPath) || WidgetBlueprintPath.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetBlueprintPath'."), nullptr, true);
			}
			if (!Arguments.TryGetStringField(TEXT("sourceName"), SourceName) || SourceName.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'sourceName'."), nullptr, true);
			}
			Arguments.TryGetStringField(TEXT("newName"), RequestedName);
			Arguments.TryGetBoolField(TEXT("includeSubtree"), bIncludeSubtree);

			UEditorAssetSubsystem* EditorAssetSubsystem = GetEditorAssetSubsystem();
			FString ObjectPath;
			FString FailureReason;
			UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintAsset(EditorAssetSubsystem, WidgetBlueprintPath, ObjectPath, FailureReason);
			if (!WidgetBlueprint)
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			UWidget* SourceWidget = FindWidgetByName(WidgetBlueprint, SourceName);
			if (!SourceWidget)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Widget '%s' was not found in %s."), *SourceName, *ObjectPath), nullptr, true);
			}

			UPanelWidget* Parent = SourceWidget->GetParent();
			if (!Parent)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Widget '%s' has no parent panel and cannot be duplicated beside itself."), *SourceWidget->GetName()), nullptr, true);
			}

			TSet<FString> ExistingNames;
			CollectExistingWidgetNames(WidgetBlueprint, ExistingNames);
			const FString DuplicateName = MakeUniqueWidgetDuplicateName(SourceWidget->GetName(), RequestedName, ExistingNames);
			const int32 SourceIndex = Parent->GetChildIndex(SourceWidget);

			const FScopedTransaction Transaction(LOCTEXT("UnrealMcpWidgetDuplicate", "Unreal MCP Duplicate Widget"));
			WidgetBlueprint->Modify();
			WidgetBlueprint->WidgetTree->Modify();
			Parent->Modify();
			SourceWidget->Modify();

			UWidget* DuplicatedWidget = DuplicateWidgetShallow(WidgetBlueprint, SourceWidget, DuplicateName);
			if (!DuplicatedWidget)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Failed to duplicate widget '%s'."), *SourceWidget->GetName()), nullptr, true);
			}

			int32 SubtreeNodeCount = 1;
			ExistingNames.Add(DuplicateName);
			UPanelSlot* NewSlot = Parent->InsertChildAt(SourceIndex + 1, DuplicatedWidget, SourceWidget->Slot);
			if (!NewSlot)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Failed to insert duplicated widget '%s' beside '%s'."), *DuplicateName, *SourceWidget->GetName()), nullptr, true);
			}

			if (bIncludeSubtree && !DuplicateChildSubtree(WidgetBlueprint, SourceWidget, Cast<UPanelWidget>(DuplicatedWidget), ExistingNames, SubtreeNodeCount))
			{
				return MakeExecutionResult(FString::Printf(TEXT("Duplicated widget '%s' but failed while duplicating its child subtree."), *SourceWidget->GetName()), nullptr, true);
			}

			MarkWidgetBlueprintModified(WidgetBlueprint, true);
			TSharedPtr<FJsonObject> StructuredContent = DescribeWidgetBlueprint(WidgetBlueprint, TEXT("widget_duplicate"));
			StructuredContent->SetBoolField(TEXT("success"), true);
			StructuredContent->SetStringField(TEXT("sourceName"), SourceWidget->GetName());
			StructuredContent->SetStringField(TEXT("duplicatedName"), DuplicatedWidget->GetName());
			StructuredContent->SetNumberField(TEXT("subtreeNodeCount"), SubtreeNodeCount);
			StructuredContent->SetObjectField(TEXT("widget"), DescribeWidget(DuplicatedWidget));
			return MakeExecutionResult(FString::Printf(TEXT("Duplicated widget %s as %s in %s."), *SourceWidget->GetName(), *DuplicatedWidget->GetName(), *ObjectPath), StructuredContent, false);
		}

		FUnrealMcpExecutionResult WidgetDeleteTool(const FJsonObject& Arguments)
		{
			const FString ToolName = TEXT("unreal.widget_delete");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			FString WidgetBlueprintPath;
			FString WidgetName;
			bool bForce = false;
			if (!Arguments.TryGetStringField(TEXT("widgetBlueprintPath"), WidgetBlueprintPath) || WidgetBlueprintPath.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetBlueprintPath'."), nullptr, true);
			}
			if (!Arguments.TryGetStringField(TEXT("widgetName"), WidgetName) || WidgetName.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetName'."), nullptr, true);
			}
			Arguments.TryGetBoolField(TEXT("force"), bForce);

			UEditorAssetSubsystem* EditorAssetSubsystem = GetEditorAssetSubsystem();
			FString ObjectPath;
			FString FailureReason;
			UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintAsset(EditorAssetSubsystem, WidgetBlueprintPath, ObjectPath, FailureReason);
			if (!WidgetBlueprint)
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			UWidget* Widget = FindWidgetByName(WidgetBlueprint, WidgetName);
			if (!Widget)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Widget '%s' was not found in %s."), *WidgetName, *ObjectPath), nullptr, true);
			}

			TSet<FString> RemovedWidgetNames;
			GetWidgetSubtreeNames(Widget, RemovedWidgetNames);
			const int32 SubtreeNodeCount = RemovedWidgetNames.Num();
			const int32 ReferenceCount = CountWidgetReferences(WidgetBlueprint, RemovedWidgetNames);
			const int32 BindingReferenceCount = CountWidgetBindingReferences(WidgetBlueprint, RemovedWidgetNames);
			if (ShouldRefuseWidgetDelete(ReferenceCount, bForce))
			{
				TSharedPtr<FJsonObject> StructuredContent = MakeWidgetEditResult(TEXT("widget_delete"));
				StructuredContent->SetBoolField(TEXT("success"), false);
				StructuredContent->SetStringField(TEXT("deletedName"), Widget->GetName());
				StructuredContent->SetNumberField(TEXT("bindingsBroken"), ReferenceCount);
				StructuredContent->SetNumberField(TEXT("subtreeNodeCount"), SubtreeNodeCount);
				StructuredContent->SetArrayField(TEXT("warnings"), MakeWarningsArray({
					FString::Printf(TEXT("Widget subtree has %d binding or graph reference(s). Pass force=true to delete and remove those references."), ReferenceCount)
				}));
				return MakeExecutionResult(FString::Printf(TEXT("Refused to delete widget '%s' because it has %d binding or graph reference(s)."), *Widget->GetName(), ReferenceCount), StructuredContent, true);
			}

			const FString DeletedName = Widget->GetName();
			const FScopedTransaction Transaction(LOCTEXT("UnrealMcpWidgetDelete", "Unreal MCP Delete Widget"));
			WidgetBlueprint->Modify();
			WidgetBlueprint->WidgetTree->Modify();
			Widget->Modify();

			const int32 ReferencesRemoved = RemoveWidgetReferencesForDelete(WidgetBlueprint, RemovedWidgetNames);
			const bool bRemoved = WidgetBlueprint->WidgetTree->RemoveWidget(Widget);
			if (WidgetBlueprint->WidgetTree->RootWidget == Widget)
			{
				WidgetBlueprint->WidgetTree->RootWidget = nullptr;
			}

			if (!bRemoved)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Failed to delete widget '%s' from %s."), *DeletedName, *ObjectPath), nullptr, true);
			}

			TArray<UWidget*> RemovedWidgets;
			RemovedWidgets.Add(Widget);
			TArray<UWidget*> ChildWidgets;
			UWidgetTree::GetChildWidgets(Widget, ChildWidgets);
			RemovedWidgets.Append(ChildWidgets);
			for (UWidget* RemovedWidget : RemovedWidgets)
			{
				if (!RemovedWidget)
				{
					continue;
				}

				const FName RemovedName = RemovedWidget->GetFName();
				RemovedWidget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
				RemoveWidgetBlueprintGuid(WidgetBlueprint, RemovedName);
			}

			MarkWidgetBlueprintModified(WidgetBlueprint, true);
			TSharedPtr<FJsonObject> StructuredContent = DescribeWidgetBlueprint(WidgetBlueprint, TEXT("widget_delete"));
			StructuredContent->SetBoolField(TEXT("success"), true);
			StructuredContent->SetStringField(TEXT("deletedName"), DeletedName);
			StructuredContent->SetNumberField(TEXT("bindingsBroken"), FMath::Max(BindingReferenceCount, ReferencesRemoved));
			StructuredContent->SetNumberField(TEXT("subtreeNodeCount"), SubtreeNodeCount);
			return MakeExecutionResult(FString::Printf(TEXT("Deleted widget %s from %s."), *DeletedName, *ObjectPath), StructuredContent, false);
		}
	}

	int32 CountWidgetBindingReferences(const UWidgetBlueprint* WidgetBlueprint, const TSet<FString>& WidgetNames)
	{
		if (!WidgetBlueprint)
		{
			return 0;
		}

		int32 Count = 0;
		for (const FDelegateEditorBinding& Binding : WidgetBlueprint->Bindings)
		{
			if (WidgetNames.Contains(Binding.ObjectName))
			{
				++Count;
			}
		}
		return Count;
	}

	int32 RenameWidgetBindingObjectReferences(UWidgetBlueprint* WidgetBlueprint, const FString& OldName, const FString& NewName)
	{
		if (!WidgetBlueprint)
		{
			return 0;
		}

		int32 Updated = 0;
		for (FDelegateEditorBinding& Binding : WidgetBlueprint->Bindings)
		{
			if (Binding.ObjectName == OldName)
			{
				Binding.ObjectName = NewName;
				++Updated;
			}
		}
		return Updated;
	}

	bool ShouldRefuseWidgetDelete(int32 ReferenceCount, bool bForce)
	{
		return ReferenceCount > 0 && !bForce;
	}

	FString MakeUniqueWidgetDuplicateName(const FString& SourceName, const FString& RequestedName, const TSet<FString>& ExistingNames)
	{
		FString BaseName = RequestedName.TrimStartAndEnd();
		if (BaseName.IsEmpty())
		{
			BaseName = FString::Printf(TEXT("%s_Copy"), *SourceName.TrimStartAndEnd());
		}
		if (BaseName.IsEmpty())
		{
			BaseName = TEXT("Widget_Copy");
		}

		FString Candidate = BaseName;
		int32 Suffix = 1;
		while (ExistingNames.Contains(Candidate))
		{
			Candidate = FString::Printf(TEXT("%s_%d"), *BaseName, Suffix++);
		}
		return Candidate;
	}

	bool TryExecuteWidgetEditTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult)
	{
		if (ToolName == TEXT("unreal.widget_rename"))
		{
			OutResult = WidgetRenameTool(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.widget_reorder_child"))
		{
			OutResult = WidgetReorderChildTool(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.widget_duplicate"))
		{
			OutResult = WidgetDuplicateTool(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.widget_delete"))
		{
			OutResult = WidgetDeleteTool(Arguments);
			return true;
		}

		return false;
	}
}

#undef LOCTEXT_NAMESPACE
