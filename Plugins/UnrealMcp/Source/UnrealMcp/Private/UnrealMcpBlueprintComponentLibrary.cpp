#include "UnrealMcpBlueprintComponentLibrary.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/OutputDevice.h"
#include "ScopedTransaction.h"
#include "UnrealMcpEngineCompat.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "UnrealMcpBlueprintComponentLibrary"

namespace UnrealMcpBlueprintComponentLibraryPrivate
{
	constexpr const TCHAR* ComponentMutationBlockedReason = TEXT("Blueprint component mutation blocked while PIE is active");

	void ComponentLibraryReset(FString& OutValue, FString& OutFailureReason)
	{
		OutValue.Reset();
		OutFailureReason.Reset();
	}

	bool ComponentLibraryIsPieActive(FString& OutFailureReason)
	{
		if (GEditor && GEditor->PlayWorld != nullptr)
		{
			OutFailureReason = ComponentMutationBlockedReason;
			return true;
		}

		return false;
	}

	bool ComponentLibraryValidateBlueprint(UBlueprint* Blueprint, FString& OutFailureReason)
	{
		if (!Blueprint)
		{
			OutFailureReason = TEXT("Blueprint is null.");
			return false;
		}
		if (!Blueprint->GeneratedClass)
		{
			OutFailureReason = FString::Printf(TEXT("Blueprint '%s' has no generated class."), *Blueprint->GetPathName());
			return false;
		}
		if (!Blueprint->SimpleConstructionScript)
		{
			OutFailureReason = FString::Printf(TEXT("Blueprint '%s' has no SimpleConstructionScript."), *Blueprint->GetPathName());
			return false;
		}

		return true;
	}

	bool ComponentLibraryIsDirectPropertyName(const FString& PropertyName, FString& OutFailureReason)
	{
		const FString TrimmedName = PropertyName.TrimStartAndEnd();
		if (TrimmedName.IsEmpty())
		{
			OutFailureReason = TEXT("propertyName is required.");
			return false;
		}
		if (TrimmedName.Contains(TEXT(".")) || TrimmedName.Contains(TEXT("[")) || TrimmedName.Contains(TEXT("]")))
		{
			OutFailureReason = FString::Printf(TEXT("Property '%s' is not allowed: only direct property names are supported."), *TrimmedName);
			return false;
		}

		return true;
	}

	bool ComponentLibraryIsAllowedStructProperty(const FStructProperty* StructProperty)
	{
		if (!StructProperty || !StructProperty->Struct)
		{
			return false;
		}

		const UScriptStruct* Struct = StructProperty->Struct;
		return Struct == TBaseStructure<FVector>::Get()
			|| Struct == TBaseStructure<FVector2D>::Get()
			|| Struct == TBaseStructure<FVector4>::Get()
			|| Struct == TBaseStructure<FRotator>::Get()
			|| Struct == TBaseStructure<FTransform>::Get()
			|| Struct == TBaseStructure<FQuat>::Get()
			|| Struct == TBaseStructure<FLinearColor>::Get()
			|| Struct == TBaseStructure<FColor>::Get();
	}

	bool ComponentLibraryIsAllowedPropertyType(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		if (Property->ArrayDim != 1)
		{
			return false;
		}

		if (Property->IsA<FArrayProperty>() || Property->IsA<FSetProperty>() || Property->IsA<FMapProperty>())
		{
			return false;
		}

		if (Property->IsA<FBoolProperty>()
			|| Property->IsA<FNumericProperty>()
			|| Property->IsA<FNameProperty>()
			|| Property->IsA<FStrProperty>()
			|| Property->IsA<FTextProperty>()
			|| Property->IsA<FEnumProperty>()
			|| Property->IsA<FObjectPropertyBase>())
		{
			return true;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			return ComponentLibraryIsAllowedStructProperty(StructProperty);
		}

		return false;
	}

