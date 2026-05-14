// Apply in UnrealMcpEditorTools.cpp before TryExecuteEditorTool.
bool TryExecuteEditorTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult);

namespace
{
	void GeneratedFpsBootstrapAddText(TArray<TSharedPtr<FJsonValue>>& Array, const FString& Text)
	{
		Array.Add(MakeShared<FJsonValueString>(Text));
	}

	TSharedPtr<FJsonObject> GeneratedFpsBootstrapStep(bool bPlanned, bool bApplied)
	{
		TSharedPtr<FJsonObject> Step = MakeShared<FJsonObject>();
		Step->SetBoolField(TEXT("planned"), bPlanned);
		Step->SetBoolField(TEXT("applied"), bApplied);
		return Step;
	}

	double GeneratedFpsBootstrapNumber(const FJsonObject& Arguments, const FString& FieldName, double DefaultValue)
	{
		double Value = DefaultValue;
		Arguments.TryGetNumberField(FieldName, Value);
		return Value;
	}

	bool GeneratedFpsBootstrapBool(const FJsonObject& Arguments, const FString& FieldName, bool bDefaultValue)
	{
		bool bValue = bDefaultValue;
		Arguments.TryGetBoolField(FieldName, bValue);
		return bValue;
	}

	UEdGraphPin* GeneratedFpsBootstrapFindPin(UEdGraphNode* Node, const FString& Name, EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction
				&& (Pin->PinName.ToString().Equals(Name, ESearchCase::IgnoreCase)
					|| Pin->GetDisplayName().ToString().Equals(Name, ESearchCase::IgnoreCase)))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	UEdGraphPin* GeneratedFpsBootstrapFindExecPin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	bool GeneratedFpsBootstrapConnect(UEdGraph* Graph, UEdGraphPin* FromPin, UEdGraphPin* ToPin, TArray<TSharedPtr<FJsonValue>>& Warnings)
	{
		if (!Graph || !Graph->GetSchema() || !FromPin || !ToPin)
		{
			GeneratedFpsBootstrapAddText(Warnings, TEXT("Skipped a Blueprint pin connection because one endpoint was missing."));
			return false;
		}
		if (FromPin->LinkedTo.Contains(ToPin))
		{
			return true;
		}
		const bool bConnected = Graph->GetSchema()->TryCreateConnection(FromPin, ToPin);
		if (!bConnected)
		{
			GeneratedFpsBootstrapAddText(Warnings, FString::Printf(TEXT("K2 schema rejected connection %s -> %s."), *FromPin->PinName.ToString(), *ToPin->PinName.ToString()));
		}
		return bConnected;
	}

