#include "UnrealMcpPieSmokeTools.h"

#include "UnrealMcpAutomationTools.h"
#include "UnrealMcpDiagnosticsTools.h"
#include "UnrealMcpModule.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Async/Async.h"
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/InputSettings.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeLock.h"
#include "PlayInEditorDataTypes.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectIterator.h"

namespace UnrealMcp
{
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);

	namespace
	{
		constexpr int32 DefaultPieSmokeTimeoutSeconds = 60;
		constexpr int32 MinPieSmokeTimeoutSeconds = 10;
		constexpr int32 MaxPieSmokeTimeoutSeconds = 300;
		constexpr int32 DefaultPieSmokeAliveWindowSeconds = 5;
		constexpr int32 MinPieSmokeAliveWindowSeconds = 1;
		constexpr int32 MaxPieSmokeAliveWindowSeconds = 30;
		constexpr int32 PieSmokeDiagnosticsExcerptLimit = 100;
		const TCHAR* PieSmokeRecoverabilityNote = TEXT("PIE crash is NOT recoverable in-process; use unreal_mcp_supervisor.py to restart the editor and re-run if needed.");

		enum class EPieSmokePhase : uint8
		{
			Queued,
			WaitingBegin,
			AliveWindow,
			WaitingEnd
		};

		struct FPieSmokeMapResolution
		{
			FString PackageName;
			FString ObjectPath;
			bool bHasMap = false;
		};

		struct FPieSmokeRunState
		{
			FUnrealMcpAutomationRunState SharedState;
			FPieSmokeMapResolution Map;
			int32 AliveWindowSeconds = DefaultPieSmokeAliveWindowSeconds;
			FDateTime AliveWindowStartedAtUtc;
			TArray<FString> DirtyAtStart;
			TArray<FString> DirtyAtEnd;
			FDelegateHandle BeginPieHandle;
			FDelegateHandle EndPieHandle;
			EPieSmokePhase Phase = EPieSmokePhase::Queued;
			bool bBeganPlayObserved = false;
			bool bAliveWindowSatisfied = false;
			bool bEndPieObserved = false;
			bool bPieEndedCleanly = false;
			bool bRequestEndIssued = false;
			bool bGameThreadStepPending = false;
		};

		FCriticalSection GPieSmokeMutex;
		bool bHasActivePieSmokeRun = false;
		FPieSmokeRunState GActivePieSmokeRun;
		FTSTicker::FDelegateHandle GPieSmokeTickerHandle;
#if WITH_DEV_AUTOMATION_TESTS
		bool bSuppressPlayerControlsPieRequestForTests = false;
#endif

		struct FPieInputProbeState
		{
			FString ProbeId;
			FString InputProfile;
			FString Status = TEXT("sampling");
			TWeakObjectPtr<APlayerController> PlayerController;
			TWeakObjectPtr<APawn> Pawn;
			FVector StartLocation = FVector::ZeroVector;
			FVector EndLocation = FVector::ZeroVector;
			FRotator StartRotation = FRotator::ZeroRotator;
			FRotator EndRotation = FRotator::ZeroRotator;
			double DurationSeconds = 0.5;
			double ElapsedSeconds = 0.0;
			int32 SampleCount = 0;
			bool bInputLayerAccepted = false;
			bool bUsedDirectFallback = false;
			bool bPressedInputKey = false;
			bool bReleasedInputKey = false;
			bool bSentDirectJump = false;
			FString ErrorKind;
			FString Message;
		};

		FCriticalSection GPieInputProbeMutex;
		TMap<FString, FPieInputProbeState> GPieInputProbeStates;
		FTSTicker::FDelegateHandle GPieInputProbeTickerHandle;
		FDelegateHandle GPieInputProbeEndPieHandle;

		TArray<TSharedPtr<FJsonValue>> MakePieSmokeStringArrayValues(const TArray<FString>& Values)
		{
			TArray<TSharedPtr<FJsonValue>> JsonValues;
			for (const FString& Value : Values)
			{
				JsonValues.Add(MakeShared<FJsonValueString>(Value));
			}
			return JsonValues;
		}

		TSharedPtr<FJsonObject> MakePieSmokeErrorObject(const FString& ErrorKind, const FString& Message)
		{
			TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
			ErrorObject->SetBoolField(TEXT("isError"), true);
			ErrorObject->SetStringField(TEXT("errorKind"), ErrorKind);
			ErrorObject->SetStringField(TEXT("message"), Message);
			return ErrorObject;
		}

		FUnrealMcpExecutionResult MakePieSmokeErrorResult(const FString& ErrorKind, const FString& Message)
		{
			return MakeExecutionResult(Message, MakePieSmokeErrorObject(ErrorKind, Message), true);
		}

		enum class EPieInputProbeProfile : uint8
		{
			MoveForward,
			MoveBackward,
			MoveRight,
			MoveLeft,
			Jump,
			LookYaw,
			LookPitch
		};

		bool ParsePieInputProbeProfile(const FString& RawProfile, EPieInputProbeProfile& OutProfile)
		{
			if (RawProfile.Equals(TEXT("moveForward"), ESearchCase::CaseSensitive))
			{
				OutProfile = EPieInputProbeProfile::MoveForward;
				return true;
			}
			if (RawProfile.Equals(TEXT("moveBackward"), ESearchCase::CaseSensitive))
			{
				OutProfile = EPieInputProbeProfile::MoveBackward;
				return true;
			}
			if (RawProfile.Equals(TEXT("moveRight"), ESearchCase::CaseSensitive))
			{
				OutProfile = EPieInputProbeProfile::MoveRight;
				return true;
			}
			if (RawProfile.Equals(TEXT("moveLeft"), ESearchCase::CaseSensitive))
			{
				OutProfile = EPieInputProbeProfile::MoveLeft;
				return true;
			}
			if (RawProfile.Equals(TEXT("jump"), ESearchCase::CaseSensitive))
			{
				OutProfile = EPieInputProbeProfile::Jump;
				return true;
			}
			if (RawProfile.Equals(TEXT("lookYaw"), ESearchCase::CaseSensitive))
			{
				OutProfile = EPieInputProbeProfile::LookYaw;
				return true;
			}
			if (RawProfile.Equals(TEXT("lookPitch"), ESearchCase::CaseSensitive))
			{
				OutProfile = EPieInputProbeProfile::LookPitch;
				return true;
			}
			return false;
		}

		FKey GetPieInputProbeKey(EPieInputProbeProfile Profile)
		{
			switch (Profile)
			{
			case EPieInputProbeProfile::MoveForward:
				return EKeys::W;
			case EPieInputProbeProfile::MoveBackward:
				return EKeys::S;
			case EPieInputProbeProfile::MoveRight:
				return EKeys::D;
			case EPieInputProbeProfile::MoveLeft:
				return EKeys::A;
			case EPieInputProbeProfile::Jump:
				return EKeys::SpaceBar;
			case EPieInputProbeProfile::LookYaw:
				return EKeys::MouseX;
			case EPieInputProbeProfile::LookPitch:
				return EKeys::MouseY;
			default:
				return EKeys::Invalid;
			}
		}

		bool IsPieInputProbeLookProfile(EPieInputProbeProfile Profile)
		{
			return Profile == EPieInputProbeProfile::LookYaw || Profile == EPieInputProbeProfile::LookPitch;
		}

		FVector GetPieInputProbeDirectMovement(APawn* Pawn, EPieInputProbeProfile Profile)
		{
			if (!Pawn)
			{
				return FVector::ZeroVector;
			}
			switch (Profile)
			{
			case EPieInputProbeProfile::MoveForward:
				return Pawn->GetActorForwardVector();
			case EPieInputProbeProfile::MoveBackward:
				return -Pawn->GetActorForwardVector();
			case EPieInputProbeProfile::MoveRight:
				return Pawn->GetActorRightVector();
			case EPieInputProbeProfile::MoveLeft:
				return -Pawn->GetActorRightVector();
			default:
				return FVector::ZeroVector;
			}
		}

		TSharedPtr<FJsonObject> MakePieInputProbeVectorObject(const FVector& Vector)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetNumberField(TEXT("x"), Vector.X);
			Object->SetNumberField(TEXT("y"), Vector.Y);
			Object->SetNumberField(TEXT("z"), Vector.Z);
			return Object;
		}

		TSharedPtr<FJsonObject> MakePieInputProbeRotatorObject(const FRotator& Rotator)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetNumberField(TEXT("pitch"), Rotator.Pitch);
			Object->SetNumberField(TEXT("yaw"), Rotator.Yaw);
			Object->SetNumberField(TEXT("roll"), Rotator.Roll);
			return Object;
		}

		double GetPieInputProbeDisplacementCm(const FPieInputProbeState& State)
		{
			return static_cast<double>(FVector::Dist(State.StartLocation, State.EndLocation));
		}

		double GetPieInputProbeRotationDeltaDegrees(const FPieInputProbeState& State)
		{
			const double PitchDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(State.StartRotation.Pitch, State.EndRotation.Pitch));
			const double YawDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(State.StartRotation.Yaw, State.EndRotation.Yaw));
			const double RollDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(State.StartRotation.Roll, State.EndRotation.Roll));
			return FMath::Max3(PitchDelta, YawDelta, RollDelta);
		}

		bool DidPieInputProbeMove(const FPieInputProbeState& State)
		{
			return GetPieInputProbeDisplacementCm(State) > 1.0 || GetPieInputProbeRotationDeltaDegrees(State) > 0.5;
		}

		FString GetPieInputProbeInjectionMethod(const FPieInputProbeState& State)
		{
			if (State.bInputLayerAccepted && State.bUsedDirectFallback)
			{
				return TEXT("input_key_event_args_with_direct_fallback");
			}
			if (State.bUsedDirectFallback)
			{
				return TEXT("direct_pawn_fallback");
			}
			return TEXT("input_key_event_args");
		}

		TSharedPtr<FJsonObject> MakePieInputProbeResultObject(const FPieInputProbeState& State)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("probeId"), State.ProbeId);
			Object->SetStringField(TEXT("status"), State.Status);
			Object->SetStringField(TEXT("inputProfile"), State.InputProfile);
			Object->SetObjectField(TEXT("startLocation"), MakePieInputProbeVectorObject(State.StartLocation));
			Object->SetObjectField(TEXT("endLocation"), MakePieInputProbeVectorObject(State.EndLocation));
			Object->SetObjectField(TEXT("startRotation"), MakePieInputProbeRotatorObject(State.StartRotation));
			Object->SetObjectField(TEXT("endRotation"), MakePieInputProbeRotatorObject(State.EndRotation));
			Object->SetNumberField(TEXT("durationSeconds"), State.DurationSeconds);
			Object->SetNumberField(TEXT("elapsedSeconds"), State.ElapsedSeconds);
			Object->SetNumberField(TEXT("sampleCount"), State.SampleCount);
			Object->SetNumberField(TEXT("displacement"), GetPieInputProbeDisplacementCm(State));
			Object->SetNumberField(TEXT("rotationDelta"), GetPieInputProbeRotationDeltaDegrees(State));
			Object->SetBoolField(TEXT("moved"), DidPieInputProbeMove(State));
			Object->SetStringField(TEXT("injectionMethod"), GetPieInputProbeInjectionMethod(State));
			if (!State.ErrorKind.IsEmpty())
			{
				Object->SetStringField(TEXT("errorKind"), State.ErrorKind);
			}
			if (!State.Message.IsEmpty())
			{
				Object->SetStringField(TEXT("message"), State.Message);
			}
			return Object;
		}

		bool SendPieInputProbeKeyEvent(APlayerController* PlayerController, const FKey& Key, EInputEvent Event, float Amount)
		{
			if (!PlayerController || !Key.IsValid())
			{
				return false;
			}
			FInputKeyEventArgs Args = FInputKeyEventArgs::CreateSimulated(Key, Event, Amount, 1, INPUTDEVICEID_NONE);
			return PlayerController->InputKey(Args);
		}

		bool SendPieInputProbeAxisEvent(APlayerController* PlayerController, const FKey& Key, float Amount, float DeltaTime)
		{
			if (!PlayerController || !Key.IsValid())
			{
				return false;
			}
			FInputKeyEventArgs Args(nullptr, INPUTDEVICEID_NONE, Key, Amount, FMath::Max(DeltaTime, KINDA_SMALL_NUMBER), 1, FPlatformTime::Cycles64());
			return PlayerController->InputKey(Args);
		}

		void ApplyPieInputProbeDirectFallback(FPieInputProbeState& State, EPieInputProbeProfile Profile, APlayerController* PlayerController, APawn* Pawn, float DeltaTime)
		{
			State.bUsedDirectFallback = true;
			if (Profile == EPieInputProbeProfile::Jump)
			{
				if (!State.bSentDirectJump)
				{
					if (ACharacter* Character = Cast<ACharacter>(Pawn))
					{
						Character->Jump();
						State.bSentDirectJump = true;
					}
				}
				return;
			}
			if (Profile == EPieInputProbeProfile::LookYaw)
			{
				if (PlayerController)
				{
					PlayerController->AddYawInput(45.0f * FMath::Max(DeltaTime, KINDA_SMALL_NUMBER));
				}
				return;
			}
			if (Profile == EPieInputProbeProfile::LookPitch)
			{
				if (PlayerController)
				{
					PlayerController->AddPitchInput(45.0f * FMath::Max(DeltaTime, KINDA_SMALL_NUMBER));
				}
				return;
			}

			const FVector Direction = GetPieInputProbeDirectMovement(Pawn, Profile);
			if (!Direction.IsNearlyZero() && Pawn)
			{
				Pawn->AddMovementInput(Direction.GetSafeNormal(), 1.0f);
			}
		}

		void InjectPieInputProbeInput(FPieInputProbeState& State, float DeltaTime)
		{
			EPieInputProbeProfile Profile;
			if (!ParsePieInputProbeProfile(State.InputProfile, Profile))
			{
				return;
			}

			APlayerController* PlayerController = State.PlayerController.Get();
			APawn* Pawn = State.Pawn.Get();
			const FKey Key = GetPieInputProbeKey(Profile);
			bool bAccepted = false;
			if (IsPieInputProbeLookProfile(Profile))
			{
				bAccepted = SendPieInputProbeAxisEvent(PlayerController, Key, 1.0f, DeltaTime);
			}
			else if (Profile == EPieInputProbeProfile::Jump)
			{
				if (!State.bPressedInputKey)
				{
					bAccepted = SendPieInputProbeKeyEvent(PlayerController, Key, IE_Pressed, 1.0f);
					State.bPressedInputKey = true;
				}
				else
				{
					bAccepted = State.bInputLayerAccepted;
				}
			}
			else
			{
				bAccepted = SendPieInputProbeKeyEvent(PlayerController, Key, State.bPressedInputKey ? IE_Repeat : IE_Pressed, 1.0f);
				State.bPressedInputKey = true;
			}

			State.bInputLayerAccepted = State.bInputLayerAccepted || bAccepted;
			if (!bAccepted)
			{
				ApplyPieInputProbeDirectFallback(State, Profile, PlayerController, Pawn, DeltaTime);
			}
		}

		void ReleasePieInputProbeInput(FPieInputProbeState& State)
		{
			if (State.bReleasedInputKey)
			{
				return;
			}
			EPieInputProbeProfile Profile;
			if (!ParsePieInputProbeProfile(State.InputProfile, Profile))
			{
				return;
			}

			APlayerController* PlayerController = State.PlayerController.Get();
			const FKey Key = GetPieInputProbeKey(Profile);
			if (IsPieInputProbeLookProfile(Profile))
			{
				SendPieInputProbeAxisEvent(PlayerController, Key, 0.0f, 0.016f);
			}
			else if (State.bPressedInputKey)
			{
				SendPieInputProbeKeyEvent(PlayerController, Key, IE_Released, 0.0f);
			}
			if (State.bSentDirectJump)
			{
				if (ACharacter* Character = Cast<ACharacter>(State.Pawn.Get()))
				{
					Character->StopJumping();
				}
			}
			State.bReleasedInputKey = true;
		}

		void FinishPieInputProbe(FPieInputProbeState& State, const FString& Message)
		{
			ReleasePieInputProbeInput(State);
			if (APawn* Pawn = State.Pawn.Get())
			{
				State.EndLocation = Pawn->GetActorLocation();
				State.EndRotation = Pawn->GetActorRotation();
			}
			State.Status = TEXT("done");
			State.Message = Message;
		}

		bool HasSamplingPieInputProbeLocked()
		{
			for (const TPair<FString, FPieInputProbeState>& Pair : GPieInputProbeStates)
			{
				if (Pair.Value.Status == TEXT("sampling"))
				{
					return true;
				}
			}
			return false;
		}

		void UnregisterPieInputProbeEndPieDelegate()
		{
			if (GPieInputProbeEndPieHandle.IsValid())
			{
				FEditorDelegates::EndPIE.Remove(GPieInputProbeEndPieHandle);
				GPieInputProbeEndPieHandle.Reset();
			}
		}

		void MarkPieInputProbesDoneForEndPie()
		{
			FScopeLock Lock(&GPieInputProbeMutex);
			for (TPair<FString, FPieInputProbeState>& Pair : GPieInputProbeStates)
			{
				FPieInputProbeState& State = Pair.Value;
				if (State.Status == TEXT("sampling"))
				{
					State.ErrorKind = TEXT("PieEnded");
					if (APawn* Pawn = State.Pawn.Get())
					{
						State.EndLocation = Pawn->GetActorLocation();
						State.EndRotation = Pawn->GetActorRotation();
					}
					State.Status = TEXT("done");
					State.Message = TEXT("PIE ended before the input probe completed.");
				}
			}
			if (GPieInputProbeTickerHandle.IsValid())
			{
				FTSTicker::GetCoreTicker().RemoveTicker(GPieInputProbeTickerHandle);
				GPieInputProbeTickerHandle.Reset();
			}
		}

		void RegisterPieInputProbeEndPieDelegate()
		{
			if (GPieInputProbeEndPieHandle.IsValid())
			{
				return;
			}
			GPieInputProbeEndPieHandle = FEditorDelegates::EndPIE.AddLambda([](const bool bIsSimulating)
			{
				(void)bIsSimulating;
				MarkPieInputProbesDoneForEndPie();
			});
		}

		bool TickActivePieInputProbes(float DeltaTime)
		{
			if (!IsInGameThread())
			{
				return true;
			}

			TArray<FString> ProbeIds;
			{
				FScopeLock Lock(&GPieInputProbeMutex);
				for (const TPair<FString, FPieInputProbeState>& Pair : GPieInputProbeStates)
				{
					if (Pair.Value.Status == TEXT("sampling"))
					{
						ProbeIds.Add(Pair.Key);
					}
				}
			}

			for (const FString& ProbeId : ProbeIds)
			{
				FPieInputProbeState State;
				{
					FScopeLock Lock(&GPieInputProbeMutex);
					FPieInputProbeState* StoredState = GPieInputProbeStates.Find(ProbeId);
					if (!StoredState || StoredState->Status != TEXT("sampling"))
					{
						continue;
					}
					State = *StoredState;
				}

				APawn* Pawn = State.Pawn.Get();
				APlayerController* PlayerController = State.PlayerController.Get();
				if (!GEditor || !GEditor->PlayWorld || !Pawn || !PlayerController)
				{
					State.ErrorKind = TEXT("PieUnavailable");
					FinishPieInputProbe(State, TEXT("PIE, player controller, or pawn became unavailable during sampling."));
				}
				else
				{
					InjectPieInputProbeInput(State, DeltaTime);
					State.ElapsedSeconds += FMath::Max(0.0f, DeltaTime);
					State.EndLocation = Pawn->GetActorLocation();
					State.EndRotation = Pawn->GetActorRotation();
					++State.SampleCount;
					if (State.ElapsedSeconds >= State.DurationSeconds)
					{
						FinishPieInputProbe(State, TEXT("PIE input probe completed."));
					}
				}

				{
					FScopeLock Lock(&GPieInputProbeMutex);
					GPieInputProbeStates.Add(ProbeId, State);
				}
			}

			{
				FScopeLock Lock(&GPieInputProbeMutex);
				if (!HasSamplingPieInputProbeLocked())
				{
					GPieInputProbeTickerHandle.Reset();
					UnregisterPieInputProbeEndPieDelegate();
					return false;
				}
			}
			return true;
		}

		void EnsurePieInputProbeTicker()
		{
			if (!GPieInputProbeTickerHandle.IsValid())
			{
				RegisterPieInputProbeEndPieDelegate();
				GPieInputProbeTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickActivePieInputProbes), 0.0f);
			}
		}

		void ResetPieInputProbeState()
		{
			FScopeLock Lock(&GPieInputProbeMutex);
			if (GPieInputProbeTickerHandle.IsValid())
			{
				FTSTicker::GetCoreTicker().RemoveTicker(GPieInputProbeTickerHandle);
				GPieInputProbeTickerHandle.Reset();
			}
			UnregisterPieInputProbeEndPieDelegate();
			GPieInputProbeStates.Reset();
		}

		int32 GetClampedIntArgument(const FJsonObject& Arguments, const FString& FieldName, int32 DefaultValue, int32 MinValue, int32 MaxValue)
		{
			double Value = static_cast<double>(DefaultValue);
			if (Arguments.TryGetNumberField(FieldName, Value))
			{
				return FMath::Clamp(static_cast<int32>(Value), MinValue, MaxValue);
			}
			return DefaultValue;
		}

		bool ParsePieSmokeArguments(const FJsonObject& Arguments, int32& OutTimeoutSeconds, int32& OutAliveWindowSeconds, FString& OutErrorKind)
		{
			OutTimeoutSeconds = GetClampedIntArgument(
				Arguments,
				TEXT("timeoutSeconds"),
				DefaultPieSmokeTimeoutSeconds,
				MinPieSmokeTimeoutSeconds,
				MaxPieSmokeTimeoutSeconds);
			OutAliveWindowSeconds = GetClampedIntArgument(
				Arguments,
				TEXT("aliveWindowSeconds"),
				DefaultPieSmokeAliveWindowSeconds,
				MinPieSmokeAliveWindowSeconds,
				MaxPieSmokeAliveWindowSeconds);

			if (OutAliveWindowSeconds >= OutTimeoutSeconds)
			{
				OutErrorKind = TEXT("InvalidArguments");
				return false;
			}
			OutErrorKind.Reset();
			return true;
		}

		FString GetCurrentEditorMapPackageName()
		{
			UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			UPackage* Package = EditorWorld ? EditorWorld->GetOutermost() : nullptr;
			return Package ? Package->GetName() : FString();
		}

		bool IsCurrentEditorMapDirty()
		{
			UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			UPackage* Package = EditorWorld ? EditorWorld->GetOutermost() : nullptr;
			return Package && Package->IsDirty();
		}

		TArray<FString> SnapshotDirtyPackages()
		{
			TArray<FString> DirtyPackages;
			for (TObjectIterator<UPackage> PackageIt; PackageIt; ++PackageIt)
			{
				UPackage* Package = *PackageIt;
				if (Package && Package->IsDirty())
				{
					const FString PackageName = Package->GetName();
					if (!PackageName.IsEmpty())
					{
						DirtyPackages.Add(PackageName);
					}
				}
			}
			DirtyPackages.Sort();
			return DirtyPackages;
		}

		TArray<FString> Difference(const TArray<FString>& Left, const TArray<FString>& Right)
		{
			TSet<FString> RightSet;
			for (const FString& Value : Right)
			{
				RightSet.Add(Value);
			}

			TArray<FString> Result;
			for (const FString& Value : Left)
			{
				if (!RightSet.Contains(Value))
				{
					Result.Add(Value);
				}
			}
			Result.Sort();
			return Result;
		}

		bool ResolvePieSmokeMapPath(const FString& RawMapPath, FPieSmokeMapResolution& OutResolution, FString& OutErrorKind, FString& OutMessage)
		{
			OutResolution = FPieSmokeMapResolution();
			OutErrorKind.Reset();
			OutMessage.Reset();

			const FString TrimmedPath = RawMapPath.TrimStartAndEnd();
			if (TrimmedPath.IsEmpty())
			{
				return true;
			}

			if (!TrimmedPath.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive))
			{
				OutErrorKind = TEXT("InvalidMapPath");
				OutMessage = TEXT("mapPath must start with /Game/.");
				return false;
			}

			FString PackageName = TrimmedPath;
			FString ObjectName;
			const bool bHasObjectName = TrimmedPath.Split(TEXT("."), &PackageName, &ObjectName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			FText InvalidPackageReason;
			if (!FPackageName::IsValidLongPackageName(PackageName, false, &InvalidPackageReason))
			{
				OutErrorKind = TEXT("InvalidMapPath");
				OutMessage = InvalidPackageReason.ToString();
				return false;
			}

			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			if (bHasObjectName)
			{
				const FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(TrimmedPath));
				if (!AssetData.IsValid())
				{
					OutErrorKind = TEXT("MapNotFound");
					OutMessage = FString::Printf(TEXT("Map asset '%s' was not found."), *TrimmedPath);
					return false;
				}
				if (AssetData.AssetClassPath != UWorld::StaticClass()->GetClassPathName())
				{
					OutErrorKind = TEXT("InvalidMapPath");
					OutMessage = FString::Printf(TEXT("Asset '%s' is not a UWorld asset."), *TrimmedPath);
					return false;
				}
				OutResolution.PackageName = AssetData.PackageName.ToString();
				OutResolution.ObjectPath = AssetData.GetSoftObjectPath().ToString();
				OutResolution.bHasMap = true;
				return true;
			}

			TArray<FAssetData> PackageAssets;
			AssetRegistry.GetAssetsByPackageName(FName(*PackageName), PackageAssets);
			if (PackageAssets.IsEmpty())
			{
				OutErrorKind = TEXT("MapNotFound");
				OutMessage = FString::Printf(TEXT("Map package '%s' was not found."), *PackageName);
				return false;
			}

			for (const FAssetData& AssetData : PackageAssets)
			{
				if (AssetData.AssetClassPath == UWorld::StaticClass()->GetClassPathName())
				{
					OutResolution.PackageName = AssetData.PackageName.ToString();
					OutResolution.ObjectPath = AssetData.GetSoftObjectPath().ToString();
					OutResolution.bHasMap = true;
					return true;
				}
			}

			OutErrorKind = TEXT("InvalidMapPath");
			OutMessage = FString::Printf(TEXT("Package '%s' exists but does not contain a UWorld asset."), *PackageName);
			return false;
		}

		bool HasDirtyEditorMapConflict(const FPieSmokeMapResolution& Map)
		{
			if (!Map.bHasMap)
			{
				return false;
			}
			const FString CurrentMapPackage = GetCurrentEditorMapPackageName();
			if (CurrentMapPackage.IsEmpty() || CurrentMapPackage == Map.PackageName)
			{
				return false;
			}
			return IsCurrentEditorMapDirty();
		}

		TSharedPtr<FJsonObject> MakePieSmokeReport(const FPieSmokeRunState& State)
		{
			TSharedPtr<FJsonObject> ReportObject = MakeShared<FJsonObject>();
			if (State.Map.bHasMap)
			{
				ReportObject->SetStringField(TEXT("mapPath"), State.Map.ObjectPath);
			}
			ReportObject->SetBoolField(TEXT("beganPlayObserved"), State.bBeganPlayObserved);
			ReportObject->SetBoolField(TEXT("aliveWindowSatisfied"), State.bAliveWindowSatisfied);
			ReportObject->SetBoolField(TEXT("pieEndedCleanly"), State.bPieEndedCleanly);
			ReportObject->SetObjectField(
				TEXT("diagnostics"),
				CaptureDiagnosticsSummarySince(State.SharedState.StartedAtUtc, PieSmokeDiagnosticsExcerptLimit));

			TSharedPtr<FJsonObject> DirtinessDelta = MakeShared<FJsonObject>();
			DirtinessDelta->SetArrayField(TEXT("dirtyAtStart"), MakePieSmokeStringArrayValues(State.DirtyAtStart));
			DirtinessDelta->SetArrayField(TEXT("dirtyAtEnd"), MakePieSmokeStringArrayValues(State.DirtyAtEnd));
			DirtinessDelta->SetArrayField(TEXT("newlyDirty"), MakePieSmokeStringArrayValues(Difference(State.DirtyAtEnd, State.DirtyAtStart)));
			DirtinessDelta->SetArrayField(TEXT("cleanedDuringPie"), MakePieSmokeStringArrayValues(Difference(State.DirtyAtStart, State.DirtyAtEnd)));
			ReportObject->SetObjectField(TEXT("packageDirtinessDelta"), DirtinessDelta);
			ReportObject->SetStringField(TEXT("recoverabilityNote"), PieSmokeRecoverabilityNote);
			return ReportObject;
		}

		void UnregisterPieSmokeDelegates(FPieSmokeRunState& State)
		{
			if (State.BeginPieHandle.IsValid())
			{
				FEditorDelegates::BeginPIE.Remove(State.BeginPieHandle);
				State.BeginPieHandle.Reset();
			}
			if (State.EndPieHandle.IsValid())
			{
				FEditorDelegates::EndPIE.Remove(State.EndPieHandle);
				State.EndPieHandle.Reset();
			}
		}

		void RemovePieSmokeDelegateHandlesOnGameThread(FDelegateHandle BeginHandle, FDelegateHandle EndHandle)
		{
			auto RemoveHandles = [BeginHandle, EndHandle]()
			{
				if (BeginHandle.IsValid())
				{
					FEditorDelegates::BeginPIE.Remove(BeginHandle);
				}
				if (EndHandle.IsValid())
				{
					FEditorDelegates::EndPIE.Remove(EndHandle);
				}
			};

			if (IsInGameThread())
			{
				RemoveHandles();
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, MoveTemp(RemoveHandles));
			}
		}

		void RequestEndPieOnGameThread()
		{
			auto RequestEnd = []()
			{
				if (GEditor && (GEditor->PlayWorld != nullptr || GEditor->GetPlaySessionRequest().IsSet()))
				{
					GEditor->RequestEndPlayMap();
				}
			};

			if (IsInGameThread())
			{
				RequestEnd();
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, MoveTemp(RequestEnd));
			}
		}

		void FinishPieSmokeRunLocked(FPieSmokeRunState& State, const FString& Status, const FString& Reason)
		{
			UnregisterPieSmokeDelegates(State);
			State.DirtyAtEnd = SnapshotDirtyPackages();
			State.SharedState.Status = Status;
			State.SharedState.Reason = Reason;
			State.SharedState.EndedAtUtc = FDateTime::UtcNow();
			State.SharedState.LastHeartbeatUtc = State.SharedState.EndedAtUtc;
			State.SharedState.PieReport = MakePieSmokeReport(State);
			SaveAutomationRunStateFile(State.SharedState);
			SetActiveAutomationRunState(State.SharedState);
			ClearActiveAutomationRunIfMatching(State.SharedState.RunId);
			bHasActivePieSmokeRun = false;
			GActivePieSmokeRun = FPieSmokeRunState();
		}

		void RegisterPieSmokeDelegates(FPieSmokeRunState& State)
		{
			State.BeginPieHandle = FEditorDelegates::BeginPIE.AddLambda([](const bool bIsSimulating)
			{
				(void)bIsSimulating;
				FScopeLock Lock(&GPieSmokeMutex);
				if (bHasActivePieSmokeRun)
				{
					GActivePieSmokeRun.bBeganPlayObserved = true;
				}
			});
			State.EndPieHandle = FEditorDelegates::EndPIE.AddLambda([](const bool bIsSimulating)
			{
				(void)bIsSimulating;
				FScopeLock Lock(&GPieSmokeMutex);
				if (bHasActivePieSmokeRun)
				{
					GActivePieSmokeRun.bEndPieObserved = true;
				}
			});
		}

		void PersistPieSmokeProgressLocked(FPieSmokeRunState& State)
		{
			State.SharedState.LastHeartbeatUtc = FDateTime::UtcNow();
			SaveAutomationRunStateFile(State.SharedState);
			SetActiveAutomationRunState(State.SharedState);
		}

		void PerformPieSmokeGameThreadStep()
		{
			check(IsInGameThread());
			FScopeLock Lock(&GPieSmokeMutex);
			if (!bHasActivePieSmokeRun)
			{
				return;
			}

			FPieSmokeRunState& State = GActivePieSmokeRun;
			State.bGameThreadStepPending = false;
			const FDateTime NowUtc = FDateTime::UtcNow();
			const FDateTime TimeoutAtUtc = State.SharedState.StartedAtUtc + FTimespan::FromSeconds(State.SharedState.TimeoutSeconds);
			if (NowUtc >= TimeoutAtUtc)
			{
				RequestEndPieOnGameThread();
				FinishPieSmokeRunLocked(State, TEXT("failed"), TEXT("PIE smoke exceeded timeout before clean completion."));
				return;
			}

			if (State.Phase == EPieSmokePhase::Queued)
			{
				if (!GEditor)
				{
					FinishPieSmokeRunLocked(State, TEXT("failed"), TEXT("GEditor is unavailable."));
					return;
				}

				if (State.Map.bHasMap && GetCurrentEditorMapPackageName() != State.Map.PackageName)
				{
					TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);
					if (UEditorLoadingAndSavingUtils::LoadMap(State.Map.ObjectPath) == nullptr)
					{
						FinishPieSmokeRunLocked(State, TEXT("failed"), FString::Printf(TEXT("Failed to open map '%s'."), *State.Map.ObjectPath));
						return;
					}
				}

				RegisterPieSmokeDelegates(State);
				FRequestPlaySessionParams SessionParams;
				// Lock-ordering note: GPieSmokeMutex is held here while calling
				// RequestPlaySession. UE's RequestPlaySession is a deferred
				// setter (it stores PlaySessionRequest on GEditor and returns;
				// BeginPIE is broadcast later from UEditorEngine::Tick AFTER
				// this function exits and releases the lock). The BeginPIE
				// lambda registered above acquires GPieSmokeMutex itself but
				// runs on a subsequent tick, so there is no same-thread
				// re-entrant lock acquisition. If a future UE version changes
				// RequestPlaySession to synchronously broadcast BeginPIE, this
				// code must be refactored to release the lock around the call.
				GEditor->RequestPlaySession(SessionParams);
				State.SharedState.Status = TEXT("running");
				State.Phase = EPieSmokePhase::WaitingBegin;
				PersistPieSmokeProgressLocked(State);
				return;
			}

			if (State.Phase == EPieSmokePhase::WaitingBegin)
			{
				if (State.bBeganPlayObserved)
				{
					State.AliveWindowStartedAtUtc = NowUtc;
					State.Phase = EPieSmokePhase::AliveWindow;
					PersistPieSmokeProgressLocked(State);
				}
				return;
			}

			if (State.Phase == EPieSmokePhase::AliveWindow)
			{
				UWorld* PlayWorld = GEditor ? GEditor->PlayWorld : nullptr;
				if (PlayWorld && PlayWorld->HasBegunPlay())
				{
					State.bAliveWindowSatisfied = true;
				}

				const bool bAliveWindowElapsed =
					State.AliveWindowStartedAtUtc.GetTicks() > 0
					&& NowUtc >= (State.AliveWindowStartedAtUtc + FTimespan::FromSeconds(State.AliveWindowSeconds));
				if (PlayWorld == nullptr || bAliveWindowElapsed)
				{
					if (!State.bRequestEndIssued)
					{
						RequestEndPieOnGameThread();
						State.bRequestEndIssued = true;
					}
					State.Phase = EPieSmokePhase::WaitingEnd;
					PersistPieSmokeProgressLocked(State);
				}
				return;
			}

			if (State.Phase == EPieSmokePhase::WaitingEnd)
			{
				const bool bPlayWorldCleared = GEditor == nullptr || GEditor->PlayWorld == nullptr;
				if (State.bEndPieObserved && bPlayWorldCleared)
				{
					State.bPieEndedCleanly = true;
					const bool bCompleted = State.bBeganPlayObserved && State.bAliveWindowSatisfied && State.bPieEndedCleanly;
					FinishPieSmokeRunLocked(
						State,
						bCompleted ? TEXT("completed") : TEXT("failed"),
						bCompleted ? FString() : TEXT("PIE smoke did not satisfy all lifecycle checks."));
				}
			}
		}

		void DispatchPieSmokeGameThreadStep()
		{
			{
				FScopeLock Lock(&GPieSmokeMutex);
				if (!bHasActivePieSmokeRun || GActivePieSmokeRun.bGameThreadStepPending)
				{
					return;
				}
				GActivePieSmokeRun.bGameThreadStepPending = true;
			}

			AsyncTask(ENamedThreads::GameThread, []()
			{
				PerformPieSmokeGameThreadStep();
			});
		}

		bool MarkPieSmokeRunStale(const FString& RunId, const FString& StaleReason)
		{
			FPieSmokeRunState State;
			{
				FScopeLock Lock(&GPieSmokeMutex);
				if (!bHasActivePieSmokeRun || GActivePieSmokeRun.SharedState.RunId != RunId)
				{
					return false;
				}
				State = GActivePieSmokeRun;
				bHasActivePieSmokeRun = false;
				GActivePieSmokeRun.BeginPieHandle.Reset();
				GActivePieSmokeRun.EndPieHandle.Reset();
				GActivePieSmokeRun = FPieSmokeRunState();
			}

			RequestEndPieOnGameThread();
			RemovePieSmokeDelegateHandlesOnGameThread(State.BeginPieHandle, State.EndPieHandle);
			State.DirtyAtEnd = SnapshotDirtyPackages();
			State.SharedState.Status = TEXT("stale");
			State.SharedState.StaleReason = StaleReason.TrimStartAndEnd().IsEmpty() ? TEXT("hard_timeout") : StaleReason.TrimStartAndEnd();
			State.SharedState.Reason = State.SharedState.StaleReason == TEXT("editor_shutdown")
				? TEXT("editor shut down while PIE smoke was active")
				: TEXT("PIE smoke stale recovery ran best-effort cleanup.");
			State.SharedState.EndedAtUtc = FDateTime::UtcNow();
			State.SharedState.LastHeartbeatUtc = State.SharedState.EndedAtUtc;
			State.SharedState.PieReport = MakePieSmokeReport(State);
			SaveAutomationRunStateFile(State.SharedState);
			ClearActiveAutomationRunIfMatching(RunId);
			return true;
		}

		bool TickActivePieSmokeRun(float DeltaTime)
		{
			(void)DeltaTime;

			FUnrealMcpAutomationRunState SharedState;
			{
				FScopeLock Lock(&GPieSmokeMutex);
				if (!bHasActivePieSmokeRun)
				{
					GPieSmokeTickerHandle.Reset();
					return false;
				}
				SharedState = GActivePieSmokeRun.SharedState;
			}

			const FDateTime NowUtc = FDateTime::UtcNow();
			const FString StaleReason = GetAutomationRunStaleReason(SharedState, NowUtc);
			if (!StaleReason.IsEmpty())
			{
				MarkPieSmokeRunStale(SharedState.RunId, StaleReason);
				GPieSmokeTickerHandle.Reset();
				return false;
			}

			{
				FScopeLock Lock(&GPieSmokeMutex);
				if (bHasActivePieSmokeRun && GActivePieSmokeRun.SharedState.RunId == SharedState.RunId)
				{
					GActivePieSmokeRun.SharedState.LastHeartbeatUtc = NowUtc;
					SetActiveAutomationRunState(GActivePieSmokeRun.SharedState);
				}
			}

			DispatchPieSmokeGameThreadStep();
			return true;
		}

		void EnsurePieSmokeTicker()
		{
			if (!GPieSmokeTickerHandle.IsValid())
			{
				GPieSmokeTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickActivePieSmokeRun), 0.1f);
			}
		}

		TSharedPtr<FJsonObject> MakePlayerControlsErrorObject(const FString& ErrorKind, const FString& Message)
		{
			TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
			ErrorObject->SetStringField(TEXT("errorKind"), ErrorKind);
			ErrorObject->SetStringField(TEXT("message"), Message);
			return ErrorObject;
		}

		FUnrealMcpExecutionResult MakePlayerControlsErrorResult(const FString& ErrorKind, const FString& Message, TSharedPtr<FJsonObject> Content)
		{
			if (!Content.IsValid())
			{
				Content = MakeShared<FJsonObject>();
			}
			Content->SetObjectField(TEXT("error"), MakePlayerControlsErrorObject(ErrorKind, Message));
			return MakeExecutionResult(Message, Content, true);
		}

		FString NormalizeExpectedPawnClassPath(const FString& RawPath)
		{
			FString Path = RawPath.TrimStartAndEnd();
			if (Path.IsEmpty()
				|| Path.StartsWith(TEXT("/Script/"), ESearchCase::CaseSensitive)
				|| Path.EndsWith(TEXT("_C"), ESearchCase::CaseSensitive))
			{
				return Path;
			}

			if (Path.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive)
				|| Path.StartsWith(TEXT("/Engine/"), ESearchCase::CaseSensitive))
			{
				if (Path.Contains(TEXT("."), ESearchCase::CaseSensitive))
				{
					return Path + TEXT("_C");
				}
				return FString::Printf(TEXT("%s.%s_C"), *Path, *FPackageName::GetLongPackageAssetName(Path));
			}
			return Path;
		}

		UClass* ResolveExpectedPawnClass(const FString& RawPath, FString& OutResolvedPath)
		{
			OutResolvedPath.Reset();
			const FString Trimmed = RawPath.TrimStartAndEnd();
			if (Trimmed.IsEmpty())
			{
				return nullptr;
			}

			if (UClass* DirectClass = LoadObject<UClass>(nullptr, *Trimmed))
			{
				OutResolvedPath = DirectClass->GetPathName();
				return DirectClass;
			}

			const FString NormalizedPath = NormalizeExpectedPawnClassPath(Trimmed);
			if (!NormalizedPath.Equals(Trimmed, ESearchCase::CaseSensitive))
			{
				if (UClass* NormalizedClass = LoadObject<UClass>(nullptr, *NormalizedPath))
				{
					OutResolvedPath = NormalizedClass->GetPathName();
					return NormalizedClass;
				}
			}
			return nullptr;
		}

		bool PlayerControlsHasLegacyAxisMapping(const FName AxisName)
		{
			UInputSettings* InputSettings = GetMutableDefault<UInputSettings>();
			if (!InputSettings)
			{
				return false;
			}

			TArray<FInputAxisKeyMapping> Mappings;
			InputSettings->GetAxisMappingByName(AxisName, Mappings);
			return Mappings.Num() > 0;
		}

		bool PlayerControlsHasLegacyActionMapping(const FName ActionName)
		{
			UInputSettings* InputSettings = GetMutableDefault<UInputSettings>();
			if (!InputSettings)
			{
				return false;
			}

			TArray<FInputActionKeyMapping> Mappings;
			InputSettings->GetActionMappingByName(ActionName, Mappings);
			return Mappings.Num() > 0;
		}

		bool PlayerControlsInputComponentHasAxis(UInputComponent* InputComponent, const FName AxisName)
		{
			if (!InputComponent)
			{
				return false;
			}
			for (const FInputAxisBinding& AxisBinding : InputComponent->AxisBindings)
			{
				if (AxisBinding.AxisName == AxisName)
				{
					return true;
				}
			}
			return false;
		}

		bool PlayerControlsInputComponentHasAction(UInputComponent* InputComponent, const FName ActionName)
		{
			if (!InputComponent)
			{
				return false;
			}
			for (int32 Index = 0; Index < InputComponent->GetNumActionBindings(); ++Index)
			{
				if (InputComponent->GetActionBinding(Index).GetActionName() == ActionName)
				{
					return true;
				}
			}
			return false;
		}

		bool PlayerControlsRuntimeHasAxis(const TArray<UInputComponent*>& InputComponents, const FName AxisName)
		{
			for (UInputComponent* InputComponent : InputComponents)
			{
				if (PlayerControlsInputComponentHasAxis(InputComponent, AxisName))
				{
					return true;
				}
			}
			return false;
		}

		bool PlayerControlsRuntimeHasAction(const TArray<UInputComponent*>& InputComponents, const FName ActionName)
		{
			for (UInputComponent* InputComponent : InputComponents)
			{
				if (PlayerControlsInputComponentHasAction(InputComponent, ActionName))
				{
					return true;
				}
			}
			return false;
		}

		TSharedPtr<FJsonObject> MakePlayerControlsBindingObject(bool bLegacy, bool bRuntime)
		{
			TSharedPtr<FJsonObject> BindingObject = MakeShared<FJsonObject>();
			BindingObject->SetBoolField(TEXT("legacyMappingExists"), bLegacy);
			BindingObject->SetBoolField(TEXT("runtimeBindingExists"), bRuntime);
			BindingObject->SetBoolField(TEXT("exists"), bLegacy || bRuntime);
			return BindingObject;
		}

		class FScopedPlayerControlsVerificationLock
		{
		public:
			bool TryAcquire(TSharedPtr<FJsonObject>& OutFailure)
			{
				FUnrealMcpAutomationRunState ActiveRun;
				if (TryGetActiveAutomationRunState(ActiveRun))
				{
					const FString Message = FString::Printf(TEXT("Run '%s' (%s) is still %s."), *ActiveRun.RunId, *LexToString(ActiveRun.RunType), *ActiveRun.Status);
					OutFailure = MakePlayerControlsErrorObject(TEXT("RunAlreadyActive"), Message);
					OutFailure->SetStringField(TEXT("activeRunId"), ActiveRun.RunId);
					OutFailure->SetStringField(TEXT("activeRunType"), LexToString(ActiveRun.RunType));
					return false;
				}

				RunId = MakeAutomationRunId();
				FUnrealMcpAutomationRunState State;
				State.RunId = RunId;
				State.RunType = EUnrealMcpAutomationRunType::PieSmoke;
				State.Status = TEXT("running");
				State.AcceptedAtUtc = FDateTime::UtcNow();
				State.StartedAtUtc = State.AcceptedAtUtc;
				State.LastHeartbeatUtc = State.AcceptedAtUtc;
				State.TimeoutSeconds = 60;
				SetActiveAutomationRunState(State);
				bAcquired = true;
				return true;
			}

			~FScopedPlayerControlsVerificationLock()
			{
				if (bAcquired && !RunId.IsEmpty())
				{
					ClearActiveAutomationRunIfMatching(RunId);
				}
			}

			FString GetRunId() const
			{
				return RunId;
			}

		private:
			bool bAcquired = false;
			FString RunId;
		};

		bool RequestPieForPlayerControls()
		{
			if (!GEditor)
			{
				return false;
			}

#if WITH_DEV_AUTOMATION_TESTS
			if (bSuppressPlayerControlsPieRequestForTests)
			{
				return true;
			}
#endif

			FRequestPlaySessionParams SessionParams;
			GEditor->RequestPlaySession(SessionParams);
			return true;
		}

		TSharedPtr<FJsonObject> InspectPlayerControlsRuntime(const FString& ExpectedPawnClassText, bool bBeginPieObserved, bool bPieAlreadyActive)
		{
			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetBoolField(TEXT("needsPie"), false);
			Content->SetBoolField(TEXT("canVerifyRuntime"), GEditor && GEditor->PlayWorld != nullptr);
			Content->SetBoolField(TEXT("beginPieObserved"), bBeginPieObserved);
			Content->SetBoolField(TEXT("pieAlreadyActive"), bPieAlreadyActive);

			UWorld* PlayWorld = GEditor ? GEditor->PlayWorld : nullptr;
			APlayerController* PlayerController = PlayWorld ? PlayWorld->GetFirstPlayerController() : nullptr;
			APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;

			Content->SetBoolField(TEXT("playerControllerExists"), PlayerController != nullptr);
			Content->SetBoolField(TEXT("possessedPawnExists"), Pawn != nullptr);
			Content->SetStringField(TEXT("playerController"), PlayerController ? PlayerController->GetPathName() : FString());
			Content->SetStringField(TEXT("possessedPawn"), Pawn ? Pawn->GetPathName() : FString());

			UClass* PawnClass = Pawn ? Pawn->GetClass() : nullptr;
			Content->SetStringField(TEXT("pawnClass"), PawnClass ? PawnClass->GetPathName() : FString());

			if (!ExpectedPawnClassText.TrimStartAndEnd().IsEmpty())
			{
				FString ResolvedExpectedClassPath;
				UClass* ExpectedPawnClass = ResolveExpectedPawnClass(ExpectedPawnClassText, ResolvedExpectedClassPath);
				Content->SetStringField(TEXT("expectedPawnClass"), ExpectedPawnClassText.TrimStartAndEnd());
				Content->SetStringField(TEXT("resolvedExpectedPawnClass"), ResolvedExpectedClassPath);
				Content->SetBoolField(TEXT("expectedPawnClassResolved"), ExpectedPawnClass != nullptr);
				Content->SetBoolField(TEXT("pawnClassMatchesExpected"), ExpectedPawnClass && PawnClass && PawnClass->IsChildOf(ExpectedPawnClass));
			}

			const bool bHasCamera = Pawn && Pawn->FindComponentByClass<UCameraComponent>() != nullptr;
			const bool bHasSpringArm = Pawn && Pawn->FindComponentByClass<USpringArmComponent>() != nullptr;
			Content->SetBoolField(TEXT("hasCamera"), bHasCamera);
			Content->SetBoolField(TEXT("hasSpringArm"), bHasSpringArm);

			TArray<UInputComponent*> InputComponents;
			if (Pawn && Pawn->InputComponent)
			{
				InputComponents.Add(Pawn->InputComponent);
			}
			if (PlayerController && PlayerController->InputComponent && !InputComponents.Contains(PlayerController->InputComponent))
			{
				InputComponents.Add(PlayerController->InputComponent);
			}
			Content->SetNumberField(TEXT("inputComponentCount"), InputComponents.Num());

			const FName JumpName(TEXT("Jump"));
			const FName MoveForwardName(TEXT("MoveForward"));
			const FName MoveRightName(TEXT("MoveRight"));
			const FName LookYawName(TEXT("LookYaw"));
			const FName LookPitchName(TEXT("LookPitch"));

			const bool bJumpLegacy = PlayerControlsHasLegacyActionMapping(JumpName);
			const bool bJumpRuntime = PlayerControlsRuntimeHasAction(InputComponents, JumpName);
			const bool bMoveForwardLegacy = PlayerControlsHasLegacyAxisMapping(MoveForwardName);
			const bool bMoveForwardRuntime = PlayerControlsRuntimeHasAxis(InputComponents, MoveForwardName);
			const bool bMoveRightLegacy = PlayerControlsHasLegacyAxisMapping(MoveRightName);
			const bool bMoveRightRuntime = PlayerControlsRuntimeHasAxis(InputComponents, MoveRightName);
			const bool bLookYawLegacy = PlayerControlsHasLegacyAxisMapping(LookYawName);
			const bool bLookYawRuntime = PlayerControlsRuntimeHasAxis(InputComponents, LookYawName);
			const bool bLookPitchLegacy = PlayerControlsHasLegacyAxisMapping(LookPitchName);
			const bool bLookPitchRuntime = PlayerControlsRuntimeHasAxis(InputComponents, LookPitchName);

			TSharedPtr<FJsonObject> Bindings = MakeShared<FJsonObject>();
			Bindings->SetObjectField(TEXT("Jump"), MakePlayerControlsBindingObject(bJumpLegacy, bJumpRuntime));
			Bindings->SetObjectField(TEXT("MoveForward"), MakePlayerControlsBindingObject(bMoveForwardLegacy, bMoveForwardRuntime));
			Bindings->SetObjectField(TEXT("MoveRight"), MakePlayerControlsBindingObject(bMoveRightLegacy, bMoveRightRuntime));
			Bindings->SetObjectField(TEXT("LookYaw"), MakePlayerControlsBindingObject(bLookYawLegacy, bLookYawRuntime));
			Bindings->SetObjectField(TEXT("LookPitch"), MakePlayerControlsBindingObject(bLookPitchLegacy, bLookPitchRuntime));
			Content->SetObjectField(TEXT("bindings"), Bindings);

			const bool bJumpExists = bJumpLegacy || bJumpRuntime;
			const bool bMoveForwardExists = bMoveForwardLegacy || bMoveForwardRuntime;
			const bool bMoveRightExists = bMoveRightLegacy || bMoveRightRuntime;
			const bool bLookYawExists = bLookYawLegacy || bLookYawRuntime;
			const bool bLookPitchExists = bLookPitchLegacy || bLookPitchRuntime;
			Content->SetBoolField(TEXT("jumpBindingExists"), bJumpExists);
			Content->SetBoolField(TEXT("moveForwardBindingExists"), bMoveForwardExists);
			Content->SetBoolField(TEXT("moveRightBindingExists"), bMoveRightExists);
			Content->SetBoolField(TEXT("lookYawBindingExists"), bLookYawExists);
			Content->SetBoolField(TEXT("lookPitchBindingExists"), bLookPitchExists);
			Content->SetBoolField(TEXT("moveBindingsExist"), bMoveForwardExists && bMoveRightExists);
			Content->SetBoolField(TEXT("lookBindingsExist"), bLookYawExists && bLookPitchExists);
			Content->SetBoolField(
				TEXT("controlsSetupExists"),
				PlayerController != nullptr
					&& Pawn != nullptr
					&& bHasCamera
					&& bJumpExists
					&& bMoveForwardExists
					&& bMoveRightExists
					&& bLookYawExists
					&& bLookPitchExists);
			return Content;
		}

		FUnrealMcpExecutionResult VerifyPlayerControlsTool(const FJsonObject& Arguments)
		{
			FString ExpectedPawnClassText;
			Arguments.TryGetStringField(TEXT("expectedPawnClass"), ExpectedPawnClassText);

			bool bStartIfNeeded = false;
			Arguments.TryGetBoolField(TEXT("startIfNeeded"), bStartIfNeeded);

			bool bStopAfter = bStartIfNeeded;
			Arguments.TryGetBoolField(TEXT("stopAfter"), bStopAfter);

			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetStringField(TEXT("action"), TEXT("verify_player_controls"));
			Content->SetBoolField(TEXT("startIfNeeded"), bStartIfNeeded);
			Content->SetBoolField(TEXT("stopAfter"), bStopAfter);
			Content->SetStringField(TEXT("expectedPawnClass"), ExpectedPawnClassText.TrimStartAndEnd());
			Content->SetBoolField(TEXT("inputInjectionPerformed"), false);
			Content->SetBoolField(TEXT("movementDeltaChecked"), false);
			Content->SetBoolField(TEXT("movementDeltaMeasured"), false);

			if (!GEditor)
			{
				Content->SetBoolField(TEXT("needsPie"), true);
				Content->SetBoolField(TEXT("canVerifyRuntime"), false);
				Content->SetBoolField(TEXT("beginPieObserved"), false);
				return MakePlayerControlsErrorResult(TEXT("EditorUnavailable"), TEXT("GEditor is unavailable."), Content);
			}

			const bool bPieAlreadyActive = GEditor->PlayWorld != nullptr;
			if (!bPieAlreadyActive && !bStartIfNeeded)
			{
				Content->SetBoolField(TEXT("needsPie"), true);
				Content->SetBoolField(TEXT("canVerifyRuntime"), false);
				Content->SetBoolField(TEXT("beginPieObserved"), false);
				return MakeExecutionResult(TEXT("PIE is required to verify player controls."), Content, false);
			}

			bool bBeginPieObserved = bPieAlreadyActive;
			if (!bPieAlreadyActive)
			{
				FScopedPlayerControlsVerificationLock VerificationLock;
				TSharedPtr<FJsonObject> LockFailure;
				if (!VerificationLock.TryAcquire(LockFailure))
				{
					Content->SetBoolField(TEXT("needsPie"), true);
					Content->SetBoolField(TEXT("canVerifyRuntime"), false);
					Content->SetBoolField(TEXT("beginPieObserved"), false);
					if (LockFailure.IsValid())
					{
						Content->SetObjectField(TEXT("error"), LockFailure);
					}
					return MakeExecutionResult(TEXT("Another verification run is already active."), Content, true);
				}

				const bool bPieRequested = RequestPieForPlayerControls();
				Content->SetBoolField(TEXT("needsPie"), true);
				Content->SetBoolField(TEXT("canVerifyRuntime"), false);
				Content->SetBoolField(TEXT("beginPieObserved"), false);
				Content->SetBoolField(TEXT("pendingPie"), bPieRequested);
				Content->SetBoolField(TEXT("pieRequested"), bPieRequested);
				Content->SetStringField(TEXT("note"), TEXT("PIE start requested; call verify_player_controls again after BeginPIE."));
				return MakeExecutionResult(TEXT("PIE start requested; call verify_player_controls again after BeginPIE."), Content, false);
			}

			TSharedPtr<FJsonObject> RuntimeContent = InspectPlayerControlsRuntime(ExpectedPawnClassText, bBeginPieObserved, true);
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : RuntimeContent->Values)
			{
				Content->SetField(Pair.Key, Pair.Value);
			}
			Content->SetBoolField(TEXT("beginPieObserved"), bBeginPieObserved);
			return MakeExecutionResult(TEXT("Player controls verification completed."), Content, false);
		}

		FUnrealMcpExecutionResult PieInputProbeStartTool(const FJsonObject& Arguments)
		{
			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetStringField(TEXT("action"), TEXT("pie_input_probe"));
			Content->SetStringField(TEXT("status"), TEXT("rejected"));

			FString InputProfile;
			if (!Arguments.TryGetStringField(TEXT("inputProfile"), InputProfile) || InputProfile.TrimStartAndEnd().IsEmpty())
			{
				Content->SetStringField(TEXT("errorKind"), TEXT("MissingInputProfile"));
				return MakeExecutionResult(TEXT("Missing required field 'inputProfile' for action=start."), Content, true);
			}
			InputProfile = InputProfile.TrimStartAndEnd();

			EPieInputProbeProfile ParsedProfile;
			if (!ParsePieInputProbeProfile(InputProfile, ParsedProfile))
			{
				Content->SetStringField(TEXT("errorKind"), TEXT("InvalidInputProfile"));
				Content->SetStringField(TEXT("inputProfile"), InputProfile);
				return MakeExecutionResult(FString::Printf(TEXT("Unsupported inputProfile '%s'."), *InputProfile), Content, true);
			}

			double DurationSeconds = 0.5;
			Arguments.TryGetNumberField(TEXT("durationSeconds"), DurationSeconds);
			DurationSeconds = FMath::Clamp(DurationSeconds, 0.05, 5.0);

			UWorld* PlayWorld = GEditor ? GEditor->PlayWorld : nullptr;
			if (!PlayWorld)
			{
				Content->SetBoolField(TEXT("needsPie"), true);
				Content->SetBoolField(TEXT("canVerifyRuntime"), false);
				Content->SetStringField(TEXT("status"), TEXT("needs_pie"));
				return MakeExecutionResult(TEXT("PIE is required to run an input probe."), Content, false);
			}

			APlayerController* PlayerController = PlayWorld->GetFirstPlayerController();
			APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
			if (!PlayerController || !Pawn)
			{
				Content->SetBoolField(TEXT("needsPie"), false);
				Content->SetBoolField(TEXT("canVerifyRuntime"), false);
				Content->SetStringField(TEXT("errorKind"), TEXT("NoPossessedPawn"));
				return MakeExecutionResult(TEXT("PIE is active, but no possessed pawn was found for player 0."), Content, true);
			}

			FPieInputProbeState State;
			State.ProbeId = FString::Printf(TEXT("pie-input-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12).ToLower());
			State.InputProfile = InputProfile;
			State.PlayerController = PlayerController;
			State.Pawn = Pawn;
			State.StartLocation = Pawn->GetActorLocation();
			State.EndLocation = State.StartLocation;
			State.StartRotation = Pawn->GetActorRotation();
			State.EndRotation = State.StartRotation;
			State.DurationSeconds = DurationSeconds;

			{
				FScopeLock Lock(&GPieInputProbeMutex);
				GPieInputProbeStates.Add(State.ProbeId, State);
			}
			EnsurePieInputProbeTicker();

			Content = MakePieInputProbeResultObject(State);
			Content->SetStringField(TEXT("action"), TEXT("pie_input_probe"));
			Content->SetBoolField(TEXT("needsPie"), false);
			Content->SetBoolField(TEXT("canVerifyRuntime"), true);
			return MakeExecutionResult(
				FString::Printf(TEXT("Started PIE input probe '%s'."), *State.ProbeId),
				Content,
				false);
		}

		FUnrealMcpExecutionResult PieInputProbeResultTool(const FJsonObject& Arguments)
		{
			FString ProbeId;
			if (!Arguments.TryGetStringField(TEXT("probeId"), ProbeId) || ProbeId.TrimStartAndEnd().IsEmpty())
			{
				TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
				Content->SetStringField(TEXT("action"), TEXT("pie_input_probe"));
				Content->SetStringField(TEXT("errorKind"), TEXT("MissingProbeId"));
				return MakeExecutionResult(TEXT("Missing required field 'probeId' for action=result."), Content, true);
			}
			ProbeId = ProbeId.TrimStartAndEnd();

			FPieInputProbeState State;
			{
				FScopeLock Lock(&GPieInputProbeMutex);
				const FPieInputProbeState* StoredState = GPieInputProbeStates.Find(ProbeId);
				if (!StoredState)
				{
					TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
					Content->SetStringField(TEXT("action"), TEXT("pie_input_probe"));
					Content->SetStringField(TEXT("probeId"), ProbeId);
					Content->SetStringField(TEXT("errorKind"), TEXT("UnknownProbeId"));
					return MakeExecutionResult(FString::Printf(TEXT("Unknown or expired PIE input probe '%s'."), *ProbeId), Content, true);
				}
				State = *StoredState;
			}

			TSharedPtr<FJsonObject> Content = MakePieInputProbeResultObject(State);
			Content->SetStringField(TEXT("action"), TEXT("pie_input_probe"));
			return MakeExecutionResult(
				State.Status == TEXT("done") ? TEXT("PIE input probe completed.") : TEXT("PIE input probe is still sampling."),
				Content,
				false);
		}

		FUnrealMcpExecutionResult PieInputProbeTool(const FJsonObject& Arguments)
		{
			FString Action;
			if (!Arguments.TryGetStringField(TEXT("action"), Action) || Action.TrimStartAndEnd().IsEmpty())
			{
				TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
				Content->SetStringField(TEXT("action"), TEXT("pie_input_probe"));
				Content->SetStringField(TEXT("errorKind"), TEXT("MissingAction"));
				return MakeExecutionResult(TEXT("Missing required field 'action'."), Content, true);
			}
			Action = Action.TrimStartAndEnd();
			if (Action == TEXT("start"))
			{
				return PieInputProbeStartTool(Arguments);
			}
			if (Action == TEXT("result"))
			{
				return PieInputProbeResultTool(Arguments);
			}

			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetStringField(TEXT("action"), TEXT("pie_input_probe"));
			Content->SetStringField(TEXT("errorKind"), TEXT("InvalidAction"));
			Content->SetStringField(TEXT("requestedAction"), Action);
			return MakeExecutionResult(FString::Printf(TEXT("Unsupported pie_input_probe action '%s'."), *Action), Content, true);
		}

		FUnrealMcpExecutionResult VerifyViewportWidgetsTool(const FJsonObject& Arguments)
		{
			FString WidgetClassFilter;
			Arguments.TryGetStringField(TEXT("widgetClassFilter"), WidgetClassFilter);
			WidgetClassFilter = WidgetClassFilter.TrimStartAndEnd();

			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetStringField(TEXT("action"), TEXT("verify_viewport_widgets"));
			Content->SetStringField(TEXT("widgetClassFilter"), WidgetClassFilter);

			UWorld* PlayWorld = GEditor ? GEditor->PlayWorld : nullptr;
			if (!PlayWorld)
			{
				Content->SetBoolField(TEXT("needsPie"), true);
				Content->SetNumberField(TEXT("count"), 0);
				Content->SetArrayField(TEXT("widgets"), TArray<TSharedPtr<FJsonValue>>());
				return MakeExecutionResult(TEXT("PIE is required to verify viewport widgets."), Content, false);
			}

			TArray<TSharedPtr<FJsonValue>> WidgetValues;
			for (TObjectIterator<UUserWidget> WidgetIt; WidgetIt; ++WidgetIt)
			{
				UUserWidget* Widget = *WidgetIt;
				if (!Widget || Widget->HasAnyFlags(RF_ClassDefaultObject) || Widget->GetWorld() != PlayWorld || !Widget->IsInViewport())
				{
					continue;
				}

				UClass* WidgetClass = Widget->GetClass();
				const FString ClassPath = WidgetClass ? WidgetClass->GetPathName() : FString();
				if (!WidgetClassFilter.IsEmpty() && ClassPath != WidgetClassFilter)
				{
					continue;
				}

				TSharedPtr<FJsonObject> WidgetObject = MakeShared<FJsonObject>();
				WidgetObject->SetStringField(TEXT("name"), Widget->GetName());
				WidgetObject->SetStringField(TEXT("class"), ClassPath);
				WidgetValues.Add(MakeShared<FJsonValueObject>(WidgetObject));
			}

			Content->SetBoolField(TEXT("needsPie"), false);
			Content->SetNumberField(TEXT("count"), WidgetValues.Num());
			Content->SetArrayField(TEXT("widgets"), WidgetValues);
			return MakeExecutionResult(TEXT("Viewport widget verification completed."), Content, false);
		}

		FUnrealMcpExecutionResult PieSmokeTool(const FJsonObject& Arguments)
		{
			int32 TimeoutSeconds = DefaultPieSmokeTimeoutSeconds;
			int32 AliveWindowSeconds = DefaultPieSmokeAliveWindowSeconds;
			FString ArgumentErrorKind;
			if (!ParsePieSmokeArguments(Arguments, TimeoutSeconds, AliveWindowSeconds, ArgumentErrorKind))
			{
				return MakePieSmokeErrorResult(TEXT("InvalidArguments"), TEXT("aliveWindowSeconds must be less than timeoutSeconds after clamping."));
			}

			FPieSmokeMapResolution MapResolution;
			FString MapPath;
			FString MapErrorKind;
			FString MapErrorMessage;
			if (Arguments.TryGetStringField(TEXT("mapPath"), MapPath)
				&& !ResolvePieSmokeMapPath(MapPath, MapResolution, MapErrorKind, MapErrorMessage))
			{
				return MakePieSmokeErrorResult(MapErrorKind, MapErrorMessage);
			}

			FUnrealMcpAutomationRunState ActiveRun;
			if (TryGetActiveAutomationRunState(ActiveRun))
			{
				const FString Message = FString::Printf(TEXT("Run '%s' (%s) is still %s."), *ActiveRun.RunId, *LexToString(ActiveRun.RunType), *ActiveRun.Status);
				TSharedPtr<FJsonObject> ErrorObject = MakePieSmokeErrorObject(TEXT("RunAlreadyActive"), Message);
				ErrorObject->SetStringField(TEXT("activeRunId"), ActiveRun.RunId);
				ErrorObject->SetStringField(TEXT("activeRunType"), LexToString(ActiveRun.RunType));
				return MakeExecutionResult(Message, ErrorObject, true);
			}

			if (HasDirtyEditorMapConflict(MapResolution))
			{
				return MakePieSmokeErrorResult(
					TEXT("EditorMapDirty"),
					TEXT("The current editor map is dirty and differs from mapPath; resolve dirty state before PIE smoke opens another map."));
			}

			const FString RunId = MakeAutomationRunId();
			const FDateTime AcceptedAtUtc = FDateTime::UtcNow();
			FUnrealMcpAutomationRunState SharedState;
			SharedState.RunId = RunId;
			SharedState.RunType = EUnrealMcpAutomationRunType::PieSmoke;
			SharedState.Status = TEXT("queued");
			SharedState.AcceptedAtUtc = AcceptedAtUtc;
			SharedState.StartedAtUtc = AcceptedAtUtc;
			SharedState.LastHeartbeatUtc = AcceptedAtUtc;
			SharedState.TimeoutSeconds = TimeoutSeconds;
			SetActiveAutomationRunState(SharedState);
			const FDateTime StartedAtUtc = FDateTime::UtcNow();
			SharedState.StartedAtUtc = StartedAtUtc;
			SharedState.LastHeartbeatUtc = StartedAtUtc;

			FPieSmokeRunState PieState;
			PieState.SharedState = SharedState;
			PieState.Map = MapResolution;
			PieState.AliveWindowSeconds = AliveWindowSeconds;
			PieState.DirtyAtStart = SnapshotDirtyPackages();

			SaveAutomationRunStateFile(SharedState);
			SetActiveAutomationRunState(SharedState);
			{
				FScopeLock Lock(&GPieSmokeMutex);
				GActivePieSmokeRun = PieState;
				bHasActivePieSmokeRun = true;
			}
			EnsurePieSmokeTicker();

			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetStringField(TEXT("runId"), SharedState.RunId);
			Content->SetStringField(TEXT("acceptedAt"), SharedState.AcceptedAtUtc.ToIso8601());
			if (MapResolution.bHasMap)
			{
				Content->SetStringField(TEXT("matchedMap"), MapResolution.ObjectPath);
			}
			Content->SetStringField(TEXT("initialStatus"), SharedState.Status);
			Content->SetStringField(TEXT("reportPath"), MakeAutomationReportRelativePath(SharedState.RunId));
			Content->SetStringField(TEXT("pollingHint"), TEXT("Poll automation_report every 1-2 seconds; PIE smoke typically completes in 10-30 seconds."));
			return MakeExecutionResult(
				FString::Printf(TEXT("Queued PIE smoke run '%s'."), *SharedState.RunId),
				Content,
				false);
		}
	}

	bool TryExecutePieSmokeTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult)
	{
		if (ToolName == TEXT("unreal.pie_smoke"))
		{
			OutResult = PieSmokeTool(Arguments);
			return true;
		}
		if (ToolName == TEXT("unreal.verify_player_controls"))
		{
			OutResult = VerifyPlayerControlsTool(Arguments);
			return true;
		}
		if (ToolName == TEXT("unreal.pie_input_probe"))
		{
			OutResult = PieInputProbeTool(Arguments);
			return true;
		}
		if (ToolName == TEXT("unreal.verify_viewport_widgets"))
		{
			OutResult = VerifyViewportWidgetsTool(Arguments);
			return true;
		}
		return false;
	}

	void MarkActivePieSmokeStaleOnShutdown()
	{
		FString RunId;
		{
			FScopeLock Lock(&GPieSmokeMutex);
			if (bHasActivePieSmokeRun)
			{
				RunId = GActivePieSmokeRun.SharedState.RunId;
			}
		}

		if (!RunId.IsEmpty())
		{
			MarkPieSmokeRunStale(RunId, TEXT("editor_shutdown"));
		}

		if (GPieSmokeTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GPieSmokeTickerHandle);
			GPieSmokeTickerHandle.Reset();
		}
		ResetPieInputProbeState();
	}

	bool MarkActivePieSmokeStaleFromAutomationLock(const FString& RunId, const FString& StaleReason)
	{
		return MarkPieSmokeRunStale(RunId, StaleReason);
	}

