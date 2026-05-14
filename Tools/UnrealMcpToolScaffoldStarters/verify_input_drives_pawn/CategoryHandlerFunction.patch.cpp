// Apply in UnrealMcpEditorTools.cpp before TryExecuteEditorTool.
bool TryExecuteEditorTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult);
UClass* ResolveClassPath(const FString& ClassPath, UEditorAssetSubsystem* EditorAssetSubsystem);

namespace
{
	void GeneratedVerifyInputAddNote(TArray<TSharedPtr<FJsonValue>>& Notes, const FString& Text)
	{
		Notes.Add(MakeShared<FJsonValueString>(Text));
	}

	double GeneratedVerifyInputNumber(const FJsonObject& Arguments, const FString& FieldName, double DefaultValue)
	{
		double Value = DefaultValue;
		Arguments.TryGetNumberField(FieldName, Value);
		return Value;
	}

	bool GeneratedVerifyInputBool(const FJsonObject& Arguments, const FString& FieldName, bool bDefaultValue)
	{
		bool bValue = bDefaultValue;
		Arguments.TryGetBoolField(FieldName, bValue);
		return bValue;
	}

	TSharedPtr<FJsonObject> GeneratedVerifyInputVectorObject(const FVector& Vector)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("x"), Vector.X);
		Object->SetNumberField(TEXT("y"), Vector.Y);
		Object->SetNumberField(TEXT("z"), Vector.Z);
		Object->SetNumberField(TEXT("size"), Vector.Size());
		return Object;
	}

	FKey GeneratedVerifyInputFallbackKey(const FString& AxisName)
	{
		const FString LowerAxis = AxisName.ToLower();
		if (LowerAxis.Contains(TEXT("right")))
		{
			return EKeys::D;
		}
		if (LowerAxis.Contains(TEXT("turn")))
		{
			return FKey(TEXT("Mouse X"));
		}
		if (LowerAxis.Contains(TEXT("lookup")) || LowerAxis.Contains(TEXT("look_up")))
		{
			return FKey(TEXT("Mouse Y"));
		}
		return EKeys::W;
	}

	FKey GeneratedVerifyInputResolvePrimaryKey(const FName AxisName, const double AxisValue, TArray<TSharedPtr<FJsonValue>>& Notes)
	{
		if (UInputSettings* InputSettings = GetMutableDefault<UInputSettings>())
		{
			TArray<FInputAxisKeyMapping> Mappings;
			InputSettings->GetAxisMappingByName(AxisName, Mappings);
			for (const FInputAxisKeyMapping& Mapping : Mappings)
			{
				const bool bSameDirection = (AxisValue >= 0.0 && Mapping.Scale >= 0.0) || (AxisValue < 0.0 && Mapping.Scale < 0.0);
				if (bSameDirection)
				{
					GeneratedVerifyInputAddNote(Notes, FString::Printf(TEXT("Resolved axis %s to key %s."), *AxisName.ToString(), *Mapping.Key.ToString()));
					return Mapping.Key;
				}
			}
		}
		const FKey Fallback = GeneratedVerifyInputFallbackKey(AxisName.ToString());
		GeneratedVerifyInputAddNote(Notes, FString::Printf(TEXT("Axis mapping not found; using fallback key %s."), *Fallback.ToString()));
		return Fallback;
	}

	bool GeneratedVerifyInputWaitForPie(UWorld*& OutWorld, APlayerController*& OutController, APawn*& OutPawn)
	{
		OutWorld = nullptr;
		OutController = nullptr;
		OutPawn = nullptr;
		for (int32 Attempt = 0; Attempt < 100; ++Attempt)
		{
			OutWorld = GEditor ? GEditor->PlayWorld : nullptr;
			OutController = OutWorld ? OutWorld->GetFirstPlayerController() : nullptr;
			OutPawn = OutController ? OutController->GetPawn() : nullptr;
			if (OutWorld && OutController && OutPawn)
			{
				return true;
			}
			FPlatformProcess::Sleep(0.05f);
			if (GEditor)
			{
				GEditor->Tick(0.05f, false);
			}
		}
		return false;
	}

	void GeneratedVerifyInputTickWorld(UWorld* World, const double DurationSeconds, APlayerController* Controller, const FKey Key, const double AxisValue)
	{
		const float Step = 1.0f / 60.0f;
		const int32 StepCount = FMath::Max(1, FMath::CeilToInt(DurationSeconds / Step));
		for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
		{
			if (Controller)
			{
				FInputKeyParams Params(Key, IE_Axis, static_cast<float>(AxisValue), false);
				Controller->InputKey(Params);
			}
			if (World)
			{
				World->Tick(LEVELTICK_All, Step);
			}
			if (GEditor)
			{
				GEditor->Tick(Step, false);
			}
		}
	}
}