	bool ComponentLibraryValidatePropertyForWrite(const FProperty* Property, FString& OutFailureReason)
	{
		if (!Property)
		{
			OutFailureReason = TEXT("Property is null.");
			return false;
		}
		if (!Property->HasAnyPropertyFlags(CPF_Edit))
		{
			OutFailureReason = FString::Printf(TEXT("Property '%s' is not editor-editable."), *Property->GetName());
			return false;
		}
		if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_EditConst))
		{
			OutFailureReason = FString::Printf(TEXT("Property '%s' has unsupported transient, deprecated, or edit-const flags."), *Property->GetName());
			return false;
		}
		if (!ComponentLibraryIsAllowedPropertyType(Property))
		{
			OutFailureReason = FString::Printf(TEXT("Property '%s' type '%s' is outside the allowed direct value set."), *Property->GetName(), *Property->GetCPPType());
			return false;
		}

		return true;
	}

	FProperty* ComponentLibraryFindWritableProperty(UObject* TargetObject, const FString& PropertyName, FString& OutFailureReason)
	{
		if (!TargetObject)
		{
			OutFailureReason = TEXT("Target object is null.");
			return nullptr;
		}
		if (!ComponentLibraryIsDirectPropertyName(PropertyName, OutFailureReason))
		{
			return nullptr;
		}

		const FName PropertyFName(*PropertyName.TrimStartAndEnd());
		FProperty* Property = FindFProperty<FProperty>(TargetObject->GetClass(), PropertyFName);
		if (!Property)
		{
			OutFailureReason = FString::Printf(TEXT("Property '%s' was not found on '%s'."), *PropertyName.TrimStartAndEnd(), *TargetObject->GetClass()->GetPathName());
			return nullptr;
		}
		if (!ComponentLibraryValidatePropertyForWrite(Property, OutFailureReason))
		{
			return nullptr;
		}

		return Property;
	}

	FString ComponentLibraryExportPropertyValue(FProperty* Property, const void* ValuePtr, UObject* TargetObject)
	{
		FString ExportedText;
		if (Property && ValuePtr)
		{
			Property->ExportTextItem_Direct(ExportedText, ValuePtr, nullptr, TargetObject, PPF_None);
		}
		return ExportedText;
	}

	bool ComponentLibrarySetObjectPropertyFromText(
		UObject* TargetObject,
		const FString& PropertyName,
		const FString& ValueText,
		FString& OutReadbackValue,
		FString& OutFailureReason)
	{
		ComponentLibraryReset(OutReadbackValue, OutFailureReason);

		FProperty* Property = ComponentLibraryFindWritableProperty(TargetObject, PropertyName, OutFailureReason);
		if (!Property)
		{
			return false;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(TargetObject);
		if (!ValuePtr)
		{
			OutFailureReason = FString::Printf(TEXT("Property '%s' has no value storage on '%s'."), *Property->GetName(), *TargetObject->GetPathName());
			return false;
		}

		FStringOutputDevice ImportErrors;
		TargetObject->Modify();
		TargetObject->PreEditChange(Property);
		const TCHAR* ImportResult = Property->ImportText_Direct(*ValueText, ValuePtr, TargetObject, PPF_None, &ImportErrors);
		if (!ImportResult)
		{
			OutFailureReason = FString::Printf(
				TEXT("Failed to import value '%s' into property '%s'%s%s."),
				*ValueText,
				*Property->GetName(),
				ImportErrors.IsEmpty() ? TEXT("") : TEXT(": "),
				ImportErrors.IsEmpty() ? TEXT("") : *FString(ImportErrors));
			return false;
		}

		FPropertyChangedEvent PropertyChangedEvent(Property, EPropertyChangeType::ValueSet);
		TargetObject->PostEditChangeProperty(PropertyChangedEvent);
		TargetObject->MarkPackageDirty();
		OutReadbackValue = ComponentLibraryExportPropertyValue(Property, ValuePtr, TargetObject);
		OutFailureReason.Reset();
		return true;
	}

	USceneComponent* ComponentLibraryFindNativeSceneComponentTemplate(UBlueprint* Blueprint, const FName ComponentName)
	{
		if (!Blueprint || !Blueprint->GeneratedClass || ComponentName.IsNone())
		{
			return nullptr;
		}

		AActor* DefaultActor = Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject());
		if (!DefaultActor)
		{
			return nullptr;
		}

		TArray<UActorComponent*> Components;
		DefaultActor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
			if (!SceneComponent)
			{
				continue;
			}

			if (SceneComponent->GetFName() == ComponentName
				|| SceneComponent->GetName().Equals(ComponentName.ToString(), ESearchCase::IgnoreCase))
			{
				return SceneComponent;
			}
		}

		return nullptr;
	}

	bool ComponentLibraryResolveParent(
		UBlueprint* Blueprint,
		USimpleConstructionScript* SCS,
		const FName AttachParentComponentName,
		USCS_Node*& OutParentNode,
		USceneComponent*& OutNativeParentTemplate,
		FString& OutFailureReason)
	{
		OutParentNode = nullptr;
		OutNativeParentTemplate = nullptr;
		OutFailureReason.Reset();

		if (!SCS)
		{
			OutFailureReason = TEXT("SimpleConstructionScript is null.");
			return false;
		}

		if (!AttachParentComponentName.IsNone())
		{
			OutParentNode = SCS->FindSCSNode(AttachParentComponentName);
			if (OutParentNode)
			{
				if (!Cast<USceneComponent>(OutParentNode->ComponentTemplate))
				{
					OutFailureReason = FString::Printf(TEXT("Attach parent '%s' is not a scene component."), *AttachParentComponentName.ToString());
					return false;
				}
				return true;
			}

			OutNativeParentTemplate = ComponentLibraryFindNativeSceneComponentTemplate(Blueprint, AttachParentComponentName);
			if (OutNativeParentTemplate)
			{
				return true;
			}

			OutFailureReason = FString::Printf(TEXT("Attach parent component '%s' was not found."), *AttachParentComponentName.ToString());
			return false;
		}

		SCS->ValidateSceneRootNodes();
		OutNativeParentTemplate = SCS->GetSceneRootComponentTemplate(true, &OutParentNode);
		return true;
	}

	bool ComponentLibraryAttachNewNode(
		UBlueprint* Blueprint,
		USCS_Node* NewNode,
		const FName AttachParentComponentName,
		FString& OutParentName,
		FString& OutFailureReason)
	{
		OutParentName.Reset();
		OutFailureReason.Reset();

		if (!Blueprint || !Blueprint->SimpleConstructionScript || !NewNode)
		{
			OutFailureReason = TEXT("Blueprint, SimpleConstructionScript, or new SCS node is null.");
			return false;
		}

		USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
		UActorComponent* ComponentTemplate = NewNode->ComponentTemplate;
		const bool bIsSceneComponent = ComponentTemplate && ComponentTemplate->IsA<USceneComponent>();
		if (!bIsSceneComponent)
		{
			if (!AttachParentComponentName.IsNone())
			{
				OutFailureReason = TEXT("Only scene components can attach under a parent component.");
				return false;
			}
			SCS->AddNode(NewNode);
			return true;
		}

		USCS_Node* ParentNode = nullptr;
		USceneComponent* NativeParentTemplate = nullptr;
		if (!ComponentLibraryResolveParent(Blueprint, SCS, AttachParentComponentName, ParentNode, NativeParentTemplate, OutFailureReason))
		{
			return false;
		}

		if (ParentNode)
		{
			OutParentName = ParentNode->GetVariableName().ToString();
			if (ParentNode->GetSCS() == SCS)
			{
				ParentNode->AddChildNode(NewNode);
			}
			else
			{
				SCS->AddNode(NewNode);
				NewNode->SetParent(ParentNode);
			}
			return true;
		}

		if (NativeParentTemplate)
		{
			OutParentName = NativeParentTemplate->GetName();
			SCS->AddNode(NewNode);
			NewNode->SetParent(NativeParentTemplate);
			return true;
		}

		const TArray<USCS_Node*>& RootNodes = SCS->GetRootNodes();
		if (RootNodes.Num() > 0 && RootNodes[0] && Cast<USceneComponent>(RootNodes[0]->ComponentTemplate))
		{
			OutParentName = RootNodes[0]->GetVariableName().ToString();
			RootNodes[0]->AddChildNode(NewNode);
			return true;
		}

		SCS->AddNode(NewNode);
		return true;
	}
}