#if WITH_DEV_AUTOMATION_TESTS
	void ResetPieSmokeToolStateForTests()
	{
		FScopeLock Lock(&GPieSmokeMutex);
		if (bHasActivePieSmokeRun)
		{
			UnregisterPieSmokeDelegates(GActivePieSmokeRun);
		}
		bHasActivePieSmokeRun = false;
		GActivePieSmokeRun = FPieSmokeRunState();
		if (GPieSmokeTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GPieSmokeTickerHandle);
			GPieSmokeTickerHandle.Reset();
		}
		ResetPieInputProbeState();
	}

	bool ParsePieSmokeArgumentsForTests(const FJsonObject& Arguments, int32& OutTimeoutSeconds, int32& OutAliveWindowSeconds, FString& OutErrorKind)
	{
		return ParsePieSmokeArguments(Arguments, OutTimeoutSeconds, OutAliveWindowSeconds, OutErrorKind);
	}

	bool ValidatePieSmokeMapPathForTests(const FString& MapPath, FString& OutMatchedMap, FString& OutErrorKind, FString& OutMessage)
	{
		FPieSmokeMapResolution Resolution;
		const bool bValid = ResolvePieSmokeMapPath(MapPath, Resolution, OutErrorKind, OutMessage);
		OutMatchedMap = Resolution.ObjectPath;
		return bValid;
	}

	void SetVerifyPlayerControlsSuppressPieRequestForTests(bool bSuppress)
	{
		bSuppressPlayerControlsPieRequestForTests = bSuppress;
	}

	void CreateActivePieSmokeRunWithDelegatesForTests(const FString& RunId)
	{
		ResetPieSmokeToolStateForTests();

		FUnrealMcpAutomationRunState SharedState;
		SharedState.RunId = RunId;
		SharedState.RunType = EUnrealMcpAutomationRunType::PieSmoke;
		SharedState.Status = TEXT("running");
		SharedState.AcceptedAtUtc = FDateTime::UtcNow();
		SharedState.StartedAtUtc = SharedState.AcceptedAtUtc;
		SharedState.LastHeartbeatUtc = SharedState.AcceptedAtUtc;
		SharedState.TimeoutSeconds = DefaultPieSmokeTimeoutSeconds;

		FPieSmokeRunState State;
		State.SharedState = SharedState;
		State.DirtyAtStart = SnapshotDirtyPackages();
		RegisterPieSmokeDelegates(State);
		{
			FScopeLock Lock(&GPieSmokeMutex);
			GActivePieSmokeRun = State;
			bHasActivePieSmokeRun = true;
		}
		SetActiveAutomationRunState(SharedState);
		SaveAutomationRunStateFile(SharedState);
	}

	bool ArePieSmokeDelegatesRegisteredForTests()
	{
		FScopeLock Lock(&GPieSmokeMutex);
		return bHasActivePieSmokeRun
			&& (GActivePieSmokeRun.BeginPieHandle.IsValid() || GActivePieSmokeRun.EndPieHandle.IsValid());
	}

	bool MarkActivePieSmokeStaleForTests(const FString& RunId, const FString& StaleReason)
	{
		return MarkPieSmokeRunStale(RunId, StaleReason);
	}
#endif
}