	UK2Node_InputAxisEvent* GeneratedFpsBootstrapFindOrCreateAxisEvent(
		UEdGraph* Graph,
		const FName AxisName,
		const FVector2D Position,
		bool& bOutCreated)
	{
		bOutCreated = false;
		if (!Graph)
		{
			return nullptr;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_InputAxisEvent* AxisNode = Cast<UK2Node_InputAxisEvent>(Node))
			{
				if (AxisNode->InputAxisName == AxisName)
				{
					return AxisNode;
				}
			}
		}
		UK2Node_InputAxisEvent* NewNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_InputAxisEvent>(
			Graph,
			Position,
			EK2NewNodeFlags::None,
			[AxisName](UK2Node_InputAxisEvent* NewInstance)
			{
				NewInstance->InputAxisName = AxisName;
			});
		bOutCreated = NewNode != nullptr;
		return NewNode;
	}

	UK2Node_CallFunction* GeneratedFpsBootstrapCreateCallNode(
		UEdGraph* Graph,
		UFunction* Function,
		const FVector2D Position)
	{
		if (!Graph || !Function)
		{
			return nullptr;
		}
		return FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallFunction>(
			Graph,
			Position,
			EK2NewNodeFlags::None,
			[Function](UK2Node_CallFunction* NewInstance)
			{
				NewInstance->SetFromFunction(Function);
			});
	}

	bool GeneratedFpsBootstrapWireMovementAxis(
		UEdGraph* Graph,
		const FName AxisName,
		UFunction* DirectionFunction,
		const FVector2D Origin,
		TArray<TSharedPtr<FJsonValue>>& Changes,
		TArray<TSharedPtr<FJsonValue>>& Warnings)
	{
		bool bCreatedAxis = false;
		UK2Node_InputAxisEvent* AxisNode = GeneratedFpsBootstrapFindOrCreateAxisEvent(Graph, AxisName, Origin, bCreatedAxis);
		UFunction* AddMovementInput = APawn::StaticClass()->FindFunctionByName(TEXT("AddMovementInput"));
		UK2Node_CallFunction* MoveNode = GeneratedFpsBootstrapCreateCallNode(Graph, AddMovementInput, Origin + FVector2D(360.0, 0.0));
		UK2Node_CallFunction* DirectionNode = GeneratedFpsBootstrapCreateCallNode(Graph, DirectionFunction, Origin + FVector2D(40.0, 170.0));
		if (!AxisNode || !MoveNode || !DirectionNode)
		{
			GeneratedFpsBootstrapAddText(Warnings, FString::Printf(TEXT("Failed to create movement wiring nodes for axis %s."), *AxisName.ToString()));
			return false;
		}
		bool bOk = true;
		bOk &= GeneratedFpsBootstrapConnect(Graph, GeneratedFpsBootstrapFindExecPin(AxisNode, EGPD_Output), GeneratedFpsBootstrapFindExecPin(MoveNode, EGPD_Input), Warnings);
		bOk &= GeneratedFpsBootstrapConnect(Graph, GeneratedFpsBootstrapFindPin(AxisNode, TEXT("AxisValue"), EGPD_Output), GeneratedFpsBootstrapFindPin(MoveNode, TEXT("ScaleValue"), EGPD_Input), Warnings);
		bOk &= GeneratedFpsBootstrapConnect(Graph, GeneratedFpsBootstrapFindPin(DirectionNode, TEXT("ReturnValue"), EGPD_Output), GeneratedFpsBootstrapFindPin(MoveNode, TEXT("WorldDirection"), EGPD_Input), Warnings);
		GeneratedFpsBootstrapAddText(Changes, FString::Printf(TEXT("%s movement wiring %s."), *AxisName.ToString(), bCreatedAxis ? TEXT("created") : TEXT("updated")));
		return bOk;
	}

	bool GeneratedFpsBootstrapWireLookAxis(
		UEdGraph* Graph,
		const FName AxisName,
		UFunction* LookFunction,
		const FVector2D Origin,
		TArray<TSharedPtr<FJsonValue>>& Changes,
		TArray<TSharedPtr<FJsonValue>>& Warnings)
	{
		bool bCreatedAxis = false;
		UK2Node_InputAxisEvent* AxisNode = GeneratedFpsBootstrapFindOrCreateAxisEvent(Graph, AxisName, Origin, bCreatedAxis);
		UK2Node_CallFunction* LookNode = GeneratedFpsBootstrapCreateCallNode(Graph, LookFunction, Origin + FVector2D(360.0, 0.0));
		if (!AxisNode || !LookNode)
		{
			GeneratedFpsBootstrapAddText(Warnings, FString::Printf(TEXT("Failed to create look wiring nodes for axis %s."), *AxisName.ToString()));
			return false;
		}
		bool bOk = true;
		bOk &= GeneratedFpsBootstrapConnect(Graph, GeneratedFpsBootstrapFindExecPin(AxisNode, EGPD_Output), GeneratedFpsBootstrapFindExecPin(LookNode, EGPD_Input), Warnings);
		bOk &= GeneratedFpsBootstrapConnect(Graph, GeneratedFpsBootstrapFindPin(AxisNode, TEXT("AxisValue"), EGPD_Output), GeneratedFpsBootstrapFindPin(LookNode, TEXT("Val"), EGPD_Input), Warnings);
		GeneratedFpsBootstrapAddText(Changes, FString::Printf(TEXT("%s look wiring %s."), *AxisName.ToString(), bCreatedAxis ? TEXT("created") : TEXT("updated")));
		return bOk;
	}
}

