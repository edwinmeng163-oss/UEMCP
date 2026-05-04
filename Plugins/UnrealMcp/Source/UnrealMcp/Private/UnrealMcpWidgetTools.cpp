#include "UnrealMcpWidgetTools.h"

#include "UnrealMcpModule.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "K2Node_ComponentBoundEvent.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "UnrealMcpWidgetTools"

namespace UnrealMcp
{
	bool IsEditorPlaying();
	FUnrealMcpExecutionResult MakePieBlockedResult(const FString& ToolName);
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);
	UWidgetBlueprint* LoadWidgetBlueprintAsset(UEditorAssetSubsystem* EditorAssetSubsystem, const FString& WidgetBlueprintPath, FString& OutObjectPath, FString& OutFailureReason);
	UWidgetBlueprint* LoadOrCreateWidgetBlueprintAsset(UEditorAssetSubsystem* EditorAssetSubsystem, const FString& WidgetBlueprintPath, FString& OutObjectPath, bool& bOutCreated, FString& OutFailureReason);
	UWidget* FindWidgetByName(UWidgetBlueprint* WidgetBlueprint, const FString& WidgetName);
	UClass* ResolveWidgetClass(const FString& WidgetClassName, UEditorAssetSubsystem* EditorAssetSubsystem);
	FString MakeUniqueWidgetName(UWidgetBlueprint* WidgetBlueprint, UClass* WidgetClass, const FString& RequestedName);
	TSharedPtr<FJsonObject> DescribeWidget(UWidget* Widget);
	TSharedPtr<FJsonObject> DescribeWidgetBlueprint(UWidgetBlueprint* WidgetBlueprint, const FString& Action);
	void EnsureWidgetBlueprintGuid(UWidgetBlueprint* WidgetBlueprint, UWidget* Widget);
	void RemoveWidgetBlueprintGuid(UWidgetBlueprint* WidgetBlueprint, const FName& WidgetName);
	void RenameWidgetBlueprintGuid(UWidgetBlueprint* WidgetBlueprint, const FName& OldWidgetName, UWidget* Widget);
	void MarkWidgetBlueprintModified(UWidgetBlueprint* WidgetBlueprint, bool bStructurallyModified);
	bool ApplyStringToProperty(UObject* RootObject, const FString& PropertyPath, const FString& Value, FString& OutFailureReason, TSharedPtr<FJsonObject>& OutEditObject);
	void ApplyPanelSlotLayout(UPanelSlot* Slot, const FJsonObject& Arguments, int32& InOutChangedCount);
	TSharedPtr<FJsonObject> DescribeBlueprintNode(const UEdGraphNode* Node);
	UEdGraph* ResolveBlueprintGraph(UBlueprint* Blueprint, const FString& GraphName, bool bCreateEventGraphIfMissing, FString& OutFailureReason);
	bool BuildDefaultWidgetTemplate(UWidgetBlueprint* WidgetBlueprint, const FString& TitleText, FString& OutFailureReason);

	namespace
	{
		UEditorAssetSubsystem* GetEditorAssetSubsystem()
		{
			return GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
		}

		FUnrealMcpExecutionResult WidgetAddTool(const FJsonObject& Arguments)
		{
			const FString ToolName = TEXT("unreal.widget_add");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			UEditorAssetSubsystem* EditorAssetSubsystem = GetEditorAssetSubsystem();
			if (!EditorAssetSubsystem)
			{
				return MakeExecutionResult(TEXT("EditorAssetSubsystem is unavailable."), nullptr, true);
			}

			FString WidgetBlueprintPath;
			FString WidgetClassName;
			if (!Arguments.TryGetStringField(TEXT("widgetBlueprintPath"), WidgetBlueprintPath) || WidgetBlueprintPath.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetBlueprintPath'."), nullptr, true);
			}
			if (!Arguments.TryGetStringField(TEXT("widgetClass"), WidgetClassName) || WidgetClassName.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetClass'."), nullptr, true);
			}

			FString ObjectPath;
			FString FailureReason;
			UWidgetBlueprint* WidgetBlueprint = LoadWidgetBlueprintAsset(EditorAssetSubsystem, WidgetBlueprintPath, ObjectPath, FailureReason);
			if (!WidgetBlueprint)
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}
			if (!WidgetBlueprint->WidgetTree)
			{
				return MakeExecutionResult(TEXT("Widget Blueprint has no WidgetTree."), nullptr, true);
			}

			UClass* WidgetClass = ResolveWidgetClass(WidgetClassName, EditorAssetSubsystem);
			if (!WidgetClass)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Unable to resolve widgetClass '%s'."), *WidgetClassName), nullptr, true);
			}

			FString RequestedWidgetName;
			FString ParentWidgetName;
			double IndexValue = -1.0;
			bool bIsVariable = true;
			Arguments.TryGetStringField(TEXT("widgetName"), RequestedWidgetName);
			Arguments.TryGetStringField(TEXT("parentWidgetName"), ParentWidgetName);
			Arguments.TryGetNumberField(TEXT("index"), IndexValue);
			Arguments.TryGetBoolField(TEXT("isVariable"), bIsVariable);

			const FString WidgetName = MakeUniqueWidgetName(WidgetBlueprint, WidgetClass, RequestedWidgetName);
			const FScopedTransaction Transaction(LOCTEXT("UnrealMcpWidgetAdd", "Unreal MCP Add Widget"));
			WidgetBlueprint->Modify();
			WidgetBlueprint->WidgetTree->Modify();

			UWidget* NewWidget = WidgetBlueprint->WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*WidgetName));
			if (!NewWidget)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Failed to construct widget '%s' of class '%s'."), *WidgetName, *WidgetClass->GetPathName()), nullptr, true);
			}
			NewWidget->Modify();
			NewWidget->bIsVariable = bIsVariable;
			NewWidget->OnCreationFromPalette();

			if (!WidgetBlueprint->WidgetTree->RootWidget && ParentWidgetName.TrimStartAndEnd().IsEmpty())
			{
				WidgetBlueprint->WidgetTree->RootWidget = NewWidget;
			}
			else
			{
				UWidget* ParentWidget = FindWidgetByName(WidgetBlueprint, ParentWidgetName);
				if (!ParentWidget)
				{
					return MakeExecutionResult(FString::Printf(TEXT("Parent widget '%s' was not found."), *ParentWidgetName), nullptr, true);
				}

				UPanelWidget* ParentPanel = Cast<UPanelWidget>(ParentWidget);
				if (!ParentPanel)
				{
					return MakeExecutionResult(FString::Printf(TEXT("Parent widget '%s' is not a panel/content widget that can receive children."), *ParentWidget->GetName()), nullptr, true);
				}

				if (!ParentPanel->CanAddMoreChildren())
				{
					return MakeExecutionResult(FString::Printf(TEXT("Parent widget '%s' cannot accept children."), *ParentWidget->GetName()), nullptr, true);
				}

				UPanelSlot* NewSlot = nullptr;
				const int32 Index = static_cast<int32>(IndexValue);
				if (Index >= 0)
				{
					NewSlot = ParentPanel->InsertChildAt(FMath::Clamp(Index, 0, ParentPanel->GetChildrenCount()), NewWidget);
				}
				else
				{
					NewSlot = ParentPanel->AddChild(NewWidget);
				}

				if (!NewSlot)
				{
					return MakeExecutionResult(FString::Printf(TEXT("Failed to add widget '%s' to parent '%s'."), *WidgetName, *ParentWidget->GetName()), nullptr, true);
				}
			}

			EnsureWidgetBlueprintGuid(WidgetBlueprint, NewWidget);
			MarkWidgetBlueprintModified(WidgetBlueprint, true);

			TSharedPtr<FJsonObject> StructuredContent = DescribeWidgetBlueprint(WidgetBlueprint, TEXT("widget_add"));
			StructuredContent->SetObjectField(TEXT("widget"), DescribeWidget(NewWidget));
			return MakeExecutionResult(
				FString::Printf(TEXT("Added widget %s (%s) to %s."), *NewWidget->GetName(), *WidgetClass->GetPathName(), *ObjectPath),
				StructuredContent,
				false);
		}

		FUnrealMcpExecutionResult WidgetRemoveTool(const FJsonObject& Arguments)
		{
			const FString ToolName = TEXT("unreal.widget_remove");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			UEditorAssetSubsystem* EditorAssetSubsystem = GetEditorAssetSubsystem();
			if (!EditorAssetSubsystem)
			{
				return MakeExecutionResult(TEXT("EditorAssetSubsystem is unavailable."), nullptr, true);
			}

			FString WidgetBlueprintPath;
			FString WidgetName;
			if (!Arguments.TryGetStringField(TEXT("widgetBlueprintPath"), WidgetBlueprintPath) || WidgetBlueprintPath.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetBlueprintPath'."), nullptr, true);
			}
			if (!Arguments.TryGetStringField(TEXT("widgetName"), WidgetName) || WidgetName.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetName'."), nullptr, true);
			}

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

			const FString RemovedWidgetName = Widget->GetName();
			const FScopedTransaction Transaction(LOCTEXT("UnrealMcpWidgetRemove", "Unreal MCP Remove Widget"));
			WidgetBlueprint->Modify();
			WidgetBlueprint->WidgetTree->Modify();
			Widget->Modify();

			TArray<UWidget*> RemovedWidgets;
			RemovedWidgets.Add(Widget);
			TArray<UWidget*> ChildWidgets;
			UWidgetTree::GetChildWidgets(Widget, ChildWidgets);
			RemovedWidgets.Append(ChildWidgets);

			TSet<FString> RemovedWidgetNames;
			for (UWidget* RemovedWidget : RemovedWidgets)
			{
				if (RemovedWidget)
				{
					RemovedWidgetNames.Add(RemovedWidget->GetName());
				}
			}
			WidgetBlueprint->Bindings.RemoveAll([&RemovedWidgetNames](const FDelegateEditorBinding& Binding)
			{
				return RemovedWidgetNames.Contains(Binding.ObjectName);
			});

			const bool bRemoved = WidgetBlueprint->WidgetTree->RemoveWidget(Widget);
			if (WidgetBlueprint->WidgetTree->RootWidget == Widget)
			{
				WidgetBlueprint->WidgetTree->RootWidget = nullptr;
			}

			if (!bRemoved)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Failed to remove widget '%s' from %s."), *RemovedWidgetName, *ObjectPath), nullptr, true);
			}

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
			TSharedPtr<FJsonObject> StructuredContent = DescribeWidgetBlueprint(WidgetBlueprint, TEXT("widget_remove"));
			StructuredContent->SetStringField(TEXT("removedWidgetName"), RemovedWidgetName);
			return MakeExecutionResult(
				FString::Printf(TEXT("Removed widget %s from %s."), *RemovedWidgetName, *ObjectPath),
				StructuredContent,
				false);
		}

		FUnrealMcpExecutionResult WidgetSetPropertyTool(const FJsonObject& Arguments)
		{
			const FString ToolName = TEXT("unreal.widget_set_property");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			UEditorAssetSubsystem* EditorAssetSubsystem = GetEditorAssetSubsystem();
			if (!EditorAssetSubsystem)
			{
				return MakeExecutionResult(TEXT("EditorAssetSubsystem is unavailable."), nullptr, true);
			}

			FString WidgetBlueprintPath;
			FString WidgetName;
			FString PropertyName;
			FString Value;
			if (!Arguments.TryGetStringField(TEXT("widgetBlueprintPath"), WidgetBlueprintPath) || WidgetBlueprintPath.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetBlueprintPath'."), nullptr, true);
			}
			if (!Arguments.TryGetStringField(TEXT("widgetName"), WidgetName) || WidgetName.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetName'."), nullptr, true);
			}
			if (!Arguments.TryGetStringField(TEXT("propertyName"), PropertyName) || PropertyName.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'propertyName'."), nullptr, true);
			}
			Arguments.TryGetStringField(TEXT("value"), Value);

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

			const FScopedTransaction Transaction(LOCTEXT("UnrealMcpWidgetSetProperty", "Unreal MCP Set Widget Property"));
			WidgetBlueprint->Modify();
			TSharedPtr<FJsonObject> EditObject;
			if (!ApplyStringToProperty(Widget, PropertyName, Value, FailureReason, EditObject))
			{
				return MakeExecutionResult(FailureReason, EditObject, true);
			}

			MarkWidgetBlueprintModified(WidgetBlueprint, false);
			TSharedPtr<FJsonObject> StructuredContent = DescribeWidgetBlueprint(WidgetBlueprint, TEXT("widget_set_property"));
			StructuredContent->SetObjectField(TEXT("widget"), DescribeWidget(Widget));
			StructuredContent->SetObjectField(TEXT("edit"), EditObject);

			return MakeExecutionResult(
				FString::Printf(TEXT("Set %s.%s on %s."), *Widget->GetName(), *PropertyName, *ObjectPath),
				StructuredContent,
				false);
		}

		FUnrealMcpExecutionResult WidgetSetSlotLayoutTool(const FJsonObject& Arguments)
		{
			const FString ToolName = TEXT("unreal.widget_set_slot_layout");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			UEditorAssetSubsystem* EditorAssetSubsystem = GetEditorAssetSubsystem();
			if (!EditorAssetSubsystem)
			{
				return MakeExecutionResult(TEXT("EditorAssetSubsystem is unavailable."), nullptr, true);
			}

			FString WidgetBlueprintPath;
			FString WidgetName;
			if (!Arguments.TryGetStringField(TEXT("widgetBlueprintPath"), WidgetBlueprintPath) || WidgetBlueprintPath.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetBlueprintPath'."), nullptr, true);
			}
			if (!Arguments.TryGetStringField(TEXT("widgetName"), WidgetName) || WidgetName.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetName'."), nullptr, true);
			}

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
			if (!Widget->Slot)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Widget '%s' has no parent slot. Root widget layout cannot be edited with widget_set_slot_layout."), *Widget->GetName()), nullptr, true);
			}

			const FScopedTransaction Transaction(LOCTEXT("UnrealMcpWidgetSetSlotLayout", "Unreal MCP Set Widget Slot Layout"));
			WidgetBlueprint->Modify();
			Widget->Modify();
			int32 ChangedCount = 0;
			ApplyPanelSlotLayout(Widget->Slot, Arguments, ChangedCount);
			MarkWidgetBlueprintModified(WidgetBlueprint, false);

			TSharedPtr<FJsonObject> StructuredContent = DescribeWidgetBlueprint(WidgetBlueprint, TEXT("widget_set_slot_layout"));
			StructuredContent->SetObjectField(TEXT("widget"), DescribeWidget(Widget));
			StructuredContent->SetNumberField(TEXT("changedFieldGroups"), ChangedCount);
			return MakeExecutionResult(
				FString::Printf(TEXT("Updated slot layout for widget %s in %s. changedFieldGroups=%d"), *Widget->GetName(), *ObjectPath, ChangedCount),
				StructuredContent,
				false);
		}

		FUnrealMcpExecutionResult WidgetBindEventTool(const FJsonObject& Arguments)
		{
			const FString ToolName = TEXT("unreal.widget_bind_event");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			UEditorAssetSubsystem* EditorAssetSubsystem = GetEditorAssetSubsystem();
			if (!EditorAssetSubsystem)
			{
				return MakeExecutionResult(TEXT("EditorAssetSubsystem is unavailable."), nullptr, true);
			}

			FString WidgetBlueprintPath;
			FString WidgetName;
			FString EventName = TEXT("OnClicked");
			FString FunctionName;
			double X = 0.0;
			double Y = 0.0;
			bool bCompile = true;
			if (!Arguments.TryGetStringField(TEXT("widgetBlueprintPath"), WidgetBlueprintPath) || WidgetBlueprintPath.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetBlueprintPath'."), nullptr, true);
			}
			if (!Arguments.TryGetStringField(TEXT("widgetName"), WidgetName) || WidgetName.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetName'."), nullptr, true);
			}
			Arguments.TryGetStringField(TEXT("eventName"), EventName);
			Arguments.TryGetStringField(TEXT("functionName"), FunctionName);
			const bool bHasX = Arguments.TryGetNumberField(TEXT("x"), X);
			const bool bHasY = Arguments.TryGetNumberField(TEXT("y"), Y);
			Arguments.TryGetBoolField(TEXT("compile"), bCompile);

			const FName EventFName(*EventName.TrimStartAndEnd());
			if (EventFName.IsNone())
			{
				return MakeExecutionResult(TEXT("eventName cannot be empty."), nullptr, true);
			}

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

			FMulticastDelegateProperty* DelegateProperty = FindFProperty<FMulticastDelegateProperty>(Widget->GetClass(), EventFName);
			if (!DelegateProperty)
			{
				for (TFieldIterator<FMulticastDelegateProperty> It(Widget->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
				{
					if (It->GetName().Equals(EventName, ESearchCase::IgnoreCase))
					{
						DelegateProperty = *It;
						break;
					}
				}
			}
			if (!DelegateProperty)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Event delegate '%s' was not found on widget class '%s'."), *EventName, *Widget->GetClass()->GetPathName()), nullptr, true);
			}

			const FScopedTransaction Transaction(LOCTEXT("UnrealMcpWidgetBindEvent", "Unreal MCP Bind Widget Event"));
			WidgetBlueprint->Modify();
			Widget->Modify();
			if (!Widget->bIsVariable)
			{
				Widget->bIsVariable = true;
			}
			EnsureWidgetBlueprintGuid(WidgetBlueprint, Widget);
			ResolveBlueprintGraph(WidgetBlueprint, UEdGraphSchema_K2::GN_EventGraph.ToString(), true, FailureReason);
			MarkWidgetBlueprintModified(WidgetBlueprint, true);
			if (bCompile)
			{
				FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
			}

			FObjectProperty* VariableProperty = WidgetBlueprint->SkeletonGeneratedClass
				? FindFProperty<FObjectProperty>(WidgetBlueprint->SkeletonGeneratedClass, Widget->GetFName())
				: nullptr;
			if (!VariableProperty)
			{
				return MakeExecutionResult(
					FString::Printf(TEXT("Widget '%s' is not available as an object property on the Widget Blueprint skeleton class. Try compile=true or widget_bind_blueprint_variable first."), *Widget->GetName()),
					nullptr,
					true);
			}

			bool bCreated = false;
			const UK2Node_ComponentBoundEvent* ExistingNode = FKismetEditorUtilities::FindBoundEventForComponent(WidgetBlueprint, DelegateProperty->GetFName(), VariableProperty->GetFName());
			if (!ExistingNode)
			{
				FKismetEditorUtilities::CreateNewBoundEventForClass(Widget->GetClass(), DelegateProperty->GetFName(), WidgetBlueprint, VariableProperty);
				ExistingNode = FKismetEditorUtilities::FindBoundEventForComponent(WidgetBlueprint, DelegateProperty->GetFName(), VariableProperty->GetFName());
				bCreated = ExistingNode != nullptr;
			}

			UK2Node_ComponentBoundEvent* EventNode = const_cast<UK2Node_ComponentBoundEvent*>(ExistingNode);
			if (!EventNode)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Failed to create bound event %s for widget %s."), *DelegateProperty->GetName(), *Widget->GetName()), nullptr, true);
			}

			EventNode->Modify();
			if (!FunctionName.TrimStartAndEnd().IsEmpty())
			{
				EventNode->CustomFunctionName = FName(*FunctionName.TrimStartAndEnd());
			}
			if (bHasX || bHasY)
			{
				EventNode->NodePosX = static_cast<int32>(X);
				EventNode->NodePosY = static_cast<int32>(Y);
			}

			if (UEdGraph* Graph = EventNode->GetGraph())
			{
				Graph->NotifyGraphChanged();
			}
			MarkWidgetBlueprintModified(WidgetBlueprint, true);

			TSharedPtr<FJsonObject> StructuredContent = DescribeWidgetBlueprint(WidgetBlueprint, TEXT("widget_bind_event"));
			StructuredContent->SetObjectField(TEXT("widget"), DescribeWidget(Widget));
			StructuredContent->SetObjectField(TEXT("node"), DescribeBlueprintNode(EventNode));
			StructuredContent->SetBoolField(TEXT("created"), bCreated);
			StructuredContent->SetStringField(TEXT("eventName"), DelegateProperty->GetName());
			StructuredContent->SetStringField(TEXT("functionName"), EventNode->CustomFunctionName.ToString());
			return MakeExecutionResult(
				FString::Printf(TEXT("%s bound event %s for widget %s in %s."),
					bCreated ? TEXT("Created") : TEXT("Found existing"),
					*DelegateProperty->GetName(),
					*Widget->GetName(),
					*ObjectPath),
				StructuredContent,
				false);
		}

		FUnrealMcpExecutionResult WidgetBindBlueprintVariableTool(const FJsonObject& Arguments)
		{
			const FString ToolName = TEXT("unreal.widget_bind_blueprint_variable");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			UEditorAssetSubsystem* EditorAssetSubsystem = GetEditorAssetSubsystem();
			if (!EditorAssetSubsystem)
			{
				return MakeExecutionResult(TEXT("EditorAssetSubsystem is unavailable."), nullptr, true);
			}

			FString WidgetBlueprintPath;
			FString WidgetName;
			FString VariableName;
			bool bExpose = true;
			bool bCompile = true;
			if (!Arguments.TryGetStringField(TEXT("widgetBlueprintPath"), WidgetBlueprintPath) || WidgetBlueprintPath.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetBlueprintPath'."), nullptr, true);
			}
			if (!Arguments.TryGetStringField(TEXT("widgetName"), WidgetName) || WidgetName.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetName'."), nullptr, true);
			}
			Arguments.TryGetStringField(TEXT("variableName"), VariableName);
			Arguments.TryGetBoolField(TEXT("expose"), bExpose);
			Arguments.TryGetBoolField(TEXT("compile"), bCompile);

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

			const FScopedTransaction Transaction(LOCTEXT("UnrealMcpWidgetBindBlueprintVariable", "Unreal MCP Bind Widget Blueprint Variable"));
			WidgetBlueprint->Modify();
			Widget->Modify();

			const FName OldWidgetName = Widget->GetFName();
			FString FinalVariableName = VariableName.TrimStartAndEnd();
			if (!FinalVariableName.IsEmpty() && !FinalVariableName.Equals(Widget->GetName(), ESearchCase::CaseSensitive))
			{
				if (WidgetBlueprint->WidgetTree->FindWidget(FName(*FinalVariableName)))
				{
					return MakeExecutionResult(FString::Printf(TEXT("Cannot rename widget '%s' to '%s' because that widget name already exists."), *Widget->GetName(), *FinalVariableName), nullptr, true);
				}

				const bool bHadGuid = WidgetBlueprint->WidgetVariableNameToGuidMap.Contains(OldWidgetName);
				if (!Widget->Rename(*FinalVariableName, Widget->GetOuter(), REN_DontCreateRedirectors))
				{
					return MakeExecutionResult(FString::Printf(TEXT("Failed to rename widget '%s' to '%s'."), *OldWidgetName.ToString(), *FinalVariableName), nullptr, true);
				}
				if (bHadGuid)
				{
					RenameWidgetBlueprintGuid(WidgetBlueprint, OldWidgetName, Widget);
				}
			}

			if (bExpose && !Widget->bIsVariable)
			{
				Widget->bIsVariable = true;
			}
			else if (!bExpose && Widget->bIsVariable)
			{
				Widget->bIsVariable = false;
			}
			EnsureWidgetBlueprintGuid(WidgetBlueprint, Widget);

			MarkWidgetBlueprintModified(WidgetBlueprint, true);
			if (bCompile)
			{
				FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
			}

			TSharedPtr<FJsonObject> StructuredContent = DescribeWidgetBlueprint(WidgetBlueprint, TEXT("widget_bind_blueprint_variable"));
			StructuredContent->SetObjectField(TEXT("widget"), DescribeWidget(Widget));
			StructuredContent->SetBoolField(TEXT("expose"), bExpose);
			StructuredContent->SetBoolField(TEXT("compiled"), bCompile);
			return MakeExecutionResult(
				FString::Printf(TEXT("Widget %s is now %s as a Blueprint variable in %s."),
					*Widget->GetName(),
					bExpose ? TEXT("exposed") : TEXT("hidden"),
					*ObjectPath),
				StructuredContent,
				false);
		}

		FUnrealMcpExecutionResult WidgetBuildTemplateTool(const FJsonObject& Arguments)
		{
			const FString ToolName = TEXT("unreal.widget_build_template");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			UEditorAssetSubsystem* EditorAssetSubsystem = GetEditorAssetSubsystem();
			if (!EditorAssetSubsystem)
			{
				return MakeExecutionResult(TEXT("EditorAssetSubsystem is unavailable."), nullptr, true);
			}

			FString WidgetBlueprintPath;
			FString TemplateName = TEXT("mcp_demo_hud");
			FString TitleText = TEXT("MCP Demo");
			bool bReplaceRoot = true;
			bool bCompile = false;
			bool bSavePackage = false;
			if (!Arguments.TryGetStringField(TEXT("widgetBlueprintPath"), WidgetBlueprintPath) || WidgetBlueprintPath.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'widgetBlueprintPath'."), nullptr, true);
			}
			Arguments.TryGetStringField(TEXT("templateName"), TemplateName);
			Arguments.TryGetStringField(TEXT("title"), TitleText);
			Arguments.TryGetBoolField(TEXT("replaceRoot"), bReplaceRoot);
			Arguments.TryGetBoolField(TEXT("compile"), bCompile);
			Arguments.TryGetBoolField(TEXT("savePackage"), bSavePackage);

			if (!TemplateName.TrimStartAndEnd().Equals(TEXT("mcp_demo_hud"), ESearchCase::IgnoreCase))
			{
				return MakeExecutionResult(FString::Printf(TEXT("Unsupported widget template '%s'. Currently supported: mcp_demo_hud."), *TemplateName), nullptr, true);
			}

			FString ObjectPath;
			FString FailureReason;
			bool bCreated = false;
			UWidgetBlueprint* WidgetBlueprint = LoadOrCreateWidgetBlueprintAsset(EditorAssetSubsystem, WidgetBlueprintPath, ObjectPath, bCreated, FailureReason);
			if (!WidgetBlueprint)
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			if (!bReplaceRoot && WidgetBlueprint->WidgetTree && WidgetBlueprint->WidgetTree->RootWidget)
			{
				return MakeExecutionResult(TEXT("replaceRoot=false but the Widget Blueprint already has a root widget."), nullptr, true);
			}

			const FScopedTransaction Transaction(LOCTEXT("UnrealMcpWidgetBuildTemplate", "Unreal MCP Build Widget Template"));
			if (!BuildDefaultWidgetTemplate(WidgetBlueprint, TitleText, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}

			MarkWidgetBlueprintModified(WidgetBlueprint, true);

			bool bCompileSucceeded = true;
			if (bCompile)
			{
				FKismetEditorUtilities::CompileBlueprint(WidgetBlueprint);
				bCompileSucceeded = WidgetBlueprint->Status != BS_Error;
			}

			bool bSaved = false;
			if (bSavePackage)
			{
				bSaved = EditorAssetSubsystem->SaveLoadedAsset(WidgetBlueprint, false);
			}

			TSharedPtr<FJsonObject> StructuredContent = DescribeWidgetBlueprint(WidgetBlueprint, TEXT("widget_build_template"));
			StructuredContent->SetStringField(TEXT("objectPath"), ObjectPath);
			StructuredContent->SetStringField(TEXT("templateName"), TemplateName);
			StructuredContent->SetBoolField(TEXT("created"), bCreated);
			StructuredContent->SetBoolField(TEXT("compiled"), bCompile);
			StructuredContent->SetBoolField(TEXT("compileSucceeded"), bCompileSucceeded);
			StructuredContent->SetBoolField(TEXT("savePackage"), bSavePackage);
			StructuredContent->SetBoolField(TEXT("saved"), bSaved);

			return MakeExecutionResult(
				FString::Printf(TEXT("Built widget template %s in %s. created=%s compiled=%s saved=%s"),
					*TemplateName,
					*ObjectPath,
					bCreated ? TEXT("true") : TEXT("false"),
					bCompile ? (bCompileSucceeded ? TEXT("true") : TEXT("error")) : TEXT("false"),
					bSaved ? TEXT("true") : TEXT("false")),
				StructuredContent,
				!bCompileSucceeded || (bSavePackage && !bSaved));
		}
	}

	bool TryExecuteWidgetTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult)
	{
		if (ToolName == TEXT("unreal.widget_add"))
		{
			OutResult = WidgetAddTool(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.widget_remove"))
		{
			OutResult = WidgetRemoveTool(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.widget_set_property"))
		{
			OutResult = WidgetSetPropertyTool(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.widget_set_slot_layout"))
		{
			OutResult = WidgetSetSlotLayoutTool(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.widget_bind_event"))
		{
			OutResult = WidgetBindEventTool(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.widget_bind_blueprint_variable"))
		{
			OutResult = WidgetBindBlueprintVariableTool(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.widget_build_template"))
		{
			OutResult = WidgetBuildTemplateTool(Arguments);
			return true;
		}

		return false;
	}
}

#undef LOCTEXT_NAMESPACE
