#include "UnrealMcpBlueprintTools.h"

#include "UnrealMcpModule.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorScriptingHelpers.h"
#include "Engine/Blueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompilerModule.h"
#include "Misc/PackageName.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "UObject/Package.h"

namespace UnrealMcp
{
	int32 GetPositiveIntArgument(const FJsonObject& Arguments, const FString& FieldName, int32 DefaultValue);
	bool IsEditorPlaying();
	FUnrealMcpExecutionResult MakePieBlockedResult(const FString& ToolName);
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);
	UObject* LoadAssetFromAnyPath(UEditorAssetSubsystem* EditorAssetSubsystem, const FString& AnyAssetPath, FString& OutObjectPath, FString& OutFailureReason);
	UClass* ResolveClassPath(const FString& ClassPath, UEditorAssetSubsystem* EditorAssetSubsystem);

	namespace
	{
		FUnrealMcpExecutionResult CompileBlueprintTool(const FJsonObject& Arguments)
		{
			const FString ToolName = TEXT("unreal.compile_blueprint");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			UEditorAssetSubsystem* EditorAssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
			if (!EditorAssetSubsystem)
			{
				return MakeExecutionResult(TEXT("EditorAssetSubsystem is unavailable."), nullptr, true);
			}

			FString AssetPath;
			if (!Arguments.TryGetStringField(TEXT("path"), AssetPath) || AssetPath.IsEmpty())
			{
				return MakeExecutionResult(TEXT("The path argument is required."), nullptr, true);
			}

			FString ObjectPath;
			FString FailureReason;
			UObject* LoadedAsset = LoadAssetFromAnyPath(EditorAssetSubsystem, AssetPath, ObjectPath, FailureReason);
			UBlueprint* Blueprint = Cast<UBlueprint>(LoadedAsset);
			if (!Blueprint)
			{
				return MakeExecutionResult(
					LoadedAsset
						? FString::Printf(TEXT("Asset '%s' is not a Blueprint."), *ObjectPath)
						: FailureReason,
					nullptr,
					true);
			}

			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			const bool bSucceeded = Blueprint->Status != BS_Error;

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("objectPath"), ObjectPath);
			StructuredContent->SetStringField(TEXT("status"), StaticEnum<EBlueprintStatus>()->GetNameStringByValue(static_cast<int64>(Blueprint->Status)));
			return MakeExecutionResult(
				FString::Printf(TEXT("Compiled Blueprint %s. status=%s"), *ObjectPath, bSucceeded ? TEXT("success") : TEXT("error")),
				StructuredContent,
				!bSucceeded);
		}

		FUnrealMcpExecutionResult CompileBlueprintsInPathTool(const FJsonObject& Arguments)
		{
			const FString ToolName = TEXT("unreal.compile_blueprints_in_path");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			UEditorAssetSubsystem* EditorAssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
			if (!EditorAssetSubsystem)
			{
				return MakeExecutionResult(TEXT("EditorAssetSubsystem is unavailable."), nullptr, true);
			}

			FString Path = TEXT("/Game");
			bool bRecursive = true;
			Arguments.TryGetStringField(TEXT("path"), Path);
			Arguments.TryGetBoolField(TEXT("recursive"), bRecursive);
			const int32 Limit = FMath::Min(GetPositiveIntArgument(Arguments, TEXT("limit"), 100), 500);

			if (Path.IsEmpty() || !Path.StartsWith(TEXT("/")))
			{
				return MakeExecutionResult(TEXT("The path argument must be a Content Browser path like /Game."), nullptr, true);
			}

			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

			FARFilter Filter;
			Filter.PackagePaths.Add(*Path);
			Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
			Filter.bRecursivePaths = bRecursive;
			Filter.bRecursiveClasses = true;

			TArray<FAssetData> AssetData;
			AssetRegistryModule.Get().GetAssets(Filter, AssetData);
			AssetData.Sort([](const FAssetData& A, const FAssetData& B)
			{
				return A.GetSoftObjectPath().ToString() < B.GetSoftObjectPath().ToString();
			});

			int32 CompiledCount = 0;
			int32 SuccessCount = 0;
			int32 FailureCount = 0;
			bool bTruncated = false;
			TArray<TSharedPtr<FJsonValue>> ResultsArray;
			TArray<FString> FailureLines;

			for (const FAssetData& Asset : AssetData)
			{
				if (CompiledCount >= Limit)
				{
					bTruncated = true;
					break;
				}

				UObject* LoadedAsset = EditorAssetSubsystem->LoadAsset(Asset.GetSoftObjectPath().ToString());
				UBlueprint* Blueprint = Cast<UBlueprint>(LoadedAsset);
				if (!Blueprint)
				{
					continue;
				}

				FKismetEditorUtilities::CompileBlueprint(Blueprint);

				++CompiledCount;
				const bool bSucceeded = Blueprint->Status != BS_Error;
				if (bSucceeded)
				{
					++SuccessCount;
				}
				else
				{
					++FailureCount;
					FailureLines.Add(Asset.GetSoftObjectPath().ToString());
				}

				TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
				ResultObject->SetStringField(TEXT("objectPath"), Asset.GetSoftObjectPath().ToString());
				ResultObject->SetStringField(TEXT("status"), StaticEnum<EBlueprintStatus>()->GetNameStringByValue(static_cast<int64>(Blueprint->Status)));
				ResultObject->SetBoolField(TEXT("success"), bSucceeded);
				ResultsArray.Add(MakeShared<FJsonValueObject>(ResultObject));
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("path"), Path);
			StructuredContent->SetBoolField(TEXT("recursive"), bRecursive);
			StructuredContent->SetNumberField(TEXT("limit"), Limit);
			StructuredContent->SetNumberField(TEXT("totalBlueprintAssets"), AssetData.Num());
			StructuredContent->SetNumberField(TEXT("compiledCount"), CompiledCount);
			StructuredContent->SetNumberField(TEXT("successCount"), SuccessCount);
			StructuredContent->SetNumberField(TEXT("failureCount"), FailureCount);
			StructuredContent->SetBoolField(TEXT("truncated"), bTruncated);
			StructuredContent->SetArrayField(TEXT("results"), ResultsArray);

			FString Text = FString::Printf(
				TEXT("Compiled %d Blueprint assets under %s. success=%d failure=%d"),
				CompiledCount,
				*Path,
				SuccessCount,
				FailureCount);
			if (bTruncated)
			{
				Text += FString::Printf(TEXT(" (stopped at limit %d)"), Limit);
			}
			if (FailureLines.Num() > 0)
			{
				Text += TEXT("\nFailed:\n") + FString::Join(FailureLines, TEXT("\n"));
			}

			return MakeExecutionResult(Text, StructuredContent, FailureCount > 0);
		}

		FUnrealMcpExecutionResult CreateBlueprintClassTool(const FJsonObject& Arguments)
		{
			const FString ToolName = TEXT("unreal.create_blueprint_class");
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			UEditorAssetSubsystem* EditorAssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
			if (!EditorAssetSubsystem)
			{
				return MakeExecutionResult(TEXT("EditorAssetSubsystem is unavailable."), nullptr, true);
			}

			FString AssetPath;
			if (!Arguments.TryGetStringField(TEXT("assetPath"), AssetPath) || AssetPath.IsEmpty())
			{
				return MakeExecutionResult(TEXT("The assetPath argument is required."), nullptr, true);
			}

			FString ParentClassPath = TEXT("/Script/Engine.Actor");
			bool bOpenAfterCreate = true;
			bool bCompile = true;
			Arguments.TryGetStringField(TEXT("parentClass"), ParentClassPath);
			Arguments.TryGetBoolField(TEXT("openAfterCreate"), bOpenAfterCreate);
			Arguments.TryGetBoolField(TEXT("compile"), bCompile);

			UClass* ParentClass = ResolveClassPath(ParentClassPath, EditorAssetSubsystem);
			if (!ParentClass)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Unable to resolve parent class '%s'."), *ParentClassPath), nullptr, true);
			}

			if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
			{
				return MakeExecutionResult(FString::Printf(TEXT("Cannot create a Blueprint from class '%s'."), *ParentClass->GetPathName()), nullptr, true);
			}

			FString FailureReason;
			const FString ObjectPath = EditorScriptingHelpers::ConvertAnyPathToObjectPath(AssetPath, FailureReason);
			if (ObjectPath.IsEmpty())
			{
				return MakeExecutionResult(FString::Printf(TEXT("Unable to resolve asset path: %s"), *FailureReason), nullptr, true);
			}

			if (!EditorScriptingHelpers::IsAValidPathForCreateNewAsset(ObjectPath, FailureReason))
			{
				return MakeExecutionResult(FString::Printf(TEXT("Invalid asset path '%s': %s"), *ObjectPath, *FailureReason), nullptr, true);
			}

			if (EditorAssetSubsystem->DoesAssetExist(ObjectPath))
			{
				return MakeExecutionResult(FString::Printf(TEXT("Asset '%s' already exists."), *ObjectPath), nullptr, true);
			}

			const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
			const FName AssetName(*FPackageName::GetLongPackageAssetName(PackageName));
			UPackage* Package = CreatePackage(*PackageName);
			if (!Package)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Failed to create package '%s'."), *PackageName), nullptr, true);
			}

			UClass* BlueprintClass = nullptr;
			UClass* BlueprintGeneratedClass = nullptr;
			IKismetCompilerInterface& KismetCompilerModule = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>(KISMET_COMPILER_MODULENAME);
			KismetCompilerModule.GetBlueprintTypesForClass(ParentClass, BlueprintClass, BlueprintGeneratedClass);

			UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(
				ParentClass,
				Package,
				AssetName,
				BPTYPE_Normal,
				BlueprintClass,
				BlueprintGeneratedClass,
				FName(TEXT("UnrealMcp")));

			if (!NewBlueprint)
			{
				return MakeExecutionResult(FString::Printf(TEXT("Failed to create Blueprint '%s'."), *ObjectPath), nullptr, true);
			}

			FAssetRegistryModule::AssetCreated(NewBlueprint);
			Package->MarkPackageDirty();

			if (bCompile)
			{
				FKismetEditorUtilities::CompileBlueprint(NewBlueprint);
			}

			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
			if (bOpenAfterCreate && AssetEditorSubsystem)
			{
				AssetEditorSubsystem->OpenEditorForAsset(NewBlueprint);
			}

			TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
			StructuredContent->SetStringField(TEXT("objectPath"), ObjectPath);
			StructuredContent->SetStringField(TEXT("packageName"), PackageName);
			StructuredContent->SetStringField(TEXT("parentClass"), ParentClass->GetPathName());
			StructuredContent->SetBoolField(TEXT("compiled"), bCompile);
			StructuredContent->SetBoolField(TEXT("opened"), bOpenAfterCreate);
			if (NewBlueprint->GeneratedClass)
			{
				StructuredContent->SetStringField(TEXT("generatedClass"), NewBlueprint->GeneratedClass->GetPathName());
			}

			return MakeExecutionResult(
				FString::Printf(TEXT("Created Blueprint %s with parent %s."), *ObjectPath, *ParentClass->GetPathName()),
				StructuredContent,
				false);
		}
	}

	bool TryExecuteBlueprintTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult)
	{
		if (ToolName == TEXT("unreal.compile_blueprint"))
		{
			OutResult = CompileBlueprintTool(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.compile_blueprints_in_path"))
		{
			OutResult = CompileBlueprintsInPathTool(Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.create_blueprint_class"))
		{
			OutResult = CreateBlueprintClassTool(Arguments);
			return true;
		}

		return false;
	}
}