bool UUnrealMcpBlueprintComponentLibrary::AddComponentToBlueprint(
	UBlueprint* Blueprint,
	UClass* ComponentClass,
	FName ComponentName,
	FName AttachParentComponentName,
	FString& OutCreatedNodeName,
	FString& OutFailureReason)
{
	using namespace UnrealMcpBlueprintComponentLibraryPrivate;

	ComponentLibraryReset(OutCreatedNodeName, OutFailureReason);
	if (ComponentLibraryIsPieActive(OutFailureReason) || !ComponentLibraryValidateBlueprint(Blueprint, OutFailureReason))
	{
		return false;
	}
	if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		OutFailureReason = TEXT("ComponentClass must resolve to a UActorComponent subclass.");
		return false;
	}
	if (ComponentName.IsNone())
	{
		OutFailureReason = TEXT("ComponentName is None.");
		return false;
	}

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (SCS->FindSCSNode(ComponentName) || ComponentLibraryFindNativeSceneComponentTemplate(Blueprint, ComponentName))
	{
		OutFailureReason = FString::Printf(TEXT("Component '%s' already exists on Blueprint '%s'."), *ComponentName.ToString(), *Blueprint->GetPathName());
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddComponentToBlueprint", "Unreal MCP Add Blueprint Component"));
	Blueprint->Modify();
	SCS->Modify();
	USCS_Node* NewNode = SCS->CreateNode(ComponentClass, ComponentName);
	if (!NewNode)
	{
		OutFailureReason = FString::Printf(TEXT("Failed to create SCS node for component '%s'."), *ComponentName.ToString());
		return false;
	}
	NewNode->Modify();
	if (NewNode->ComponentTemplate)
	{
		NewNode->ComponentTemplate->Modify();
	}

	FString ParentName;
	if (!ComponentLibraryAttachNewNode(Blueprint, NewNode, AttachParentComponentName, ParentName, OutFailureReason))
	{
		return false;
	}

	SCS->ValidateSceneRootNodes();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	Blueprint->MarkPackageDirty();

	OutCreatedNodeName = NewNode->GetVariableName().ToString();
	OutFailureReason.Reset();
	return true;
}