FUnrealMcpExecutionResult ExecuteGeneratedVerifyInputDrivesPawnTool(const FString& ToolName, const FJsonObject& Arguments)
{
	FString PawnClassText;
	FString InputAxis;
	if (!Arguments.TryGetStringField(TEXT("pawnClass"), PawnClassText) || PawnClassText.TrimStartAndEnd().IsEmpty())
	{
		return MakeExecutionResult(TEXT("Missing required field 'pawnClass'."), nullptr, true);
	}
	if (!Arguments.TryGetStringField(TEXT("inputAxis"), InputAxis) || InputAxis.TrimStartAndEnd().IsEmpty())
	{
		return MakeExecutionResult(TEXT("Missing required field 'inputAxis'."), nullptr, true);
	}

	const double AxisValue = GeneratedVerifyInputNumber(Arguments, TEXT("axisValue"), 1.0);
	const double DurationSeconds = FMath::Clamp(GeneratedVerifyInputNumber(Arguments, TEXT("durationSeconds"), 0.5), 0.05, 5.0);
	const double ExpectedMotionDelta = GeneratedVerifyInputNumber(Arguments, TEXT("expectedMotionDelta"), 0.0);
	const double ExpectedYawDelta = GeneratedVerifyInputNumber(Arguments, TEXT("expectedYawDelta"), 0.0);
	const bool bStartPieIfNeeded = GeneratedVerifyInputBool(Arguments, TEXT("startPieIfNeeded"), true);

	TArray<TSharedPtr<FJsonValue>> Notes;
	TSharedPtr<FJsonObject> StructuredContent = MakeShared<FJsonObject>();
	StructuredContent->SetStringField(TEXT("action"), TEXT("verify_input_drives_pawn"));
	StructuredContent->SetStringField(TEXT("toolName"), ToolName);
	StructuredContent->SetBoolField(TEXT("pass"), false);
	StructuredContent->SetBoolField(TEXT("pieAlreadyRunning"), GEditor && GEditor->PlayWorld != nullptr);
	StructuredContent->SetObjectField(TEXT("actualLocationDelta"), GeneratedVerifyInputVectorObject(FVector::ZeroVector));
	StructuredContent->SetNumberField(TEXT("actualYawDelta"), 0.0);
	StructuredContent->SetNumberField(TEXT("actualPitchDelta"), 0.0);
	StructuredContent->SetArrayField(TEXT("notes"), Notes);

	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
	UClass* ExpectedPawnClass = ResolveClassPath(PawnClassText, EditorAssetSubsystem);
	if (!ExpectedPawnClass)
	{
		return MakeExecutionResult(FString::Printf(TEXT("Could not resolve pawnClass '%s'."), *PawnClassText), StructuredContent, false);
	}

	const bool bPieAlreadyRunning = GEditor && GEditor->PlayWorld != nullptr;
	bool bStartedPie = false;
	if (!bPieAlreadyRunning)
	{
		if (!bStartPieIfNeeded)
		{
			GeneratedVerifyInputAddNote(Notes, TEXT("No PIE session is active and startPieIfNeeded=false."));
			StructuredContent->SetArrayField(TEXT("notes"), Notes);
			return MakeExecutionResult(TEXT("No active PIE session; verification did not run."), StructuredContent, false);
		}
		FUnrealMcpExecutionResult StartResult;
		TSharedPtr<FJsonObject> StartArgs = MakeShared<FJsonObject>();
		StartArgs->SetBoolField(TEXT("simulate"), false);
		if (!TryExecuteEditorTool(TEXT("unreal.start_pie"), *StartArgs, StartResult) || StartResult.bIsError)
		{
			GeneratedVerifyInputAddNote(Notes, TEXT("Failed to start PIE."));
			StructuredContent->SetArrayField(TEXT("notes"), Notes);
			return MakeExecutionResult(TEXT("Failed to start PIE for input verification."), StructuredContent, false);
		}
		bStartedPie = true;
	}

	UWorld* PlayWorld = nullptr;
	APlayerController* PlayerController = nullptr;
	APawn* PossessedPawn = nullptr;
	if (!GeneratedVerifyInputWaitForPie(PlayWorld, PlayerController, PossessedPawn))
	{
		GeneratedVerifyInputAddNote(Notes, TEXT("Timed out waiting for PIE PlayerController and possessed Pawn."));
		StructuredContent->SetArrayField(TEXT("notes"), Notes);
		return MakeExecutionResult(TEXT("PIE did not produce a possessed pawn before timeout."), StructuredContent, false);
	}
	if (!PossessedPawn->IsA(ExpectedPawnClass))
	{
		GeneratedVerifyInputAddNote(Notes, FString::Printf(TEXT("Possessed pawn class was %s, expected %s."), *PossessedPawn->GetClass()->GetPathName(), *ExpectedPawnClass->GetPathName()));
		StructuredContent->SetArrayField(TEXT("notes"), Notes);
		if (bStartedPie)
		{
			GEditor->RequestEndPlayMap();
		}
		return MakeExecutionResult(TEXT("Possessed pawn did not match expected class."), StructuredContent, false);
	}

	const FVector StartLocation = PossessedPawn->GetActorLocation();
	const FRotator StartRotation = PossessedPawn->GetActorRotation();
	const FKey InputKey = GeneratedVerifyInputResolvePrimaryKey(FName(*InputAxis), AxisValue, Notes);
	GeneratedVerifyInputTickWorld(PlayWorld, DurationSeconds, PlayerController, InputKey, AxisValue);
	const FVector LocationDelta = PossessedPawn->GetActorLocation() - StartLocation;
	const FRotator RotationDelta = (PossessedPawn->GetActorRotation() - StartRotation).GetNormalized();
	const double ActualYawDelta = FMath::Abs(RotationDelta.Yaw);
	const double ActualPitchDelta = FMath::Abs(RotationDelta.Pitch);

	bool bPass = true;
	if (ExpectedMotionDelta > 0.0)
	{
		bPass = bPass && LocationDelta.Size() >= ExpectedMotionDelta;
	}
	if (ExpectedYawDelta > 0.0)
	{
		const FString LowerAxis = InputAxis.ToLower();
		const double RotationEvidence = (LowerAxis.Contains(TEXT("lookup")) || LowerAxis.Contains(TEXT("look_up"))) ? ActualPitchDelta : ActualYawDelta;
		bPass = bPass && RotationEvidence >= ExpectedYawDelta;
	}
	if (ExpectedMotionDelta <= 0.0 && ExpectedYawDelta <= 0.0)
	{
		bPass = LocationDelta.Size() > 0.1 || ActualYawDelta > 0.1 || ActualPitchDelta > 0.1;
	}

	StructuredContent->SetBoolField(TEXT("pass"), bPass);
	StructuredContent->SetBoolField(TEXT("pieAlreadyRunning"), bPieAlreadyRunning);
	StructuredContent->SetObjectField(TEXT("actualLocationDelta"), GeneratedVerifyInputVectorObject(LocationDelta));
	StructuredContent->SetNumberField(TEXT("actualYawDelta"), ActualYawDelta);
	StructuredContent->SetNumberField(TEXT("actualPitchDelta"), ActualPitchDelta);
	StructuredContent->SetArrayField(TEXT("notes"), Notes);
	if (bStartedPie)
	{
		GEditor->RequestEndPlayMap();
	}

	return MakeExecutionResult(
		FString::Printf(TEXT("Input verification %s for %s on %s."), bPass ? TEXT("passed") : TEXT("failed"), *InputAxis, *PawnClassText),
		StructuredContent,
		false);
}
