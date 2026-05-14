// Apply to UnrealMcpToolRegistrar.cpp before RegisterAllMcpToolDescriptors.
void RegisterGeneratedFpsBootstrapDescriptor(FUnrealMcpToolRegistrar& Registrar)
{
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetObjectField(TEXT("pawnBlueprintPath"), MakeStringProperty(TEXT("Pawn or Character Blueprint asset path to configure, for example /Game/FPS/BP_FPSCharacter."), FString()));
	Properties->SetObjectField(TEXT("gameModeBlueprintPath"), MakeStringProperty(TEXT("GameMode Blueprint asset path to configure, for example /Game/FPS/BP_FPSGameMode."), FString()));
	Properties->SetObjectField(TEXT("playerStartLabel"), MakeStringProperty(TEXT("PlayerStart actor label to find or create."), TEXT("FPS_PlayerStart")));
	Properties->SetObjectField(TEXT("x"), MakeNumberProperty(TEXT("PlayerStart world X."), 0.0));
	Properties->SetObjectField(TEXT("y"), MakeNumberProperty(TEXT("PlayerStart world Y."), 0.0));
	Properties->SetObjectField(TEXT("z"), MakeNumberProperty(TEXT("PlayerStart world Z."), 190.0));
	Properties->SetObjectField(TEXT("pitch"), MakeNumberProperty(TEXT("PlayerStart pitch in degrees."), 0.0));
	Properties->SetObjectField(TEXT("yaw"), MakeNumberProperty(TEXT("PlayerStart yaw in degrees."), 0.0));
	Properties->SetObjectField(TEXT("roll"), MakeNumberProperty(TEXT("PlayerStart roll in degrees."), 0.0));
	Properties->SetObjectField(TEXT("cameraFov"), MakeNumberProperty(TEXT("Camera field of view."), 90.0));
	Properties->SetObjectField(TEXT("cameraHeight"), MakeNumberProperty(TEXT("Camera relative Z height."), 64.0));
	Properties->SetObjectField(TEXT("walkSpeed"), MakeNumberProperty(TEXT("CharacterMovement MaxWalkSpeed."), 650.0));
	Properties->SetObjectField(TEXT("acceleration"), MakeNumberProperty(TEXT("CharacterMovement MaxAcceleration."), 4096.0));
	Properties->SetObjectField(TEXT("deceleration"), MakeNumberProperty(TEXT("CharacterMovement BrakingDecelerationWalking."), 4096.0));
	Properties->SetObjectField(TEXT("capsuleRadius"), MakeNumberProperty(TEXT("Capsule collision radius."), 42.0));
	Properties->SetObjectField(TEXT("capsuleHalfHeight"), MakeNumberProperty(TEXT("Capsule collision half-height."), 96.0));
	Properties->SetObjectField(TEXT("axisPrefix"), MakeStringProperty(TEXT("Optional prefix for generated axis mapping names, such as FPS_."), FString()));
	Properties->SetObjectField(TEXT("runVerifyAfter"), MakeBoolProperty(TEXT("Run unreal.simulation.verify_input_drives_pawn after applying movement wiring."), true));
	Properties->SetObjectField(TEXT("dryRun"), MakeBoolProperty(TEXT("Preview intended changes without mutating assets, settings, or level actors."), false));
	Properties->SetObjectField(TEXT("compileSave"), MakeBoolProperty(TEXT("Compile and save the touched Blueprint assets."), true));

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("pawnBlueprintPath")));
	Required.Add(MakeShared<FJsonValueString>(TEXT("gameModeBlueprintPath")));

	TSharedPtr<FJsonObject> Schema = MakeObjectSchema();
	Schema->SetObjectField(TEXT("properties"), Properties);
	Schema->SetArrayField(TEXT("required"), Required);

	FUnrealMcpToolDescriptor Descriptor = MakeDescriptor(
		TEXT("unreal.fps.bootstrap"),
		TEXT("Bootstrap FPS Player"),
		TEXT("Complete FPS character bring-up: registers axis mappings, wires Blueprint EventGraph nodes with forward/right vector sources, configures Pawn/Camera/GameMode/PlayerStart state, compiles and saves, and can run a functional input verification."),
		TEXT("editor"),
		TEXT("UnrealMcpEditorTools.cpp"),
		EUnrealMcpToolRisk::High);
	Descriptor.bRequiresWrite = true;
	Descriptor.bRequiresBuild = false;
	Descriptor.bRequiresExternalProcess = false;
	Descriptor.bRequiresRestart = false;
	Descriptor.bRequiresProjectMemory = false;
	Descriptor.bRequiresLock = false;
	Descriptor.bDryRunSupport = true;
	Descriptor.bPreflightSupport = true;
	Descriptor.bPostcheckSupport = true;
	Descriptor.TestCoverage = EUnrealMcpToolTestCoverage::Manual;
	Descriptor.DocsPath = TEXT("Tools/UnrealMcpToolScaffoldStarters/fps_bootstrap/README.md");
	Descriptor.Reason = TEXT("Descriptor-first starter scaffold for complete FPS bring-up after the chassis-only configure_fps_settings tool was removed.");
	Registrar.Add(Descriptor, Schema);
}