FUnrealMcpExecutionResult ExecuteGeneratedFpsBootstrapTool(const FString& ToolName, const FJsonObject& Arguments)
{
	FString PawnBlueprintPath;
	FString GameModeBlueprintPath;
	if (!Arguments.TryGetStringField(TEXT("pawnBlueprintPath"), PawnBlueprintPath) || PawnBlueprintPath.TrimStartAndEnd().IsEmpty())
	{
		return MakeExecutionResult(TEXT("Missing required field 'pawnBlueprintPath'."), nullptr, true);
	}
	if (!Arguments.TryGetStringField(TEXT("gameModeBlueprintPath"), GameModeBlueprintPath) || GameModeBlueprintPath.TrimStartAndEnd().IsEmpty())
	{
		return MakeExecutionResult(TEXT("Missing required field 'gameModeBlueprintPath'."), nullptr, true);
	}

	FString PlayerStartLabel = TEXT("FPS_PlayerStart");
	FString AxisPrefix;
	Arguments.TryGetStringField(TEXT("playerStartLabel"), PlayerStartLabel);
	Arguments.TryGetStringField(TEXT("axisPrefix"), AxisPrefix);
	const bool bDryRun = GeneratedFpsBootstrapBool(Arguments, TEXT("dryRun"), false);
	const bool bCompileSave = GeneratedFpsBootstrapBool(Arguments, TEXT("compileSave"), true);
	const bool bRunVerifyAfter = GeneratedFpsBootstrapBool(Arguments, TEXT("runVerifyAfter"), true);
	const double X = GeneratedFpsBootstrapNumber(Arguments, TEXT("x"), 0.0);
	const double Y = GeneratedFpsBootstrapNumber(Arguments, TEXT("y"), 0.0);
	const double Z = GeneratedFpsBootstrapNumber(Arguments, TEXT("z"), 190.0);
	const double Pitch = GeneratedFpsBootstrapNumber(Arguments, TEXT("pitch"), 0.0);
	const double Yaw = GeneratedFpsBootstrapNumber(Arguments, TEXT("yaw"), 0.0);
	const double Roll = GeneratedFpsBootstrapNumber(Arguments, TEXT("roll"), 0.0);
	const double CameraFov = GeneratedFpsBootstrapNumber(Arguments, TEXT("cameraFov"), 90.0);
	const double CameraHeight = GeneratedFpsBootstrapNumber(Arguments, TEXT("cameraHeight"), 64.0);
	const double WalkSpeed = GeneratedFpsBootstrapNumber(Arguments, TEXT("walkSpeed"), 650.0);
	const double Acceleration = GeneratedFpsBootstrapNumber(Arguments, TEXT("acceleration"), 4096.0);
	const double Deceleration = GeneratedFpsBootstrapNumber(Arguments, TEXT("deceleration"), 4096.0);
	const double CapsuleRadius = GeneratedFpsBootstrapNumber(Arguments, TEXT("capsuleRadius"), 42.0);
	const double CapsuleHalfHeight = GeneratedFpsBootstrapNumber(Arguments, TEXT("capsuleHalfHeight"), 96.0);

	TArray<TSharedPtr<FJsonValue>> Changes;
	TArray<TSharedPtr<FJsonValue>> Warnings;
	TSharedPtr<FJsonObject> Verified = MakeShared<FJsonObject>();
	Verified->SetObjectField(TEXT("axisMappings"), GeneratedFpsBootstrapStep(true, false));
	Verified->SetObjectField(TEXT("wiringMoveForward"), GeneratedFpsBootstrapStep(true, false));
	Verified->SetObjectField(TEXT("wiringMoveRight"), GeneratedFpsBootstrapStep(true, false));
	Verified->SetObjectField(TEXT("wiringTurn"), GeneratedFpsBootstrapStep(true, false));
	Verified->SetObjectField(TEXT("wiringLookUp"), GeneratedFpsBootstrapStep(true, false));
	Verified->SetObjectField(TEXT("pawnControllerYaw"), GeneratedFpsBootstrapStep(true, false));
	Verified->SetObjectField(TEXT("cameraPawnControlRotation"), GeneratedFpsBootstrapStep(true, false));
	Verified->SetObjectField(TEXT("compileSave"), GeneratedFpsBootstrapStep(bCompileSave, false));
	Verified->SetBoolField(TEXT("runtimeVerify"), false);

	TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
	StructuredContent->SetStringField(TEXT("action"), TEXT("fps_bootstrap"));
	StructuredContent->SetStringField(TEXT("toolName"), ToolName);
	StructuredContent->SetBoolField(TEXT("dryRun"), bDryRun);
	StructuredContent->SetObjectField(TEXT("verified"), Verified);
	StructuredContent->SetArrayField(TEXT("changes"), Changes);
	StructuredContent->SetArrayField(TEXT("warnings"), Warnings);
	if (bDryRun)
	{
		GeneratedFpsBootstrapAddText(Changes, TEXT("Dry run planned axis mappings, Blueprint wiring, Pawn/Camera settings, GameMode, PlayerStart, compile/save, and runtime verification."));
		StructuredContent->SetArrayField(TEXT("changes"), Changes);
		return MakeExecutionResult(TEXT("FPS bootstrap dry run planned."), StructuredContent, false);
	}
	if (EditorToolIsEditorPlaying())
	{
		return EditorToolMakePieBlockedResult(ToolName);
	}

	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
	if (!EditorAssetSubsystem)
	{
		return MakeExecutionResult(TEXT("EditorAssetSubsystem is unavailable."), nullptr, true);
	}
	UObject* PawnAsset = EditorAssetSubsystem->LoadAsset(PawnBlueprintPath);
	UObject* GameModeAsset = EditorAssetSubsystem->LoadAsset(GameModeBlueprintPath);
	UBlueprint* PawnBlueprint = Cast<UBlueprint>(PawnAsset);
	UBlueprint* GameModeBlueprint = Cast<UBlueprint>(GameModeAsset);
	if (!PawnBlueprint || !PawnBlueprint->GeneratedClass)
	{
		return MakeExecutionResult(FString::Printf(TEXT("Could not load Pawn Blueprint '%s'."), *PawnBlueprintPath), nullptr, true);
	}
	if (!GameModeBlueprint || !GameModeBlueprint->GeneratedClass)
	{
		return MakeExecutionResult(FString::Printf(TEXT("Could not load GameMode Blueprint '%s'."), *GameModeBlueprintPath), nullptr, true);
	}

	const FName MoveForwardName(*FString::Printf(TEXT("%sMoveForward"), *AxisPrefix));
	const FName MoveRightName(*FString::Printf(TEXT("%sMoveRight"), *AxisPrefix));
	const FName TurnName(*FString::Printf(TEXT("%sTurn"), *AxisPrefix));
	const FName LookUpName(*FString::Printf(TEXT("%sLookUp"), *AxisPrefix));

	UInputSettings* InputSettings = GetMutableDefault<UInputSettings>();
	auto EnsureAxis = [InputSettings, &Changes](const FName AxisName, const FKey Key, const float Scale)
	{
		TArray<FInputAxisKeyMapping> Existing;
		InputSettings->GetAxisMappingByName(AxisName, Existing);
		const bool bAlreadyMapped = Existing.ContainsByPredicate([Key, Scale](const FInputAxisKeyMapping& Mapping)
		{
			return Mapping.Key == Key && FMath::IsNearlyEqual(Mapping.Scale, Scale);
		});
		if (!bAlreadyMapped)
		{
			InputSettings->AddAxisMapping(FInputAxisKeyMapping(AxisName, Key, Scale), false);
			GeneratedFpsBootstrapAddText(Changes, FString::Printf(TEXT("Added axis mapping %s -> %s %.2f."), *AxisName.ToString(), *Key.ToString(), Scale));
		}
	};
	EnsureAxis(MoveForwardName, EKeys::W, 1.0f);
	EnsureAxis(MoveForwardName, EKeys::S, -1.0f);
	EnsureAxis(MoveRightName, EKeys::D, 1.0f);
	EnsureAxis(MoveRightName, EKeys::A, -1.0f);
	EnsureAxis(TurnName, FKey(TEXT("Mouse X")), 1.0f);
	EnsureAxis(LookUpName, FKey(TEXT("Mouse Y")), 1.0f);
	InputSettings->SaveKeyMappings();
	InputSettings->UpdateDefaultConfigFile();
	Verified->SetObjectField(TEXT("axisMappings"), GeneratedFpsBootstrapStep(true, true));

	UObject* PawnCdoObject = PawnBlueprint->GeneratedClass->GetDefaultObject();
	APawn* PawnCdo = Cast<APawn>(PawnCdoObject);
	if (PawnCdo)
	{
		PawnCdo->bUseControllerRotationYaw = true;
		Verified->SetObjectField(TEXT("pawnControllerYaw"), GeneratedFpsBootstrapStep(true, true));
		GeneratedFpsBootstrapAddText(Changes, TEXT("Set Pawn bUseControllerRotationYaw=true."));
	}

	if (ACharacter* CharacterCdo = Cast<ACharacter>(PawnCdoObject))
	{
		if (CharacterCdo->GetCharacterMovement())
		{
			CharacterCdo->GetCharacterMovement()->MaxWalkSpeed = static_cast<float>(WalkSpeed);
			CharacterCdo->GetCharacterMovement()->MaxAcceleration = static_cast<float>(Acceleration);
			CharacterCdo->GetCharacterMovement()->BrakingDecelerationWalking = static_cast<float>(Deceleration);
			GeneratedFpsBootstrapAddText(Changes, TEXT("Configured CharacterMovement speed, acceleration, and deceleration."));
		}
		if (CharacterCdo->GetCapsuleComponent())
		{
			CharacterCdo->GetCapsuleComponent()->SetCapsuleSize(static_cast<float>(CapsuleRadius), static_cast<float>(CapsuleHalfHeight), false);
			GeneratedFpsBootstrapAddText(Changes, TEXT("Configured Character capsule size."));
		}
	}

	UCameraComponent* CameraComponent = PawnCdo ? PawnCdo->FindComponentByClass<UCameraComponent>() : nullptr;
	if (!CameraComponent && PawnBlueprint->SimpleConstructionScript)
	{
		USCS_Node* CameraNode = PawnBlueprint->SimpleConstructionScript->CreateNode(UCameraComponent::StaticClass(), TEXT("FPSCamera"));
		PawnBlueprint->SimpleConstructionScript->AddNode(CameraNode);
		CameraComponent = Cast<UCameraComponent>(CameraNode ? CameraNode->ComponentTemplate : nullptr);
		GeneratedFpsBootstrapAddText(Changes, TEXT("Created FPSCamera component."));
	}
	if (CameraComponent)
	{
		CameraComponent->bUsePawnControlRotation = true;
		CameraComponent->FieldOfView = static_cast<float>(CameraFov);
		CameraComponent->SetRelativeLocation(FVector(0.0, 0.0, CameraHeight));
		Verified->SetObjectField(TEXT("cameraPawnControlRotation"), GeneratedFpsBootstrapStep(true, true));
		GeneratedFpsBootstrapAddText(Changes, TEXT("Configured Camera bUsePawnControlRotation, FOV, and height."));
	}
	else
	{
		GeneratedFpsBootstrapAddText(Warnings, TEXT("Could not find or create a CameraComponent on the Pawn Blueprint."));
	}

	FString FailureReason;
	UEdGraph* EventGraph = ResolveBlueprintGraph(PawnBlueprint, UEdGraphSchema_K2::GN_EventGraph.ToString(), true, FailureReason);
	if (!EventGraph)
	{
		return MakeExecutionResult(FailureReason, nullptr, true);
	}
	UFunction* ForwardFunction = AActor::StaticClass()->FindFunctionByName(TEXT("GetActorForwardVector"));
	UFunction* RightFunction = AActor::StaticClass()->FindFunctionByName(TEXT("GetActorRightVector"));
	UFunction* YawFunction = APawn::StaticClass()->FindFunctionByName(TEXT("AddControllerYawInput"));
	UFunction* PitchFunction = APawn::StaticClass()->FindFunctionByName(TEXT("AddControllerPitchInput"));
	const bool bForwardWired = GeneratedFpsBootstrapWireMovementAxis(EventGraph, MoveForwardName, ForwardFunction, FVector2D(0.0, 0.0), Changes, Warnings);
	const bool bRightWired = GeneratedFpsBootstrapWireMovementAxis(EventGraph, MoveRightName, RightFunction, FVector2D(0.0, 360.0), Changes, Warnings);
	const bool bTurnWired = GeneratedFpsBootstrapWireLookAxis(EventGraph, TurnName, YawFunction, FVector2D(0.0, 720.0), Changes, Warnings);
	const bool bLookUpWired = GeneratedFpsBootstrapWireLookAxis(EventGraph, LookUpName, PitchFunction, FVector2D(0.0, 940.0), Changes, Warnings);
	Verified->SetObjectField(TEXT("wiringMoveForward"), GeneratedFpsBootstrapStep(true, bForwardWired));
	Verified->SetObjectField(TEXT("wiringMoveRight"), GeneratedFpsBootstrapStep(true, bRightWired));
	Verified->SetObjectField(TEXT("wiringTurn"), GeneratedFpsBootstrapStep(true, bTurnWired));
	Verified->SetObjectField(TEXT("wiringLookUp"), GeneratedFpsBootstrapStep(true, bLookUpWired));
	EventGraph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(PawnBlueprint);

	if (AGameModeBase* GameModeCdo = Cast<AGameModeBase>(GameModeBlueprint->GeneratedClass->GetDefaultObject()))
	{
		GameModeCdo->DefaultPawnClass = PawnBlueprint->GeneratedClass;
		GeneratedFpsBootstrapAddText(Changes, TEXT("Configured GameMode DefaultPawnClass."));
	}
	if (UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr)
	{
		EditorWorld->GetWorldSettings()->DefaultGameMode = GameModeBlueprint->GeneratedClass;
		GeneratedFpsBootstrapAddText(Changes, TEXT("Configured current level WorldSettings default GameMode."));
	}

	UEditorActorSubsystem* ActorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorActorSubsystem>() : nullptr;
	if (ActorSubsystem)
	{
		AActor* PlayerStart = nullptr;
		for (AActor* Actor : ActorSubsystem->GetAllLevelActors())
		{
			if (Actor && Actor->GetActorLabel().Equals(PlayerStartLabel, ESearchCase::CaseSensitive))
			{
				PlayerStart = Actor;
				break;
			}
		}
		if (!PlayerStart)
		{
			PlayerStart = ActorSubsystem->SpawnActorFromClass(APlayerStart::StaticClass(), FVector(X, Y, Z), FRotator(Pitch, Yaw, Roll));
			if (PlayerStart)
			{
				PlayerStart->SetActorLabel(PlayerStartLabel);
			}
		}
		if (PlayerStart)
		{
			PlayerStart->SetActorLocation(FVector(X, Y, Z), false, nullptr, ETeleportType::TeleportPhysics);
			PlayerStart->SetActorRotation(FRotator(Pitch, Yaw, Roll));
			GeneratedFpsBootstrapAddText(Changes, FString::Printf(TEXT("Configured PlayerStart '%s'."), *PlayerStartLabel));
		}
	}

	if (bCompileSave)
	{
		FKismetEditorUtilities::CompileBlueprint(PawnBlueprint);
		FKismetEditorUtilities::CompileBlueprint(GameModeBlueprint);
		EditorAssetSubsystem->SaveLoadedAsset(PawnBlueprint, false);
		EditorAssetSubsystem->SaveLoadedAsset(GameModeBlueprint, false);
		Verified->SetObjectField(TEXT("compileSave"), GeneratedFpsBootstrapStep(true, true));
		GeneratedFpsBootstrapAddText(Changes, TEXT("Compiled and saved Pawn and GameMode Blueprints."));
	}

	if (bRunVerifyAfter)
	{
		bool bRuntimeVerify = false;
		FUnrealMcpExecutionResult VerifyResult;
		TSharedPtr<FJsonObject> VerifyArgs = MakeShared<FJsonObject>();
		VerifyArgs->SetStringField(TEXT("pawnClass"), PawnBlueprint->GeneratedClass->GetPathName());
		VerifyArgs->SetStringField(TEXT("inputAxis"), MoveForwardName.ToString());
		VerifyArgs->SetNumberField(TEXT("axisValue"), 1.0);
		VerifyArgs->SetNumberField(TEXT("durationSeconds"), 0.5);
		VerifyArgs->SetNumberField(TEXT("expectedMotionDelta"), 20.0);
		VerifyArgs->SetBoolField(TEXT("startPieIfNeeded"), true);
		if (TryExecuteEditorTool(TEXT("unreal.simulation.verify_input_drives_pawn"), *VerifyArgs, VerifyResult) && VerifyResult.StructuredContent.IsValid())
		{
			bRuntimeVerify = VerifyResult.StructuredContent->GetBoolField(TEXT("pass"));
		}
		Verified->SetBoolField(TEXT("runtimeVerify"), bRuntimeVerify);
		if (!bRuntimeVerify)
		{
			GeneratedFpsBootstrapAddText(Warnings, TEXT("Runtime MoveForward verification did not pass or verifier tool is not installed."));
		}
	}

	StructuredContent->SetObjectField(TEXT("verified"), Verified);
	StructuredContent->SetArrayField(TEXT("changes"), Changes);
	StructuredContent->SetArrayField(TEXT("warnings"), Warnings);
	return MakeExecutionResult(TEXT("FPS bootstrap completed."), StructuredContent, false);
}
