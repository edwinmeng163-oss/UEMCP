#include "UnrealMcpMaterialInstanceTools.h"

#include "UnrealMcpModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorSupportDelegates.h"
#include "EditorScriptingHelpers.h"
#include "Engine/Texture.h"
#include "Math/Color.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"

#define LOCTEXT_NAMESPACE "UnrealMcpMaterialInstanceTools"

namespace UnrealMcp
{
	bool IsEditorPlaying();
	FUnrealMcpExecutionResult MakePieBlockedResult(const FString& ToolName);
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);

	namespace
	{
		constexpr int32 DefaultMaterialListLimit = 200;

		UEditorAssetSubsystem* GetEditorAssetSubsystem()
		{
			return GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
		}

		int32 GetPositiveIntArgument(const FJsonObject& Arguments, const FString& FieldName, int32 DefaultValue)
		{
			double Value = static_cast<double>(DefaultValue);
			if (Arguments.TryGetNumberField(FieldName, Value))
			{
				return FMath::Clamp(static_cast<int32>(Value), 1, 1000);
			}
			return DefaultValue;
		}

		TSharedPtr<FJsonObject> MakeErrorObject(const FString& Action, const FString& ErrorKind, const FString& Message)
		{
			TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
			ErrorObject->SetStringField(TEXT("action"), Action);
			ErrorObject->SetBoolField(TEXT("success"), false);
			ErrorObject->SetStringField(TEXT("errorKind"), ErrorKind);
			ErrorObject->SetStringField(TEXT("message"), Message);
			return ErrorObject;
		}

		FUnrealMcpExecutionResult MakeTypeMismatchResult(const FString& Action, const FString& Message)
		{
			return MakeExecutionResult(Message, MakeErrorObject(Action, TEXT("TypeMismatch"), Message), true);
		}

		TSharedPtr<FJsonObject> MakeColorObject(const FLinearColor& Color)
		{
			TSharedPtr<FJsonObject> ColorObject = MakeShared<FJsonObject>();
			ColorObject->SetNumberField(TEXT("r"), Color.R);
			ColorObject->SetNumberField(TEXT("g"), Color.G);
			ColorObject->SetNumberField(TEXT("b"), Color.B);
			ColorObject->SetNumberField(TEXT("a"), Color.A);
			return ColorObject;
		}

		TSharedPtr<FJsonObject> MakeParameterObject(const FMaterialParameterInfo& ParameterInfo, bool bFromParent)
		{
			TSharedPtr<FJsonObject> ParameterObject = MakeShared<FJsonObject>();
			ParameterObject->SetStringField(TEXT("name"), ParameterInfo.Name.ToString());
			ParameterObject->SetBoolField(TEXT("fromParent"), bFromParent);
			return ParameterObject;
		}

		bool LoadMaterialInstanceAsset(
			UEditorAssetSubsystem* EditorAssetSubsystem,
			const FString& MaterialInstancePath,
			FString& OutObjectPath,
			UMaterialInstance*& OutMaterialInstance,
			FString& OutFailureReason)
		{
			OutObjectPath.Reset();
			OutMaterialInstance = nullptr;
			OutFailureReason.Reset();

			if (!EditorAssetSubsystem)
			{
				OutFailureReason = TEXT("EditorAssetSubsystem is unavailable.");
				return false;
			}

			const FString ObjectPath = EditorScriptingHelpers::ConvertAnyPathToObjectPath(MaterialInstancePath, OutFailureReason);
			if (ObjectPath.IsEmpty())
			{
				return false;
			}

			UObject* LoadedAsset = EditorAssetSubsystem->LoadAsset(ObjectPath);
			OutObjectPath = ObjectPath;
			OutMaterialInstance = Cast<UMaterialInstance>(LoadedAsset);
			if (!OutMaterialInstance)
			{
				OutFailureReason = FString::Printf(TEXT("Asset '%s' is not a Material Instance."), *ObjectPath);
				return false;
			}
			return true;
		}

		UMaterialInstanceConstant* LoadMaterialInstanceConstantForWrite(
			const FJsonObject& Arguments,
			const FString& Action,
			FString& OutObjectPath,
			FString& OutFailureReason,
			FUnrealMcpExecutionResult& OutErrorResult)
		{
			FString MaterialInstancePath;
			if (!Arguments.TryGetStringField(TEXT("materialInstancePath"), MaterialInstancePath) || MaterialInstancePath.TrimStartAndEnd().IsEmpty())
			{
				OutFailureReason = TEXT("Missing required field 'materialInstancePath'.");
				OutErrorResult = MakeExecutionResult(OutFailureReason, MakeErrorObject(Action, TEXT("MissingField"), OutFailureReason), true);
				return nullptr;
			}

			UMaterialInstance* MaterialInstance = nullptr;
			if (!LoadMaterialInstanceAsset(GetEditorAssetSubsystem(), MaterialInstancePath, OutObjectPath, MaterialInstance, OutFailureReason))
			{
				OutErrorResult = MakeExecutionResult(OutFailureReason, MakeErrorObject(Action, TEXT("LoadFailed"), OutFailureReason), true);
				return nullptr;
			}

			UMaterialInstanceConstant* ConstantInstance = Cast<UMaterialInstanceConstant>(MaterialInstance);
			if (!ConstantInstance)
			{
				OutFailureReason = FString::Printf(TEXT("Asset '%s' is not a UMaterialInstanceConstant and cannot be edited by this tool."), *OutObjectPath);
				OutErrorResult = MakeExecutionResult(OutFailureReason, MakeErrorObject(Action, TEXT("UnsupportedMaterialInstanceClass"), OutFailureReason), true);
				return nullptr;
			}
			return ConstantInstance;
		}

		bool FindScalarParameterInfo(UMaterialInstance* MaterialInstance, const FName& ParameterName, FMaterialParameterInfo& OutParameterInfo)
		{
			if (!MaterialInstance)
			{
				return false;
			}

			TArray<FMaterialParameterInfo> ParameterInfos;
			TArray<FGuid> ParameterIds;
			MaterialInstance->GetAllScalarParameterInfo(ParameterInfos, ParameterIds);
			for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
			{
				if (ParameterInfo.Name == ParameterName)
				{
					OutParameterInfo = ParameterInfo;
					return true;
				}
			}
			return false;
		}

		bool FindVectorParameterInfo(UMaterialInstance* MaterialInstance, const FName& ParameterName, FMaterialParameterInfo& OutParameterInfo)
		{
			if (!MaterialInstance)
			{
				return false;
			}

			TArray<FMaterialParameterInfo> ParameterInfos;
			TArray<FGuid> ParameterIds;
			MaterialInstance->GetAllVectorParameterInfo(ParameterInfos, ParameterIds);
			for (const FMaterialParameterInfo& ParameterInfo : ParameterInfos)
			{
				if (ParameterInfo.Name == ParameterName)
				{
					OutParameterInfo = ParameterInfo;
					return true;
				}
			}
			return false;
		}

		void UpdateMaterialInstanceAfterEdit(UMaterialInstanceConstant* MaterialInstance)
		{
			if (!MaterialInstance)
			{
				return;
			}

			MaterialInstance->PreEditChange(nullptr);
			MaterialInstance->PostEditChange();
			MaterialInstance->MarkPackageDirty();
			MaterialInstance->UpdateStaticPermutation();
			FEditorDelegates::RefreshEditor.Broadcast();
			FEditorSupportDelegates::RedrawAllViewports.Broadcast();
		}

		FUnrealMcpExecutionResult MaterialInstanceListTool(const FJsonObject& Arguments)
		{
			FString ContentRoot = TEXT("/Game");
			FString ClassFilter;
			bool bRecursive = true;
			Arguments.TryGetStringField(TEXT("contentRoot"), ContentRoot);
			Arguments.TryGetBoolField(TEXT("recursive"), bRecursive);
			Arguments.TryGetStringField(TEXT("classFilter"), ClassFilter);
			const int32 Limit = GetPositiveIntArgument(Arguments, TEXT("limit"), DefaultMaterialListLimit);

			ContentRoot = ContentRoot.TrimStartAndEnd();
			if (ContentRoot.IsEmpty() || !ContentRoot.StartsWith(TEXT("/")))
			{
				return MakeExecutionResult(TEXT("contentRoot must be a Content Browser path like /Game."), nullptr, true);
			}

			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			FARFilter Filter;
			Filter.PackagePaths.Add(*ContentRoot);
			Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
			Filter.bRecursiveClasses = true;
			Filter.bRecursivePaths = bRecursive;

			TArray<FAssetData> Assets;
			AssetRegistryModule.Get().GetAssets(Filter, Assets);
			Assets.Sort([](const FAssetData& A, const FAssetData& B)
			{
				return A.GetSoftObjectPath().ToString() < B.GetSoftObjectPath().ToString();
			});

			TArray<TSharedPtr<FJsonValue>> InstanceValues;
			int32 TotalCount = 0;
			bool bTruncated = false;
			for (const FAssetData& Asset : Assets)
			{
				const FString AssetClassPath = Asset.AssetClassPath.ToString();
				if (!ClassFilter.IsEmpty()
					&& !AssetClassPath.Equals(ClassFilter, ESearchCase::IgnoreCase)
					&& !AssetClassPath.Contains(ClassFilter, ESearchCase::IgnoreCase))
				{
					continue;
				}

				++TotalCount;
				if (InstanceValues.Num() >= Limit)
				{
					bTruncated = true;
					continue;
				}

				TSharedPtr<FJsonObject> InstanceObject = MakeShared<FJsonObject>();
				InstanceObject->SetStringField(TEXT("path"), Asset.GetSoftObjectPath().ToString());
				InstanceObject->SetStringField(TEXT("classPath"), AssetClassPath);
				FString ParentPath;
				if (UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Asset.GetAsset()))
				{
					ParentPath = MaterialInstance->Parent ? MaterialInstance->Parent->GetPathName() : FString();
				}
				InstanceObject->SetStringField(TEXT("parent"), ParentPath);
				InstanceValues.Add(MakeShared<FJsonValueObject>(InstanceObject));
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("contentRoot"), ContentRoot);
			StructuredContent->SetBoolField(TEXT("recursive"), bRecursive);
			StructuredContent->SetStringField(TEXT("classFilter"), ClassFilter);
			StructuredContent->SetNumberField(TEXT("totalCount"), TotalCount);
			StructuredContent->SetNumberField(TEXT("returnedCount"), InstanceValues.Num());
			StructuredContent->SetBoolField(TEXT("truncated"), bTruncated);
			StructuredContent->SetArrayField(TEXT("instances"), InstanceValues);
			return MakeExecutionResult(FString::Printf(TEXT("Found %d material instance asset(s) under %s."), TotalCount, *ContentRoot), StructuredContent, false);
		}

		FUnrealMcpExecutionResult MaterialInstanceGetParametersTool(const FJsonObject& Arguments)
		{
			FString MaterialInstancePath;
			bool bIncludeInherited = true;
			if (!Arguments.TryGetStringField(TEXT("materialInstancePath"), MaterialInstancePath) || MaterialInstancePath.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'materialInstancePath'."), nullptr, true);
			}
			Arguments.TryGetBoolField(TEXT("includeInherited"), bIncludeInherited);

			FString ObjectPath;
			FString FailureReason;
			UMaterialInstance* MaterialInstance = nullptr;
			if (!LoadMaterialInstanceAsset(GetEditorAssetSubsystem(), MaterialInstancePath, ObjectPath, MaterialInstance, FailureReason))
			{
				return MakeExecutionResult(FailureReason, MakeErrorObject(TEXT("material_instance_get_parameters"), TEXT("LoadFailed"), FailureReason), true);
			}

			TArray<TSharedPtr<FJsonValue>> ScalarValues;
			TArray<FMaterialParameterInfo> ScalarInfos;
			TArray<FGuid> ScalarIds;
			MaterialInstance->GetAllScalarParameterInfo(ScalarInfos, ScalarIds);
			for (const FMaterialParameterInfo& ParameterInfo : ScalarInfos)
			{
				float Value = 0.0f;
				const bool bHasOverride = MaterialInstance->GetScalarParameterValue(ParameterInfo, Value, true);
				if (!bHasOverride && (!bIncludeInherited || !MaterialInstance->GetScalarParameterValue(ParameterInfo, Value, false)))
				{
					continue;
				}

				TSharedPtr<FJsonObject> ParameterObject = MakeParameterObject(ParameterInfo, !bHasOverride);
				ParameterObject->SetNumberField(TEXT("value"), Value);
				ScalarValues.Add(MakeShared<FJsonValueObject>(ParameterObject));
			}

			TArray<TSharedPtr<FJsonValue>> VectorValues;
			TArray<FMaterialParameterInfo> VectorInfos;
			TArray<FGuid> VectorIds;
			MaterialInstance->GetAllVectorParameterInfo(VectorInfos, VectorIds);
			for (const FMaterialParameterInfo& ParameterInfo : VectorInfos)
			{
				FLinearColor Value = FLinearColor::Black;
				const bool bHasOverride = MaterialInstance->GetVectorParameterValue(ParameterInfo, Value, true);
				if (!bHasOverride && (!bIncludeInherited || !MaterialInstance->GetVectorParameterValue(ParameterInfo, Value, false)))
				{
					continue;
				}

				TSharedPtr<FJsonObject> ParameterObject = MakeParameterObject(ParameterInfo, !bHasOverride);
				ParameterObject->SetObjectField(TEXT("value"), MakeColorObject(Value));
				VectorValues.Add(MakeShared<FJsonValueObject>(ParameterObject));
			}

			TArray<TSharedPtr<FJsonValue>> TextureValues;
			TArray<FMaterialParameterInfo> TextureInfos;
			TArray<FGuid> TextureIds;
			MaterialInstance->GetAllTextureParameterInfo(TextureInfos, TextureIds);
			for (const FMaterialParameterInfo& ParameterInfo : TextureInfos)
			{
				UTexture* Texture = nullptr;
				const bool bHasOverride = MaterialInstance->GetTextureParameterValue(ParameterInfo, Texture, true);
				if (!bHasOverride && (!bIncludeInherited || !MaterialInstance->GetTextureParameterValue(ParameterInfo, Texture, false)))
				{
					continue;
				}

				TSharedPtr<FJsonObject> ParameterObject = MakeParameterObject(ParameterInfo, !bHasOverride);
				ParameterObject->SetStringField(TEXT("texturePath"), Texture ? Texture->GetPathName() : FString());
				TextureValues.Add(MakeShared<FJsonValueObject>(ParameterObject));
			}

			TArray<TSharedPtr<FJsonValue>> StaticSwitchValues;
			TArray<FMaterialParameterInfo> StaticSwitchInfos;
			TArray<FGuid> StaticSwitchIds;
			MaterialInstance->GetAllStaticSwitchParameterInfo(StaticSwitchInfos, StaticSwitchIds);
			for (const FMaterialParameterInfo& ParameterInfo : StaticSwitchInfos)
			{
				bool bValue = false;
				FGuid ExpressionGuid;
				const bool bHasOverride = MaterialInstance->GetStaticSwitchParameterValue(ParameterInfo, bValue, ExpressionGuid, true);
				if (!bHasOverride && (!bIncludeInherited || !MaterialInstance->GetStaticSwitchParameterValue(ParameterInfo, bValue, ExpressionGuid, false)))
				{
					continue;
				}

				TSharedPtr<FJsonObject> ParameterObject = MakeParameterObject(ParameterInfo, !bHasOverride);
				ParameterObject->SetBoolField(TEXT("value"), bValue);
				StaticSwitchValues.Add(MakeShared<FJsonValueObject>(ParameterObject));
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), TEXT("material_instance_get_parameters"));
			StructuredContent->SetStringField(TEXT("materialInstancePath"), ObjectPath);
			StructuredContent->SetStringField(TEXT("parentMaterialPath"), MaterialInstance->Parent ? MaterialInstance->Parent->GetPathName() : FString());
			StructuredContent->SetBoolField(TEXT("includeInherited"), bIncludeInherited);
			StructuredContent->SetArrayField(TEXT("scalarParameters"), ScalarValues);
			StructuredContent->SetArrayField(TEXT("vectorParameters"), VectorValues);
			StructuredContent->SetArrayField(TEXT("textureParameters"), TextureValues);
			StructuredContent->SetArrayField(TEXT("staticSwitchParameters"), StaticSwitchValues);
			return MakeExecutionResult(FString::Printf(TEXT("Read parameters for material instance %s."), *ObjectPath), StructuredContent, false);
		}

		FUnrealMcpExecutionResult MaterialInstanceSetScalarTool(const FJsonObject& Arguments)
		{
			const FString Action = TEXT("material_instance_set_scalar");
			const FString ToolName = TEXT("unreal.material_instance_set_scalar");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			double NewValueDouble = 0.0;
			FString TypeFailureReason;
			if (!TryGetStrictJsonNumber(Arguments, TEXT("value"), NewValueDouble, TypeFailureReason))
			{
				return MakeTypeMismatchResult(Action, TypeFailureReason);
			}

			FString ParameterNameString;
			if (!Arguments.TryGetStringField(TEXT("parameterName"), ParameterNameString) || ParameterNameString.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'parameterName'."), MakeErrorObject(Action, TEXT("MissingField"), TEXT("Missing required field 'parameterName'.")), true);
			}

			FString ObjectPath;
			FString FailureReason;
			FUnrealMcpExecutionResult ErrorResult;
			UMaterialInstanceConstant* MaterialInstance = LoadMaterialInstanceConstantForWrite(Arguments, Action, ObjectPath, FailureReason, ErrorResult);
			if (!MaterialInstance)
			{
				return ErrorResult;
			}

			const FName ParameterName(*ParameterNameString.TrimStartAndEnd());
			FMaterialParameterInfo ParameterInfo;
			if (!FindScalarParameterInfo(MaterialInstance, ParameterName, ParameterInfo))
			{
				const FString Message = FString::Printf(TEXT("Parameter '%s' is not a scalar parameter on %s."), *ParameterName.ToString(), *ObjectPath);
				return MakeExecutionResult(Message, MakeErrorObject(Action, TEXT("ParameterTypeMismatch"), Message), true);
			}

			float PreviousValue = 0.0f;
			if (!MaterialInstance->GetScalarParameterValue(ParameterInfo, PreviousValue, false))
			{
				const FString Message = FString::Printf(TEXT("Could not read previous scalar value for parameter '%s'."), *ParameterName.ToString());
				return MakeExecutionResult(Message, MakeErrorObject(Action, TEXT("ParameterReadFailed"), Message), true);
			}

			bool bSave = false;
			Arguments.TryGetBoolField(TEXT("save"), bSave);
			const float NewValue = static_cast<float>(NewValueDouble);
			const FScopedTransaction Transaction(LOCTEXT("UnrealMcpMaterialInstanceSetScalar", "Unreal MCP Set Material Scalar"));
			MaterialInstance->Modify();
			MaterialInstance->SetScalarParameterValueEditorOnly(ParameterInfo, NewValue);
			UpdateMaterialInstanceAfterEdit(MaterialInstance);

			bool bSaved = false;
			if (bSave)
			{
				if (UEditorAssetSubsystem* EditorAssetSubsystem = GetEditorAssetSubsystem())
				{
					bSaved = EditorAssetSubsystem->SaveLoadedAsset(MaterialInstance, false);
				}
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), Action);
			StructuredContent->SetBoolField(TEXT("success"), true);
			StructuredContent->SetStringField(TEXT("materialInstancePath"), ObjectPath);
			StructuredContent->SetStringField(TEXT("parameterName"), ParameterName.ToString());
			StructuredContent->SetNumberField(TEXT("previousValue"), PreviousValue);
			StructuredContent->SetNumberField(TEXT("newValue"), NewValue);
			StructuredContent->SetBoolField(TEXT("saved"), bSaved);
			return MakeExecutionResult(FString::Printf(TEXT("Set scalar material parameter %s on %s from %g to %g."), *ParameterName.ToString(), *ObjectPath, PreviousValue, NewValue), StructuredContent, false);
		}

		FUnrealMcpExecutionResult MaterialInstanceSetVectorTool(const FJsonObject& Arguments)
		{
			const FString Action = TEXT("material_instance_set_vector");
			const FString ToolName = TEXT("unreal.material_instance_set_vector");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			FLinearColor NewValue;
			FString TypeFailureReason;
			if (!TryGetStrictJsonLinearColor(Arguments, TEXT("value"), NewValue, TypeFailureReason))
			{
				return MakeTypeMismatchResult(Action, TypeFailureReason);
			}

			FString ParameterNameString;
			if (!Arguments.TryGetStringField(TEXT("parameterName"), ParameterNameString) || ParameterNameString.TrimStartAndEnd().IsEmpty())
			{
				return MakeExecutionResult(TEXT("Missing required field 'parameterName'."), MakeErrorObject(Action, TEXT("MissingField"), TEXT("Missing required field 'parameterName'.")), true);
			}

			FString ObjectPath;
			FString FailureReason;
			FUnrealMcpExecutionResult ErrorResult;
			UMaterialInstanceConstant* MaterialInstance = LoadMaterialInstanceConstantForWrite(Arguments, Action, ObjectPath, FailureReason, ErrorResult);
			if (!MaterialInstance)
			{
				return ErrorResult;
			}

			const FName ParameterName(*ParameterNameString.TrimStartAndEnd());
			FMaterialParameterInfo ParameterInfo;
			if (!FindVectorParameterInfo(MaterialInstance, ParameterName, ParameterInfo))
			{
				const FString Message = FString::Printf(TEXT("Parameter '%s' is not a vector parameter on %s."), *ParameterName.ToString(), *ObjectPath);
				return MakeExecutionResult(Message, MakeErrorObject(Action, TEXT("ParameterTypeMismatch"), Message), true);
			}

			FLinearColor PreviousValue = FLinearColor::Black;
			if (!MaterialInstance->GetVectorParameterValue(ParameterInfo, PreviousValue, false))
			{
				const FString Message = FString::Printf(TEXT("Could not read previous vector value for parameter '%s'."), *ParameterName.ToString());
				return MakeExecutionResult(Message, MakeErrorObject(Action, TEXT("ParameterReadFailed"), Message), true);
			}

			bool bSave = false;
			Arguments.TryGetBoolField(TEXT("save"), bSave);
			const FScopedTransaction Transaction(LOCTEXT("UnrealMcpMaterialInstanceSetVector", "Unreal MCP Set Material Vector"));
			MaterialInstance->Modify();
			MaterialInstance->SetVectorParameterValueEditorOnly(ParameterInfo, NewValue);
			UpdateMaterialInstanceAfterEdit(MaterialInstance);

			bool bSaved = false;
			if (bSave)
			{
				if (UEditorAssetSubsystem* EditorAssetSubsystem = GetEditorAssetSubsystem())
				{
					bSaved = EditorAssetSubsystem->SaveLoadedAsset(MaterialInstance, false);
				}
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("action"), Action);
			StructuredContent->SetBoolField(TEXT("success"), true);
			StructuredContent->SetStringField(TEXT("materialInstancePath"), ObjectPath);
			StructuredContent->SetStringField(TEXT("parameterName"), ParameterName.ToString());
			StructuredContent->SetObjectField(TEXT("previousValue"), MakeColorObject(PreviousValue));
			StructuredContent->SetObjectField(TEXT("newValue"), MakeColorObject(NewValue));
			StructuredContent->SetBoolField(TEXT("saved"), bSaved);
			return MakeExecutionResult(FString::Printf(TEXT("Set vector material parameter %s on %s."), *ParameterName.ToString(), *ObjectPath), StructuredContent, false);
		}
	}

	bool TryGetStrictJsonNumber(const FJsonObject& Arguments, const FString& FieldName, double& OutValue, FString& OutFailureReason)
	{
		const TSharedPtr<FJsonValue> FieldValue = Arguments.TryGetField(FieldName);
		if (!FieldValue.IsValid() || FieldValue->Type != EJson::Number)
		{
			OutFailureReason = FString::Printf(TEXT("Field '%s' must be a number."), *FieldName);
			return false;
		}

		OutValue = FieldValue->AsNumber();
		return true;
	}

	bool TryGetStrictJsonLinearColor(const FJsonObject& Arguments, const FString& FieldName, FLinearColor& OutValue, FString& OutFailureReason)
	{
		const TSharedPtr<FJsonValue> FieldValue = Arguments.TryGetField(FieldName);
		if (!FieldValue.IsValid() || FieldValue->Type != EJson::Object || !FieldValue->AsObject().IsValid())
		{
			OutFailureReason = FString::Printf(TEXT("Field '%s' must be an object with numeric r, g, b, and a fields."), *FieldName);
			return false;
		}

		const TSharedPtr<FJsonObject> ColorObject = FieldValue->AsObject();
		double R = 0.0;
		double G = 0.0;
		double B = 0.0;
		double A = 0.0;
		if (!TryGetStrictJsonNumber(*ColorObject, TEXT("r"), R, OutFailureReason)
			|| !TryGetStrictJsonNumber(*ColorObject, TEXT("g"), G, OutFailureReason)
			|| !TryGetStrictJsonNumber(*ColorObject, TEXT("b"), B, OutFailureReason)
			|| !TryGetStrictJsonNumber(*ColorObject, TEXT("a"), A, OutFailureReason))
		{
			OutFailureReason = FString::Printf(TEXT("Field '%s' must be an object with numeric r, g, b, and a fields."), *FieldName);
			return false;
		}

		OutValue = FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
		return true;
	}

	bool TryExecuteMaterialInstanceTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult)
	{
		if (ToolName == TEXT("unreal.material_instance_list"))
		{
			OutResult = MaterialInstanceListTool(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.material_instance_get_parameters"))
		{
			OutResult = MaterialInstanceGetParametersTool(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.material_instance_set_scalar"))
		{
			OutResult = MaterialInstanceSetScalarTool(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.material_instance_set_vector"))
		{
			OutResult = MaterialInstanceSetVectorTool(Arguments);
			return true;
		}

		return false;
	}
}

#undef LOCTEXT_NAMESPACE
