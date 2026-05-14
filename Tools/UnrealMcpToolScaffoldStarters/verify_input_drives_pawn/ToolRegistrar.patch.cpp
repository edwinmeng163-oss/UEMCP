// Apply to UnrealMcpToolRegistrar.cpp before RegisterAllMcpToolDescriptors.
void RegisterGeneratedVerifyInputDrivesPawnDescriptor(FUnrealMcpToolRegistrar& Registrar)
{
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetObjectField(TEXT("pawnClass"), MakeStringProperty(TEXT("Pawn class path, Blueprint class path, Blueprint asset path, or native class path to verify."), FString()));
	Properties->SetObjectField(TEXT("inputAxis"), MakeStringProperty(TEXT("Axis name to inject. Common values: MoveForward, MoveRight, Turn, LookUp. Prefixed project axes are also accepted."), TEXT("MoveForward")));
	Properties->SetObjectField(TEXT("axisValue"), MakeNumberProperty(TEXT("Axis value to inject."), 1.0));
	Properties->SetObjectField(TEXT("durationSeconds"), MakeNumberProperty(TEXT("How long to tick PIE after input injection."), 0.5));
	Properties->SetObjectField(TEXT("expectedMotionDelta"), MakeNumberProperty(TEXT("Optional minimum movement delta in cm for MoveForward/MoveRight checks. Use <=0 to skip."), 0.0));
	Properties->SetObjectField(TEXT("expectedYawDelta"), MakeNumberProperty(TEXT("Optional minimum yaw or pitch delta in degrees for Turn/LookUp checks. Use <=0 to skip."), 0.0));
	Properties->SetObjectField(TEXT("startPieIfNeeded"), MakeBoolProperty(TEXT("Start Play In Editor if it is not already active."), true));

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("pawnClass")));
	Required.Add(MakeShared<FJsonValueString>(TEXT("inputAxis")));
	Required.Add(MakeShared<FJsonValueString>(TEXT("axisValue")));

	TSharedPtr<FJsonObject> Schema = MakeObjectSchema();
	Schema->SetObjectField(TEXT("properties"), Properties);
	Schema->SetArrayField(TEXT("required"), Required);

	FUnrealMcpToolDescriptor Descriptor = MakeDescriptor(
		TEXT("unreal.simulation.verify_input_drives_pawn"),
		TEXT("Verify Input Drives Pawn"),
		TEXT("Starts or uses PIE, injects one axis input into the possessed pawn, and reports measured movement, yaw, and pitch deltas against expected thresholds."),
		TEXT("editor"),
		TEXT("UnrealMcpEditorTools.cpp"),
		EUnrealMcpToolRisk::Medium);
	Descriptor.bRequiresWrite = true;
	Descriptor.bRequiresBuild = false;
	Descriptor.bRequiresExternalProcess = false;
	Descriptor.bRequiresRestart = false;
	Descriptor.bRequiresProjectMemory = false;
	Descriptor.bRequiresLock = false;
	Descriptor.bDryRunSupport = false;
	Descriptor.bPreflightSupport = true;
	Descriptor.bPostcheckSupport = true;
	Descriptor.TestCoverage = EUnrealMcpToolTestCoverage::Manual;
	Descriptor.DocsPath = TEXT("Tools/UnrealMcpToolScaffoldStarters/verify_input_drives_pawn/README.md");
	Descriptor.Reason = TEXT("Descriptor-first starter scaffold for functional FPS input verification before Chat reports playable movement.");
	Registrar.Add(Descriptor, Schema);
}