bool UUnrealMcpBlueprintComponentLibrary::SetBlueprintComponentTemplateProperty(
	UBlueprint* Blueprint,
	FName ComponentName,
	const FString& PropertyName,
	const FString& ValueText,
	FString& OutReadbackValue,
	FString& OutFailureReason)
{
	using namespace UnrealMcpBlueprintComponentLibraryPrivate;

	ComponentLibraryReset(OutReadbackValue, OutFailureReason);
	if (ComponentLibraryIsPieActive(OutFailureReason) || !ComponentLibraryValidateBlueprint(Blueprint, OutFailureReason))
	{
		return false;
	}
	if (ComponentName.IsNone())
	{
		OutFailureReason = TEXT("ComponentName is None.");
		return false;
	}

	USCS_Node* ComponentNode = Blueprint->SimpleConstructionScript->FindSCSNode(ComponentName);
	if (!ComponentNode || !ComponentNode->ComponentTemplate)
	{
		OutFailureReason = FString::Printf(TEXT("Component template '%s' was not found on Blueprint '%s'."), *ComponentName.ToString(), *Blueprint->GetPathName());
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetBlueprintComponentTemplateProperty", "Unreal MCP Set Blueprint Component Template Property"));
	Blueprint->Modify();
	ComponentNode->Modify();
	const bool bApplied = ComponentLibrarySetObjectPropertyFromText(
		ComponentNode->ComponentTemplate,
		PropertyName,
		ValueText,
		OutReadbackValue,
		OutFailureReason);
	if (!bApplied)
	{
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	Blueprint->MarkPackageDirty();
	return true;
}

bool UUnrealMcpBlueprintComponentLibrary::SetBlueprintClassDefaultProperty(
	UBlueprint* Blueprint,
	const FString& PropertyName,
	const FString& ValueText,
	FString& OutReadbackValue,
	FString& OutFailureReason)
{
	using namespace UnrealMcpBlueprintComponentLibraryPrivate;

	ComponentLibraryReset(OutReadbackValue, OutFailureReason);
	if (ComponentLibraryIsPieActive(OutFailureReason) || !ComponentLibraryValidateBlueprint(Blueprint, OutFailureReason))
	{
		return false;
	}

	UObject* ClassDefaultObject = Blueprint->GeneratedClass->GetDefaultObject();
	if (!ClassDefaultObject)
	{
		OutFailureReason = FString::Printf(TEXT("Blueprint '%s' generated class has no class default object."), *Blueprint->GetPathName());
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetBlueprintClassDefaultProperty", "Unreal MCP Set Blueprint Class Default"));
	Blueprint->Modify();
	const bool bApplied = ComponentLibrarySetObjectPropertyFromText(
		ClassDefaultObject,
		PropertyName,
		ValueText,
		OutReadbackValue,
		OutFailureReason);
	if (!bApplied)
	{
		return false;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	Blueprint->MarkPackageDirty();
	return true;
}

#undef LOCTEXT_NAMESPACE
