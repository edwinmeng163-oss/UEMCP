#include "UnrealMcpScaffoldTools.h"

#include "UnrealMcpModule.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EditorScriptingHelpers.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompilerModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "WidgetBlueprint.h"

namespace UnrealMcp
{
	bool IsEditorPlaying();
	FUnrealMcpExecutionResult MakePieBlockedResult(const FString& ToolName);
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);
	UClass* ResolveClassPath(const FString& ClassPath, UEditorAssetSubsystem* EditorAssetSubsystem);
	UBlueprint* LoadBlueprintAsset(UEditorAssetSubsystem* EditorAssetSubsystem, const FString& BlueprintPath, FString& OutObjectPath, FString& OutFailureReason);
	UWidgetBlueprint* LoadOrCreateWidgetBlueprintAsset(UEditorAssetSubsystem* EditorAssetSubsystem, const FString& WidgetBlueprintPath, FString& OutObjectPath, bool& bOutCreated, FString& OutFailureReason);
	bool BuildShopWidgetTemplate(UWidgetBlueprint* WidgetBlueprint, const FString& TitleText, FString& OutFailureReason);
	bool BuildResultWidgetTemplate(UWidgetBlueprint* WidgetBlueprint, const FString& TitleText, FString& OutFailureReason);
	void MarkWidgetBlueprintModified(UWidgetBlueprint* WidgetBlueprint, bool bStructurallyModified);
	void AddNextStep(TArray<TSharedPtr<FJsonValue>>& NextSteps, const FString& Text);
	FUnrealMcpExecutionResult ScaffoldMcpTool(const FJsonObject& Arguments);

	FString NormalizeScaffoldRootPath(const FString& RequestedRootPath)
	{
		FString RootPath = RequestedRootPath.TrimStartAndEnd();
		if (RootPath.IsEmpty())
		{
			RootPath = TEXT("/Game/MCPDemo");
		}
		while (RootPath.EndsWith(TEXT("/")))
		{
			RootPath.LeftChopInline(1);
		}
		return RootPath;
	}

	FString MakeScaffoldPath(const FString& RootPath, const FString& RelativePath)
	{
		FString CleanRelative = RelativePath.TrimStartAndEnd();
		while (CleanRelative.StartsWith(TEXT("/")))
		{
			CleanRelative.RightChopInline(1);
		}
		return RootPath / CleanRelative;
	}

	void AddScaffoldRecord(TArray<TSharedPtr<FJsonValue>>& Array, const FString& Kind, const FString& Path, bool bCreated)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("kind"), Kind);
		Object->SetStringField(TEXT("path"), Path);
		Object->SetBoolField(TEXT("created"), bCreated);
		Array.Add(MakeShared<FJsonValueObject>(Object));
	}

	void AddScaffoldNamedRecord(TArray<TSharedPtr<FJsonValue>>& Array, const FString& Kind, const FString& OwnerPath, const FString& Name, bool bCreated)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("kind"), Kind);
		Object->SetStringField(TEXT("owner"), OwnerPath);
		Object->SetStringField(TEXT("name"), Name);
		Object->SetBoolField(TEXT("created"), bCreated);
		Array.Add(MakeShared<FJsonValueObject>(Object));
	}

	bool EnsureScaffoldDirectory(UEditorAssetSubsystem* EditorAssetSubsystem, const FString& DirectoryPath, TArray<TSharedPtr<FJsonValue>>& Directories, FString& OutFailureReason)
	{
		if (!EditorAssetSubsystem)
		{
			OutFailureReason = TEXT("EditorAssetSubsystem is unavailable.");
			return false;
		}
		if (!DirectoryPath.StartsWith(TEXT("/Game")))
		{
			OutFailureReason = FString::Printf(TEXT("Scaffold directory '%s' must be under /Game."), *DirectoryPath);
			return false;
		}

		const bool bExists = EditorAssetSubsystem->DoesDirectoryExist(DirectoryPath);
		if (!bExists && !EditorAssetSubsystem->MakeDirectory(DirectoryPath))
		{
			OutFailureReason = FString::Printf(TEXT("Failed to create directory '%s'."), *DirectoryPath);
			return false;
		}
		AddScaffoldRecord(Directories, TEXT("directory"), DirectoryPath, !bExists);
		return true;
	}

	UBlueprint* LoadOrCreateBlueprintScaffoldAsset(
		UEditorAssetSubsystem* EditorAssetSubsystem,
		const FString& AssetPath,
		const FString& ParentClassPath,
		FString& OutObjectPath,
		bool& bOutCreated,
		TArray<TSharedPtr<FJsonValue>>& Assets,
		FString& OutFailureReason)
	{
		bOutCreated = false;
		OutObjectPath.Reset();
		OutFailureReason.Reset();

		if (!EditorAssetSubsystem)
		{
			OutFailureReason = TEXT("EditorAssetSubsystem is unavailable.");
			return nullptr;
		}

		const FString ObjectPath = EditorScriptingHelpers::ConvertAnyPathToObjectPath(AssetPath, OutFailureReason);
		if (ObjectPath.IsEmpty())
		{
			return nullptr;
		}
		OutObjectPath = ObjectPath;

		if (EditorAssetSubsystem->DoesAssetExist(ObjectPath))
		{
			UBlueprint* ExistingBlueprint = LoadBlueprintAsset(EditorAssetSubsystem, ObjectPath, OutObjectPath, OutFailureReason);
			if (!ExistingBlueprint)
			{
				return nullptr;
			}
			AddScaffoldRecord(Assets, TEXT("blueprint"), OutObjectPath, false);
			return ExistingBlueprint;
		}

		UClass* ParentClass = ResolveClassPath(ParentClassPath, EditorAssetSubsystem);
		if (!ParentClass)
		{
			OutFailureReason = FString::Printf(TEXT("Unable to resolve parent class '%s'."), *ParentClassPath);
			return nullptr;
		}
		if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
		{
			OutFailureReason = FString::Printf(TEXT("Cannot create a Blueprint from class '%s'."), *ParentClass->GetPathName());
			return nullptr;
		}
		if (!EditorScriptingHelpers::IsAValidPathForCreateNewAsset(ObjectPath, OutFailureReason))
		{
			OutFailureReason = FString::Printf(TEXT("Invalid asset path '%s': %s"), *ObjectPath, *OutFailureReason);
			return nullptr;
		}

		const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
		const FName AssetName(*FPackageName::GetLongPackageAssetName(PackageName));
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			OutFailureReason = FString::Printf(TEXT("Failed to create package '%s'."), *PackageName);
			return nullptr;
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
			FName(TEXT("UnrealMcpScaffold")));

		if (!NewBlueprint)
		{
			OutFailureReason = FString::Printf(TEXT("Failed to create Blueprint '%s'."), *ObjectPath);
			return nullptr;
		}

		FAssetRegistryModule::AssetCreated(NewBlueprint);
		Package->MarkPackageDirty();
		bOutCreated = true;
		AddScaffoldRecord(Assets, TEXT("blueprint"), ObjectPath, true);
		return NewBlueprint;
	}

	FEdGraphPinType MakeScaffoldPinType(const FName& PinCategory, EPinContainerType ContainerType = EPinContainerType::None, UObject* SubCategoryObject = nullptr)
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = PinCategory;
		PinType.ContainerType = ContainerType;
		PinType.PinSubCategoryObject = SubCategoryObject;
		if (PinCategory == UEdGraphSchema_K2::PC_Real)
		{
			PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		}
		return PinType;
	}

	bool EnsureBlueprintScaffoldVariable(
		UBlueprint* Blueprint,
		const FName& VariableName,
		const FEdGraphPinType& PinType,
		const FString& DefaultValue,
		TArray<TSharedPtr<FJsonValue>>& Variables,
		FString& OutFailureReason)
	{
		if (!Blueprint)
		{
			OutFailureReason = TEXT("Blueprint is null.");
			return false;
		}
		if (FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, VariableName) != INDEX_NONE)
		{
			AddScaffoldNamedRecord(Variables, TEXT("variable"), Blueprint->GetPathName(), VariableName.ToString(), false);
			return true;
		}

		Blueprint->Modify();
		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, VariableName, PinType, DefaultValue))
		{
			OutFailureReason = FString::Printf(TEXT("Failed to add variable '%s' to %s."), *VariableName.ToString(), *Blueprint->GetPathName());
			return false;
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		Blueprint->MarkPackageDirty();
		AddScaffoldNamedRecord(Variables, TEXT("variable"), Blueprint->GetPathName(), VariableName.ToString(), true);
		return true;
	}

	bool EnsureBlueprintScaffoldFunction(UBlueprint* Blueprint, const FString& FunctionName, TArray<TSharedPtr<FJsonValue>>& Functions, FString& OutFailureReason)
	{
		if (!Blueprint)
		{
			OutFailureReason = TEXT("Blueprint is null.");
			return false;
		}

		const FString CleanFunctionName = FunctionName.TrimStartAndEnd();
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetName().Equals(CleanFunctionName, ESearchCase::IgnoreCase))
			{
				AddScaffoldNamedRecord(Functions, TEXT("function"), Blueprint->GetPathName(), Graph->GetName(), false);
				return true;
			}
		}

		Blueprint->Modify();
		UEdGraph* FunctionGraph = FBlueprintEditorUtils::CreateNewGraph(
			Blueprint,
			FName(*CleanFunctionName),
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass());
		if (!FunctionGraph)
		{
			OutFailureReason = FString::Printf(TEXT("Failed to create function graph '%s' in %s."), *CleanFunctionName, *Blueprint->GetPathName());
			return false;
		}
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, FunctionGraph, true, nullptr);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		Blueprint->MarkPackageDirty();
		AddScaffoldNamedRecord(Functions, TEXT("function"), Blueprint->GetPathName(), FunctionGraph->GetName(), true);
		return true;
	}

	void SetBlueprintClassDefault(UBlueprint* Blueprint, const FName& PropertyName, UClass* ClassValue, TArray<TSharedPtr<FJsonValue>>& Defaults)
	{
		if (!Blueprint || !Blueprint->GeneratedClass || !ClassValue)
		{
			return;
		}
		UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
		FClassProperty* ClassProperty = CDO ? FindFProperty<FClassProperty>(CDO->GetClass(), PropertyName) : nullptr;
		if (!CDO || !ClassProperty)
		{
			return;
		}

		CDO->Modify();
		CDO->PreEditChange(ClassProperty);
		ClassProperty->SetPropertyValue_InContainer(CDO, ClassValue);
		FPropertyChangedEvent PropertyChangedEvent(ClassProperty, EPropertyChangeType::ValueSet);
		CDO->PostEditChangeProperty(PropertyChangedEvent);
		Blueprint->MarkPackageDirty();

		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("blueprint"), Blueprint->GetPathName());
		Object->SetStringField(TEXT("property"), PropertyName.ToString());
		Object->SetStringField(TEXT("class"), ClassValue->GetPathName());
		Defaults.Add(MakeShared<FJsonValueObject>(Object));
	}

	void FinalizeScaffoldBlueprint(UBlueprint* Blueprint, UEditorAssetSubsystem* EditorAssetSubsystem, bool bCompile, bool bSavePackage, TArray<TSharedPtr<FJsonValue>>& Finalized)
	{
		if (!Blueprint)
		{
			return;
		}

		bool bCompileSucceeded = true;
		if (bCompile)
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			bCompileSucceeded = Blueprint->Status != BS_Error;
		}

		bool bSaved = false;
		if (bSavePackage && EditorAssetSubsystem)
		{
			bSaved = EditorAssetSubsystem->SaveLoadedAsset(Blueprint, false);
		}

		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("blueprint"), Blueprint->GetPathName());
		Object->SetBoolField(TEXT("compiled"), bCompile);
		Object->SetBoolField(TEXT("compileSucceeded"), bCompileSucceeded);
		Object->SetBoolField(TEXT("saved"), bSaved);
		Finalized.Add(MakeShared<FJsonValueObject>(Object));
	}

	TSharedPtr<FJsonObject> MakeScaffoldStructuredContent(
		const FString& Action,
		const FString& RootPath,
		const TArray<TSharedPtr<FJsonValue>>& Directories,
		const TArray<TSharedPtr<FJsonValue>>& Assets,
		const TArray<TSharedPtr<FJsonValue>>& Variables,
		const TArray<TSharedPtr<FJsonValue>>& Functions,
		const TArray<TSharedPtr<FJsonValue>>& Defaults,
		const TArray<TSharedPtr<FJsonValue>>& Finalized,
		const TArray<TSharedPtr<FJsonValue>>& NextSteps)
	{
		TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
		StructuredContent->SetStringField(TEXT("action"), Action);
		StructuredContent->SetStringField(TEXT("rootPath"), RootPath);
		StructuredContent->SetArrayField(TEXT("directories"), Directories);
		StructuredContent->SetArrayField(TEXT("assets"), Assets);
		StructuredContent->SetArrayField(TEXT("variables"), Variables);
		StructuredContent->SetArrayField(TEXT("functions"), Functions);
		StructuredContent->SetArrayField(TEXT("classDefaults"), Defaults);
		StructuredContent->SetArrayField(TEXT("finalized"), Finalized);
		StructuredContent->SetArrayField(TEXT("nextSteps"), NextSteps);
		return StructuredContent;
	}

	FUnrealMcpExecutionResult ScaffoldRoundSystem(UEditorAssetSubsystem* EditorAssetSubsystem, const FJsonObject& Arguments)
	{
		FString RootArg;
		Arguments.TryGetStringField(TEXT("rootPath"), RootArg);
		const FString RootPath = NormalizeScaffoldRootPath(RootArg);
		bool bCompile = true;
		bool bSavePackage = true;
		Arguments.TryGetBoolField(TEXT("compile"), bCompile);
		Arguments.TryGetBoolField(TEXT("savePackage"), bSavePackage);

		TArray<TSharedPtr<FJsonValue>> Directories;
		TArray<TSharedPtr<FJsonValue>> Assets;
		TArray<TSharedPtr<FJsonValue>> Variables;
		TArray<TSharedPtr<FJsonValue>> Functions;
		TArray<TSharedPtr<FJsonValue>> Defaults;
		TArray<TSharedPtr<FJsonValue>> Finalized;
		TArray<TSharedPtr<FJsonValue>> NextSteps;
		FString FailureReason;

		const FString Folders[] = {
			RootPath,
			MakeScaffoldPath(RootPath, TEXT("Blueprints")),
			MakeScaffoldPath(RootPath, TEXT("Blueprints/Core")),
			MakeScaffoldPath(RootPath, TEXT("Blueprints/Systems")),
			MakeScaffoldPath(RootPath, TEXT("Data")),
			MakeScaffoldPath(RootPath, TEXT("Maps")),
		};
		for (const FString& Folder : Folders)
		{
			if (!EnsureScaffoldDirectory(EditorAssetSubsystem, Folder, Directories, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}
		}

		auto CreateBlueprint = [&](const FString& RelativePath, const FString& ParentClassPath) -> UBlueprint*
		{
			FString ObjectPath;
			bool bCreated = false;
			return LoadOrCreateBlueprintScaffoldAsset(EditorAssetSubsystem, MakeScaffoldPath(RootPath, RelativePath), ParentClassPath, ObjectPath, bCreated, Assets, FailureReason);
		};

		UBlueprint* GameMode = CreateBlueprint(TEXT("Blueprints/Core/BP_IT_GameMode"), TEXT("/Script/Engine.GameModeBase"));
		UBlueprint* GameState = CreateBlueprint(TEXT("Blueprints/Core/BP_IT_GameState"), TEXT("/Script/Engine.GameStateBase"));
		UBlueprint* PlayerState = CreateBlueprint(TEXT("Blueprints/Core/BP_IT_PlayerState"), TEXT("/Script/Engine.PlayerState"));
		UBlueprint* PlayerController = CreateBlueprint(TEXT("Blueprints/Core/BP_IT_PlayerController"), TEXT("/Script/Engine.PlayerController"));
		UBlueprint* RoundManager = CreateBlueprint(TEXT("Blueprints/Systems/BP_IT_RoundManagerComponent"), TEXT("/Script/Engine.ActorComponent"));
		if (!GameMode || !GameState || !PlayerState || !PlayerController || !RoundManager)
		{
			return MakeExecutionResult(FailureReason, nullptr, true);
		}

		EnsureBlueprintScaffoldVariable(GameState, TEXT("CurrentRound"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("0"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(GameState, TEXT("CurrentPhase"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Name), TEXT("Initialization"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(GameState, TEXT("PreparationDuration"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Real), TEXT("45.0"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(GameState, TEXT("CombatDuration"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Real), TEXT("90.0"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(PlayerState, TEXT("PlayerHealth"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("100"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(PlayerState, TEXT("bEliminated"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Boolean), TEXT("false"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(RoundManager, TEXT("CurrentRound"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("0"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(RoundManager, TEXT("CurrentPhase"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Name), TEXT("Initialization"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(RoundManager, TEXT("RoundPairings"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Name, EPinContainerType::Array), FString(), Variables, FailureReason);

		const FString RoundFunctions[] = {
			TEXT("StartMatchFlow"),
			TEXT("AdvanceRound"),
			TEXT("BeginPreparationPhase"),
			TEXT("BeginCombatPhase"),
			TEXT("BeginResolutionPhase"),
			TEXT("PairPlayersForCombat"),
			TEXT("ApplyRoundDamage"),
			TEXT("CheckVictoryCondition"),
		};
		for (const FString& FunctionName : RoundFunctions)
		{
			if (!EnsureBlueprintScaffoldFunction(RoundManager, FunctionName, Functions, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}
		}

		TArray<UBlueprint*> Blueprints = { GameState, PlayerState, PlayerController, RoundManager, GameMode };
		for (UBlueprint* Blueprint : Blueprints)
		{
			FinalizeScaffoldBlueprint(Blueprint, EditorAssetSubsystem, bCompile, bSavePackage, Finalized);
		}

		SetBlueprintClassDefault(GameMode, TEXT("GameStateClass"), GameState->GeneratedClass, Defaults);
		SetBlueprintClassDefault(GameMode, TEXT("PlayerStateClass"), PlayerState->GeneratedClass, Defaults);
		SetBlueprintClassDefault(GameMode, TEXT("PlayerControllerClass"), PlayerController->GeneratedClass, Defaults);
		if (bSavePackage)
		{
			EditorAssetSubsystem->SaveLoadedAsset(GameMode, false);
		}

		AddNextStep(NextSteps, TEXT("Attach BP_IT_RoundManagerComponent to BP_IT_GameMode or a dedicated arena actor, then implement phase transition logic in the generated function graphs."));
		AddNextStep(NextSteps, TEXT("Set the prototype map World Settings to BP_IT_GameMode if it is not already selected."));

		TSharedPtr<FJsonObject> StructuredContent = MakeScaffoldStructuredContent(TEXT("scaffold_round_system"), RootPath, Directories, Assets, Variables, Functions, Defaults, Finalized, NextSteps);
		return MakeExecutionResult(FString::Printf(TEXT("Scaffolded round system under %s."), *RootPath), StructuredContent, false);
	}

	FUnrealMcpExecutionResult ScaffoldShopSystem(UEditorAssetSubsystem* EditorAssetSubsystem, const FJsonObject& Arguments)
	{
		FString RootArg;
		Arguments.TryGetStringField(TEXT("rootPath"), RootArg);
		const FString RootPath = NormalizeScaffoldRootPath(RootArg);
		bool bCompile = true;
		bool bSavePackage = true;
		bool bReplaceWidgetRoot = true;
		Arguments.TryGetBoolField(TEXT("compile"), bCompile);
		Arguments.TryGetBoolField(TEXT("savePackage"), bSavePackage);
		Arguments.TryGetBoolField(TEXT("replaceWidgetRoot"), bReplaceWidgetRoot);

		TArray<TSharedPtr<FJsonValue>> Directories;
		TArray<TSharedPtr<FJsonValue>> Assets;
		TArray<TSharedPtr<FJsonValue>> Variables;
		TArray<TSharedPtr<FJsonValue>> Functions;
		TArray<TSharedPtr<FJsonValue>> Defaults;
		TArray<TSharedPtr<FJsonValue>> Finalized;
		TArray<TSharedPtr<FJsonValue>> NextSteps;
		FString FailureReason;

		const FString Folders[] = {
			RootPath,
			MakeScaffoldPath(RootPath, TEXT("Blueprints/Systems")),
			MakeScaffoldPath(RootPath, TEXT("Blueprints/UI")),
			MakeScaffoldPath(RootPath, TEXT("Data")),
		};
		for (const FString& Folder : Folders)
		{
			if (!EnsureScaffoldDirectory(EditorAssetSubsystem, Folder, Directories, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}
		}

		FString ObjectPath;
		bool bCreated = false;
		UBlueprint* ShopManager = LoadOrCreateBlueprintScaffoldAsset(EditorAssetSubsystem, MakeScaffoldPath(RootPath, TEXT("Blueprints/Systems/BP_IT_ShopManagerComponent")), TEXT("/Script/Engine.ActorComponent"), ObjectPath, bCreated, Assets, FailureReason);
		if (!ShopManager)
		{
			return MakeExecutionResult(FailureReason, nullptr, true);
		}

		EnsureBlueprintScaffoldVariable(ShopManager, TEXT("ShopOfferCardIds"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Name, EPinContainerType::Array), FString(), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(ShopManager, TEXT("OwnedCardInstanceIds"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Name, EPinContainerType::Array), FString(), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(ShopManager, TEXT("RefreshCostFood"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("1"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(ShopManager, TEXT("CardCostFood"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("3"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(ShopManager, TEXT("ShopSize"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("5"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(ShopManager, TEXT("PityRefreshLimit"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("13"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(ShopManager, TEXT("RefreshesWithoutDuplicate"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("0"), Variables, FailureReason);

		const FString ShopFunctions[] = {
			TEXT("RefreshShop"),
			TEXT("GenerateShopOffers"),
			TEXT("BuyShopOffer"),
			TEXT("SellOwnedCard"),
			TEXT("MoveCardBetweenZones"),
			TEXT("CheckTripleCandidates"),
			TEXT("ApplyPityOffer"),
			TEXT("ClearShopOffers"),
		};
		for (const FString& FunctionName : ShopFunctions)
		{
			if (!EnsureBlueprintScaffoldFunction(ShopManager, FunctionName, Functions, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}
		}
		FinalizeScaffoldBlueprint(ShopManager, EditorAssetSubsystem, bCompile, bSavePackage, Finalized);

		FString WidgetObjectPath;
		bool bWidgetCreated = false;
		UWidgetBlueprint* ShopWidget = LoadOrCreateWidgetBlueprintAsset(EditorAssetSubsystem, MakeScaffoldPath(RootPath, TEXT("Blueprints/UI/WBP_IT_ShopPanel")), WidgetObjectPath, bWidgetCreated, FailureReason);
		if (!ShopWidget)
		{
			return MakeExecutionResult(FailureReason, nullptr, true);
		}
		AddScaffoldRecord(Assets, TEXT("widget_blueprint"), WidgetObjectPath, bWidgetCreated);
		if (bReplaceWidgetRoot || !ShopWidget->WidgetTree || !ShopWidget->WidgetTree->RootWidget)
		{
			if (!BuildShopWidgetTemplate(ShopWidget, TEXT("Demo Shop"), FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}
			MarkWidgetBlueprintModified(ShopWidget, true);
		}
		FinalizeScaffoldBlueprint(ShopWidget, EditorAssetSubsystem, bCompile, bSavePackage, Finalized);

		AddNextStep(NextSteps, TEXT("Connect RefreshButton.OnClicked to BP_IT_ShopManagerComponent.RefreshShop with unreal.widget_bind_event or manual Blueprint wiring."));
		AddNextStep(NextSteps, TEXT("Replace Name arrays with FCardInstanceData/FCardData structs once the C++ or Blueprint struct layer exists."));

		TSharedPtr<FJsonObject> StructuredContent = MakeScaffoldStructuredContent(TEXT("scaffold_shop_system"), RootPath, Directories, Assets, Variables, Functions, Defaults, Finalized, NextSteps);
		return MakeExecutionResult(FString::Printf(TEXT("Scaffolded shop system under %s."), *RootPath), StructuredContent, false);
	}

	FUnrealMcpExecutionResult ScaffoldEconomySystem(UEditorAssetSubsystem* EditorAssetSubsystem, const FJsonObject& Arguments)
	{
		FString RootArg;
		Arguments.TryGetStringField(TEXT("rootPath"), RootArg);
		const FString RootPath = NormalizeScaffoldRootPath(RootArg);
		bool bCompile = true;
		bool bSavePackage = true;
		Arguments.TryGetBoolField(TEXT("compile"), bCompile);
		Arguments.TryGetBoolField(TEXT("savePackage"), bSavePackage);

		TArray<TSharedPtr<FJsonValue>> Directories;
		TArray<TSharedPtr<FJsonValue>> Assets;
		TArray<TSharedPtr<FJsonValue>> Variables;
		TArray<TSharedPtr<FJsonValue>> Functions;
		TArray<TSharedPtr<FJsonValue>> Defaults;
		TArray<TSharedPtr<FJsonValue>> Finalized;
		TArray<TSharedPtr<FJsonValue>> NextSteps;
		FString FailureReason;

		const FString Folders[] = {
			RootPath,
			MakeScaffoldPath(RootPath, TEXT("Blueprints/Core")),
			MakeScaffoldPath(RootPath, TEXT("Blueprints/Systems")),
		};
		for (const FString& Folder : Folders)
		{
			if (!EnsureScaffoldDirectory(EditorAssetSubsystem, Folder, Directories, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}
		}

		FString ObjectPath;
		bool bCreated = false;
		UBlueprint* EconomyComponent = LoadOrCreateBlueprintScaffoldAsset(EditorAssetSubsystem, MakeScaffoldPath(RootPath, TEXT("Blueprints/Systems/BP_IT_EconomyComponent")), TEXT("/Script/Engine.ActorComponent"), ObjectPath, bCreated, Assets, FailureReason);
		UBlueprint* PlayerState = LoadOrCreateBlueprintScaffoldAsset(EditorAssetSubsystem, MakeScaffoldPath(RootPath, TEXT("Blueprints/Core/BP_IT_PlayerState")), TEXT("/Script/Engine.PlayerState"), ObjectPath, bCreated, Assets, FailureReason);
		if (!EconomyComponent || !PlayerState)
		{
			return MakeExecutionResult(FailureReason, nullptr, true);
		}

		UBlueprint* EconomyTargets[] = { EconomyComponent, PlayerState };
		for (UBlueprint* Target : EconomyTargets)
		{
			EnsureBlueprintScaffoldVariable(Target, TEXT("CurrentFood"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("3"), Variables, FailureReason);
			EnsureBlueprintScaffoldVariable(Target, TEXT("MaxFood"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("10"), Variables, FailureReason);
			EnsureBlueprintScaffoldVariable(Target, TEXT("CurrentGold"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("1"), Variables, FailureReason);
			EnsureBlueprintScaffoldVariable(Target, TEXT("HubLevel"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("1"), Variables, FailureReason);
			EnsureBlueprintScaffoldVariable(Target, TEXT("WinStreak"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("0"), Variables, FailureReason);
			EnsureBlueprintScaffoldVariable(Target, TEXT("LossStreak"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("0"), Variables, FailureReason);
		}

		const FString EconomyFunctions[] = {
			TEXT("InitializeEconomy"),
			TEXT("ApplyRoundIncome"),
			TEXT("ResetRoundFood"),
			TEXT("SpendFood"),
			TEXT("SpendGold"),
			TEXT("AddFood"),
			TEXT("AddGold"),
			TEXT("UpgradeHub"),
			TEXT("ApplyStreakBonus"),
		};
		for (const FString& FunctionName : EconomyFunctions)
		{
			if (!EnsureBlueprintScaffoldFunction(EconomyComponent, FunctionName, Functions, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}
		}

		FinalizeScaffoldBlueprint(EconomyComponent, EditorAssetSubsystem, bCompile, bSavePackage, Finalized);
		FinalizeScaffoldBlueprint(PlayerState, EditorAssetSubsystem, bCompile, bSavePackage, Finalized);

		AddNextStep(NextSteps, TEXT("Attach BP_IT_EconomyComponent to PlayerController or PlayerState once ownership is decided."));
		AddNextStep(NextSteps, TEXT("Move server validation into PlayerController RPCs after the resource functions have Blueprint bodies."));

		TSharedPtr<FJsonObject> StructuredContent = MakeScaffoldStructuredContent(TEXT("scaffold_economy_system"), RootPath, Directories, Assets, Variables, Functions, Defaults, Finalized, NextSteps);
		return MakeExecutionResult(FString::Printf(TEXT("Scaffolded economy system under %s."), *RootPath), StructuredContent, false);
	}

	FUnrealMcpExecutionResult ScaffoldAutobattlerAi(UEditorAssetSubsystem* EditorAssetSubsystem, const FJsonObject& Arguments)
	{
		FString RootArg;
		Arguments.TryGetStringField(TEXT("rootPath"), RootArg);
		const FString RootPath = NormalizeScaffoldRootPath(RootArg);
		bool bCompile = true;
		bool bSavePackage = true;
		Arguments.TryGetBoolField(TEXT("compile"), bCompile);
		Arguments.TryGetBoolField(TEXT("savePackage"), bSavePackage);

		TArray<TSharedPtr<FJsonValue>> Directories;
		TArray<TSharedPtr<FJsonValue>> Assets;
		TArray<TSharedPtr<FJsonValue>> Variables;
		TArray<TSharedPtr<FJsonValue>> Functions;
		TArray<TSharedPtr<FJsonValue>> Defaults;
		TArray<TSharedPtr<FJsonValue>> Finalized;
		TArray<TSharedPtr<FJsonValue>> NextSteps;
		FString FailureReason;

		const FString Folders[] = {
			RootPath,
			MakeScaffoldPath(RootPath, TEXT("Blueprints/Units")),
			MakeScaffoldPath(RootPath, TEXT("Blueprints/AI")),
			MakeScaffoldPath(RootPath, TEXT("Blueprints/Combat")),
		};
		for (const FString& Folder : Folders)
		{
			if (!EnsureScaffoldDirectory(EditorAssetSubsystem, Folder, Directories, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}
		}

		FString ObjectPath;
		bool bCreated = false;
		UBlueprint* UnitBase = LoadOrCreateBlueprintScaffoldAsset(EditorAssetSubsystem, MakeScaffoldPath(RootPath, TEXT("Blueprints/Units/BP_IT_UnitBase")), TEXT("/Script/Engine.Character"), ObjectPath, bCreated, Assets, FailureReason);
		UBlueprint* AIController = LoadOrCreateBlueprintScaffoldAsset(EditorAssetSubsystem, MakeScaffoldPath(RootPath, TEXT("Blueprints/AI/BP_IT_AutoBattleAIController")), TEXT("/Script/AIModule.AIController"), ObjectPath, bCreated, Assets, FailureReason);
		UBlueprint* CombatManager = LoadOrCreateBlueprintScaffoldAsset(EditorAssetSubsystem, MakeScaffoldPath(RootPath, TEXT("Blueprints/Combat/BP_IT_CombatManager")), TEXT("/Script/Engine.Actor"), ObjectPath, bCreated, Assets, FailureReason);
		if (!UnitBase || !AIController || !CombatManager)
		{
			return MakeExecutionResult(FailureReason, nullptr, true);
		}

		EnsureBlueprintScaffoldVariable(UnitBase, TEXT("TeamId"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("0"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(UnitBase, TEXT("UnitHealth"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Real), TEXT("100.0"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(UnitBase, TEXT("UnitDamage"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Real), TEXT("10.0"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(UnitBase, TEXT("AttackRange"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Real), TEXT("160.0"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(UnitBase, TEXT("AttackCooldown"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Real), TEXT("1.0"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(UnitBase, TEXT("SourceCardInstanceId"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Name), FString(), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(CombatManager, TEXT("TeamAUnitIds"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Name, EPinContainerType::Array), FString(), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(CombatManager, TEXT("TeamBUnitIds"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Name, EPinContainerType::Array), FString(), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(CombatManager, TEXT("bCombatRunning"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Boolean), TEXT("false"), Variables, FailureReason);

		const FString UnitFunctions[] = {
			TEXT("FindNearestEnemy"),
			TEXT("MoveToEnemy"),
			TEXT("PerformAttack"),
			TEXT("ApplyDamage"),
			TEXT("HandleDeath"),
		};
		for (const FString& FunctionName : UnitFunctions)
		{
			if (!EnsureBlueprintScaffoldFunction(UnitBase, FunctionName, Functions, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}
		}

		const FString CombatFunctions[] = {
			TEXT("SpawnTeamsFromFieldCards"),
			TEXT("StartCombat"),
			TEXT("TickCombatState"),
			TEXT("ResolveCombat"),
			TEXT("CalculateSurvivorDamage"),
			TEXT("CleanupCombatUnits"),
		};
		for (const FString& FunctionName : CombatFunctions)
		{
			if (!EnsureBlueprintScaffoldFunction(CombatManager, FunctionName, Functions, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}
		}

		FinalizeScaffoldBlueprint(AIController, EditorAssetSubsystem, bCompile, bSavePackage, Finalized);
		FinalizeScaffoldBlueprint(UnitBase, EditorAssetSubsystem, bCompile, bSavePackage, Finalized);
		FinalizeScaffoldBlueprint(CombatManager, EditorAssetSubsystem, bCompile, bSavePackage, Finalized);
		SetBlueprintClassDefault(UnitBase, TEXT("AIControllerClass"), AIController->GeneratedClass, Defaults);
		if (bSavePackage)
		{
			EditorAssetSubsystem->SaveLoadedAsset(UnitBase, false);
		}

		AddNextStep(NextSteps, TEXT("Implement FindNearestEnemy with TeamId filtering, then wire MoveToEnemy and PerformAttack from Tick or a Behavior Tree."));
		AddNextStep(NextSteps, TEXT("Keep spawned unit counts small until combat rules are proven; move to pooling or Mass later if needed."));

		TSharedPtr<FJsonObject> StructuredContent = MakeScaffoldStructuredContent(TEXT("scaffold_autobattler_ai"), RootPath, Directories, Assets, Variables, Functions, Defaults, Finalized, NextSteps);
		return MakeExecutionResult(FString::Printf(TEXT("Scaffolded autobattler AI under %s."), *RootPath), StructuredContent, false);
	}

	FUnrealMcpExecutionResult ScaffoldResultUi(UEditorAssetSubsystem* EditorAssetSubsystem, const FJsonObject& Arguments)
	{
		FString RootArg;
		Arguments.TryGetStringField(TEXT("rootPath"), RootArg);
		const FString RootPath = NormalizeScaffoldRootPath(RootArg);
		bool bCompile = true;
		bool bSavePackage = true;
		bool bReplaceWidgetRoot = true;
		Arguments.TryGetBoolField(TEXT("compile"), bCompile);
		Arguments.TryGetBoolField(TEXT("savePackage"), bSavePackage);
		Arguments.TryGetBoolField(TEXT("replaceWidgetRoot"), bReplaceWidgetRoot);

		TArray<TSharedPtr<FJsonValue>> Directories;
		TArray<TSharedPtr<FJsonValue>> Assets;
		TArray<TSharedPtr<FJsonValue>> Variables;
		TArray<TSharedPtr<FJsonValue>> Functions;
		TArray<TSharedPtr<FJsonValue>> Defaults;
		TArray<TSharedPtr<FJsonValue>> Finalized;
		TArray<TSharedPtr<FJsonValue>> NextSteps;
		FString FailureReason;

		const FString Folders[] = {
			RootPath,
			MakeScaffoldPath(RootPath, TEXT("Blueprints/UI")),
			MakeScaffoldPath(RootPath, TEXT("Blueprints/Systems")),
		};
		for (const FString& Folder : Folders)
		{
			if (!EnsureScaffoldDirectory(EditorAssetSubsystem, Folder, Directories, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}
		}

		FString ObjectPath;
		bool bCreated = false;
		UBlueprint* ResultPresenter = LoadOrCreateBlueprintScaffoldAsset(EditorAssetSubsystem, MakeScaffoldPath(RootPath, TEXT("Blueprints/Systems/BP_IT_ResultPresenterComponent")), TEXT("/Script/Engine.ActorComponent"), ObjectPath, bCreated, Assets, FailureReason);
		if (!ResultPresenter)
		{
			return MakeExecutionResult(FailureReason, nullptr, true);
		}

		EnsureBlueprintScaffoldVariable(ResultPresenter, TEXT("LastOutcome"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Name), FString(), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(ResultPresenter, TEXT("LastDamage"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Int), TEXT("0"), Variables, FailureReason);
		EnsureBlueprintScaffoldVariable(ResultPresenter, TEXT("bResultVisible"), MakeScaffoldPinType(UEdGraphSchema_K2::PC_Boolean), TEXT("false"), Variables, FailureReason);

		const FString ResultFunctions[] = {
			TEXT("ShowCombatResult"),
			TEXT("HideCombatResult"),
			TEXT("SetResultSummary"),
			TEXT("BindContinueButton"),
			TEXT("ContinueToPreparation"),
		};
		for (const FString& FunctionName : ResultFunctions)
		{
			if (!EnsureBlueprintScaffoldFunction(ResultPresenter, FunctionName, Functions, FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}
		}
		FinalizeScaffoldBlueprint(ResultPresenter, EditorAssetSubsystem, bCompile, bSavePackage, Finalized);

		FString WidgetObjectPath;
		bool bWidgetCreated = false;
		UWidgetBlueprint* ResultWidget = LoadOrCreateWidgetBlueprintAsset(EditorAssetSubsystem, MakeScaffoldPath(RootPath, TEXT("Blueprints/UI/WBP_IT_ResultPanel")), WidgetObjectPath, bWidgetCreated, FailureReason);
		if (!ResultWidget)
		{
			return MakeExecutionResult(FailureReason, nullptr, true);
		}
		AddScaffoldRecord(Assets, TEXT("widget_blueprint"), WidgetObjectPath, bWidgetCreated);
		if (bReplaceWidgetRoot || !ResultWidget->WidgetTree || !ResultWidget->WidgetTree->RootWidget)
		{
			if (!BuildResultWidgetTemplate(ResultWidget, TEXT("Combat Result"), FailureReason))
			{
				return MakeExecutionResult(FailureReason, nullptr, true);
			}
			MarkWidgetBlueprintModified(ResultWidget, true);
		}
		FinalizeScaffoldBlueprint(ResultWidget, EditorAssetSubsystem, bCompile, bSavePackage, Finalized);

		AddNextStep(NextSteps, TEXT("Bind ContinueButton.OnClicked to ContinueToPreparation, then have RoundManager call ShowCombatResult after ResolveCombat."));
		AddNextStep(NextSteps, TEXT("Feed OutcomeText and DamageText from combat resolution data once the result struct exists."));

		TSharedPtr<FJsonObject> StructuredContent = MakeScaffoldStructuredContent(TEXT("scaffold_result_ui"), RootPath, Directories, Assets, Variables, Functions, Defaults, Finalized, NextSteps);
		return MakeExecutionResult(FString::Printf(TEXT("Scaffolded result UI under %s."), *RootPath), StructuredContent, false);
	}

	namespace
	{
		UEditorAssetSubsystem* GetScaffoldEditorAssetSubsystem()
		{
			return GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
		}

		bool IsGameplayScaffoldTool(const FString& ToolName)
		{
			return ToolName == TEXT("unreal.scaffold_round_system")
				|| ToolName == TEXT("unreal.scaffold_shop_system")
				|| ToolName == TEXT("unreal.scaffold_economy_system")
				|| ToolName == TEXT("unreal.scaffold_autobattler_ai")
				|| ToolName == TEXT("unreal.scaffold_result_ui");
		}

		FUnrealMcpExecutionResult ExecuteGameplayScaffoldTool(const FString& ToolName, const FJsonObject& Arguments)
		{
			if (IsEditorPlaying())
			{
				return MakePieBlockedResult(ToolName);
			}

			UEditorAssetSubsystem* EditorAssetSubsystem = GetScaffoldEditorAssetSubsystem();
			if (!EditorAssetSubsystem)
			{
				return MakeExecutionResult(TEXT("EditorAssetSubsystem is unavailable."), nullptr, true);
			}

			if (ToolName == TEXT("unreal.scaffold_round_system"))
			{
				return ScaffoldRoundSystem(EditorAssetSubsystem, Arguments);
			}

			if (ToolName == TEXT("unreal.scaffold_shop_system"))
			{
				return ScaffoldShopSystem(EditorAssetSubsystem, Arguments);
			}

			if (ToolName == TEXT("unreal.scaffold_economy_system"))
			{
				return ScaffoldEconomySystem(EditorAssetSubsystem, Arguments);
			}

			if (ToolName == TEXT("unreal.scaffold_autobattler_ai"))
			{
				return ScaffoldAutobattlerAi(EditorAssetSubsystem, Arguments);
			}

			return ScaffoldResultUi(EditorAssetSubsystem, Arguments);
		}
	}

	bool TryExecuteScaffoldTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult)
	{
		if (IsGameplayScaffoldTool(ToolName))
		{
			OutResult = ExecuteGameplayScaffoldTool(ToolName, Arguments);
			return true;
		}

		if (ToolName == TEXT("unreal.scaffold_mcp_tool"))
		{
			if (IsEditorPlaying())
			{
				OutResult = MakePieBlockedResult(ToolName);
				return true;
			}

			OutResult = ScaffoldMcpTool(Arguments);
			return true;
		}

		return false;
	}
}
