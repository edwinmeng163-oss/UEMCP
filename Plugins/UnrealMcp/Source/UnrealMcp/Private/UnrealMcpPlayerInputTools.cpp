#include "UnrealMcpEditorTools.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerInput.h"
#include "InputCoreTypes.h"
#include "Misc/PackageName.h"
#include "UnrealMcpModule.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

namespace UnrealMcp
{
	FUnrealMcpExecutionResult MakeExecutionResult(const FString& Text, const TSharedPtr<FJsonObject>& StructuredContent, bool bIsError);

	namespace
	{
		enum class EPlayerInputSystem : uint8
		{
			Auto,
			Legacy,
			Enhanced
		};

		enum class EPlayerInputMappingKind : uint8
		{
			Axis,
			Action
		};

		struct FPlayerInputKeySpec
		{
			FString KeyName;
			FKey Key;
			float Scale = 1.0f;
		};

		struct FPlayerInputMappingSpec
		{
			FString Name;
			EPlayerInputMappingKind Kind = EPlayerInputMappingKind::Axis;
			FString InputActionPath;
			TArray<FPlayerInputKeySpec> Keys;
		};

		struct FEnhancedMappingsAccess
		{
			FArrayProperty* ArrayProperty = nullptr;
			void* ArrayContainerPtr = nullptr;
			FStructProperty* MappingStructProperty = nullptr;
			FObjectPropertyBase* ActionProperty = nullptr;
			FStructProperty* KeyProperty = nullptr;
			FString StoragePath;

			bool IsValid() const
			{
				return ArrayProperty != nullptr
					&& ArrayContainerPtr != nullptr
					&& MappingStructProperty != nullptr
					&& ActionProperty != nullptr
					&& KeyProperty != nullptr;
			}
		};

		TSharedPtr<FJsonObject> MakePlayerInputErrorObject(const FString& Code, const FString& Message)
		{
			TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
			ErrorObject->SetStringField(TEXT("code"), Code);
			ErrorObject->SetStringField(TEXT("message"), Message);
			return ErrorObject;
		}

		TArray<TSharedPtr<FJsonValue>> MakePlayerInputStringValues(const TArray<FString>& Values)
		{
			TArray<TSharedPtr<FJsonValue>> JsonValues;
			for (const FString& Value : Values)
			{
				JsonValues.Add(MakeShared<FJsonValueString>(Value));
			}
			return JsonValues;
		}

		EPlayerInputSystem ParsePlayerInputSystem(const FString& RawValue)
		{
			if (RawValue.Equals(TEXT("legacy"), ESearchCase::IgnoreCase))
			{
				return EPlayerInputSystem::Legacy;
			}
			if (RawValue.Equals(TEXT("enhanced"), ESearchCase::IgnoreCase))
			{
				return EPlayerInputSystem::Enhanced;
			}
			return EPlayerInputSystem::Auto;
		}

		FString LexToString(EPlayerInputSystem System)
		{
			switch (System)
			{
			case EPlayerInputSystem::Legacy:
				return TEXT("legacy");
			case EPlayerInputSystem::Enhanced:
				return TEXT("enhanced");
			case EPlayerInputSystem::Auto:
			default:
				return TEXT("auto");
			}
		}

		FString LexToString(EPlayerInputMappingKind Kind)
		{
			return Kind == EPlayerInputMappingKind::Action ? TEXT("action") : TEXT("axis");
		}

		FKey ResolvePlayerInputKey(const FString& RawKeyName, FString& OutNormalizedKeyName)
		{
			const FString Trimmed = RawKeyName.TrimStartAndEnd();
			if (Trimmed.Equals(TEXT("Space"), ESearchCase::IgnoreCase)
				|| Trimmed.Equals(TEXT("SpaceBar"), ESearchCase::IgnoreCase)
				|| Trimmed.Equals(TEXT("Space Bar"), ESearchCase::IgnoreCase))
			{
				OutNormalizedKeyName = EKeys::SpaceBar.ToString();
				return EKeys::SpaceBar;
			}
			if (Trimmed.Equals(TEXT("MouseX"), ESearchCase::IgnoreCase)
				|| Trimmed.Equals(TEXT("Mouse X"), ESearchCase::IgnoreCase))
			{
				OutNormalizedKeyName = EKeys::MouseX.ToString();
				return EKeys::MouseX;
			}
			if (Trimmed.Equals(TEXT("MouseY"), ESearchCase::IgnoreCase)
				|| Trimmed.Equals(TEXT("Mouse Y"), ESearchCase::IgnoreCase))
			{
				OutNormalizedKeyName = EKeys::MouseY.ToString();
				return EKeys::MouseY;
			}

			const FKey Key(*Trimmed);
			OutNormalizedKeyName = Key.ToString();
			return Key;
		}

		bool AddKeySpec(FPlayerInputMappingSpec& Mapping, const FString& RawKeyName, double Scale)
		{
			FString NormalizedKeyName;
			const FKey Key = ResolvePlayerInputKey(RawKeyName, NormalizedKeyName);
			if (!Key.IsValid())
			{
				return false;
			}

			FPlayerInputKeySpec KeySpec;
			KeySpec.KeyName = NormalizedKeyName;
			KeySpec.Key = Key;
			KeySpec.Scale = static_cast<float>(Scale);
			Mapping.Keys.Add(KeySpec);
			return true;
		}

		FPlayerInputMappingSpec MakeDefaultMapping(const FString& Name, EPlayerInputMappingKind Kind)
		{
			FPlayerInputMappingSpec Mapping;
			Mapping.Name = Name;
			Mapping.Kind = Kind;
			return Mapping;
		}

		TArray<FPlayerInputMappingSpec> MakeThirdPersonBasicMappings()
		{
			TArray<FPlayerInputMappingSpec> Mappings;

			FPlayerInputMappingSpec MoveForward = MakeDefaultMapping(TEXT("MoveForward"), EPlayerInputMappingKind::Axis);
			AddKeySpec(MoveForward, TEXT("W"), 1.0);
			AddKeySpec(MoveForward, TEXT("S"), -1.0);
			Mappings.Add(MoveForward);

			FPlayerInputMappingSpec MoveRight = MakeDefaultMapping(TEXT("MoveRight"), EPlayerInputMappingKind::Axis);
			AddKeySpec(MoveRight, TEXT("D"), 1.0);
			AddKeySpec(MoveRight, TEXT("A"), -1.0);
			Mappings.Add(MoveRight);

			FPlayerInputMappingSpec LookYaw = MakeDefaultMapping(TEXT("LookYaw"), EPlayerInputMappingKind::Axis);
			AddKeySpec(LookYaw, TEXT("MouseX"), 1.0);
			Mappings.Add(LookYaw);

			FPlayerInputMappingSpec LookPitch = MakeDefaultMapping(TEXT("LookPitch"), EPlayerInputMappingKind::Axis);
			AddKeySpec(LookPitch, TEXT("MouseY"), -1.0);
			Mappings.Add(LookPitch);

			FPlayerInputMappingSpec Jump = MakeDefaultMapping(TEXT("Jump"), EPlayerInputMappingKind::Action);
			AddKeySpec(Jump, TEXT("SpaceBar"), 1.0);
			Mappings.Add(Jump);

			return Mappings;
		}

		FPlayerInputMappingSpec* FindMappingByName(TArray<FPlayerInputMappingSpec>& Mappings, const FString& Name)
		{
			for (FPlayerInputMappingSpec& Mapping : Mappings)
			{
				if (Mapping.Name.Equals(Name, ESearchCase::CaseSensitive))
				{
					return &Mapping;
				}
			}
			return nullptr;
		}

		EPlayerInputMappingKind MappingKindForName(const FString& Name)
		{
			return Name.Equals(TEXT("Jump"), ESearchCase::CaseSensitive)
				? EPlayerInputMappingKind::Action
				: EPlayerInputMappingKind::Axis;
		}

		void ParseCustomMappingObject(const FString& MappingName, const TSharedPtr<FJsonObject>& MappingObject, TArray<FPlayerInputMappingSpec>& Mappings)
		{
			if (!MappingObject.IsValid())
			{
				return;
			}

			FPlayerInputMappingSpec ParsedMapping = MakeDefaultMapping(MappingName, MappingKindForName(MappingName));
			MappingObject->TryGetStringField(TEXT("inputActionPath"), ParsedMapping.InputActionPath);
			ParsedMapping.InputActionPath = ParsedMapping.InputActionPath.TrimStartAndEnd();

			const TArray<TSharedPtr<FJsonValue>>* KeyValues = nullptr;
			if (MappingObject->TryGetArrayField(TEXT("keys"), KeyValues) && KeyValues != nullptr)
			{
				for (const TSharedPtr<FJsonValue>& KeyValue : *KeyValues)
				{
					if (!KeyValue.IsValid())
					{
						continue;
					}

					if (KeyValue->Type == EJson::String)
					{
						AddKeySpec(ParsedMapping, KeyValue->AsString(), 1.0);
						continue;
					}

					if (KeyValue->Type == EJson::Object && KeyValue->AsObject().IsValid())
					{
						FString KeyName;
						if (!KeyValue->AsObject()->TryGetStringField(TEXT("key"), KeyName))
						{
							continue;
						}
						double Scale = 1.0;
						KeyValue->AsObject()->TryGetNumberField(TEXT("scale"), Scale);
						AddKeySpec(ParsedMapping, KeyName, Scale);
					}
				}
			}

			if (ParsedMapping.Keys.IsEmpty() && ParsedMapping.InputActionPath.IsEmpty())
			{
				return;
			}

			if (FPlayerInputMappingSpec* Existing = FindMappingByName(Mappings, MappingName))
			{
				if (!ParsedMapping.InputActionPath.IsEmpty())
				{
					Existing->InputActionPath = ParsedMapping.InputActionPath;
				}
				if (!ParsedMapping.Keys.IsEmpty())
				{
					Existing->Keys = ParsedMapping.Keys;
				}
			}
			else
			{
				Mappings.Add(ParsedMapping);
			}
		}

		TArray<FPlayerInputMappingSpec> BuildRequestedMappings(const FJsonObject& Arguments)
		{
			FString Profile;
			Arguments.TryGetStringField(TEXT("profile"), Profile);
			Profile = Profile.TrimStartAndEnd();

			TArray<FPlayerInputMappingSpec> Mappings = Profile.Equals(TEXT("custom"), ESearchCase::IgnoreCase)
				? TArray<FPlayerInputMappingSpec>()
				: MakeThirdPersonBasicMappings();

			const TSharedPtr<FJsonObject>* MappingsObject = nullptr;
			if (Arguments.TryGetObjectField(TEXT("mappings"), MappingsObject) && MappingsObject && (*MappingsObject).IsValid())
			{
				static const TArray<FString> KnownMappingNames = {
					TEXT("MoveForward"),
					TEXT("MoveRight"),
					TEXT("LookYaw"),
					TEXT("LookPitch"),
					TEXT("Jump")
				};
				for (const FString& MappingName : KnownMappingNames)
				{
					const TSharedPtr<FJsonObject>* MappingObject = nullptr;
					if ((*MappingsObject)->TryGetObjectField(MappingName, MappingObject) && MappingObject && (*MappingObject).IsValid())
					{
						ParseCustomMappingObject(MappingName, *MappingObject, Mappings);
					}
				}
			}

			return Mappings;
		}

		TSharedPtr<FJsonObject> MakeKeySpecObject(const FPlayerInputKeySpec& KeySpec)
		{
			TSharedPtr<FJsonObject> KeyObject = MakeShared<FJsonObject>();
			KeyObject->SetStringField(TEXT("key"), KeySpec.KeyName);
			KeyObject->SetNumberField(TEXT("scale"), KeySpec.Scale);
			KeyObject->SetBoolField(TEXT("valid"), KeySpec.Key.IsValid());
			return KeyObject;
		}

		TSharedPtr<FJsonObject> MakeMappingSpecObject(const FPlayerInputMappingSpec& Mapping)
		{
			TSharedPtr<FJsonObject> MappingObject = MakeShared<FJsonObject>();
			MappingObject->SetStringField(TEXT("name"), Mapping.Name);
			MappingObject->SetStringField(TEXT("kind"), LexToString(Mapping.Kind));
			if (!Mapping.InputActionPath.IsEmpty())
			{
				MappingObject->SetStringField(TEXT("inputActionPath"), Mapping.InputActionPath);
			}

			TArray<TSharedPtr<FJsonValue>> KeyValues;
			for (const FPlayerInputKeySpec& KeySpec : Mapping.Keys)
			{
				KeyValues.Add(MakeShared<FJsonValueObject>(MakeKeySpecObject(KeySpec)));
			}
			MappingObject->SetArrayField(TEXT("keys"), KeyValues);
			return MappingObject;
		}

		TArray<TSharedPtr<FJsonValue>> MakeMappingSpecValues(const TArray<FPlayerInputMappingSpec>& Mappings)
		{
			TArray<TSharedPtr<FJsonValue>> Values;
			for (const FPlayerInputMappingSpec& Mapping : Mappings)
			{
				Values.Add(MakeShared<FJsonValueObject>(MakeMappingSpecObject(Mapping)));
			}
			return Values;
		}

		bool LegacyAxisMappingExists(UInputSettings* InputSettings, const FPlayerInputMappingSpec& Mapping, const FPlayerInputKeySpec& KeySpec)
		{
			if (!InputSettings)
			{
				return false;
			}

			TArray<FInputAxisKeyMapping> ExistingMappings;
			InputSettings->GetAxisMappingByName(FName(*Mapping.Name), ExistingMappings);
			for (const FInputAxisKeyMapping& ExistingMapping : ExistingMappings)
			{
				if (ExistingMapping.Key == KeySpec.Key && FMath::IsNearlyEqual(ExistingMapping.Scale, KeySpec.Scale))
				{
					return true;
				}
			}
			return false;
		}

		bool LegacyActionMappingExists(UInputSettings* InputSettings, const FPlayerInputMappingSpec& Mapping, const FPlayerInputKeySpec& KeySpec)
		{
			if (!InputSettings)
			{
				return false;
			}

			TArray<FInputActionKeyMapping> ExistingMappings;
			InputSettings->GetActionMappingByName(FName(*Mapping.Name), ExistingMappings);
			for (const FInputActionKeyMapping& ExistingMapping : ExistingMappings)
			{
				if (ExistingMapping.Key == KeySpec.Key)
				{
					return true;
				}
			}
			return false;
		}

		bool LegacyMappingExists(UInputSettings* InputSettings, const FPlayerInputMappingSpec& Mapping, const FPlayerInputKeySpec& KeySpec)
		{
			return Mapping.Kind == EPlayerInputMappingKind::Action
				? LegacyActionMappingExists(InputSettings, Mapping, KeySpec)
				: LegacyAxisMappingExists(InputSettings, Mapping, KeySpec);
		}

		TSharedPtr<FJsonObject> ConfigureLegacyPlayerInput(const TArray<FPlayerInputMappingSpec>& Mappings, bool bDryRun, bool& bOutWouldWrite)
		{
			bOutWouldWrite = false;

			TSharedPtr<FJsonObject> LegacyObject = MakeShared<FJsonObject>();
			LegacyObject->SetBoolField(TEXT("available"), true);
			LegacyObject->SetBoolField(TEXT("dryRun"), bDryRun);

			UInputSettings* InputSettings = GetMutableDefault<UInputSettings>();
			if (!InputSettings)
			{
				LegacyObject->SetBoolField(TEXT("available"), false);
				LegacyObject->SetObjectField(TEXT("error"), MakePlayerInputErrorObject(TEXT("INPUT_SETTINGS_UNAVAILABLE"), TEXT("UInputSettings is unavailable.")));
				return LegacyObject;
			}

			TArray<TSharedPtr<FJsonValue>> ResultValues;
			int32 ExistingCount = 0;
			int32 AddedCount = 0;
			int32 WouldAddCount = 0;
			for (const FPlayerInputMappingSpec& Mapping : Mappings)
			{
				for (const FPlayerInputKeySpec& KeySpec : Mapping.Keys)
				{
					TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
					ResultObject->SetStringField(TEXT("name"), Mapping.Name);
					ResultObject->SetStringField(TEXT("kind"), LexToString(Mapping.Kind));
					ResultObject->SetObjectField(TEXT("key"), MakeKeySpecObject(KeySpec));

					const bool bExists = LegacyMappingExists(InputSettings, Mapping, KeySpec);
					ResultObject->SetBoolField(TEXT("existsBefore"), bExists);
					if (bExists)
					{
						++ExistingCount;
						ResultObject->SetStringField(TEXT("status"), TEXT("exists"));
					}
					else if (bDryRun)
					{
						++WouldAddCount;
						bOutWouldWrite = true;
						ResultObject->SetStringField(TEXT("status"), TEXT("would_add"));
					}
					else
					{
						InputSettings->Modify();
						if (Mapping.Kind == EPlayerInputMappingKind::Action)
						{
							InputSettings->AddActionMapping(FInputActionKeyMapping(FName(*Mapping.Name), KeySpec.Key), false);
						}
						else
						{
							InputSettings->AddAxisMapping(FInputAxisKeyMapping(FName(*Mapping.Name), KeySpec.Key, KeySpec.Scale), false);
						}
						++AddedCount;
						bOutWouldWrite = true;
						ResultObject->SetStringField(TEXT("status"), TEXT("added"));
					}

					ResultObject->SetBoolField(TEXT("existsAfter"), bExists || !bDryRun);
					ResultValues.Add(MakeShared<FJsonValueObject>(ResultObject));
				}
			}

			if (!bDryRun && AddedCount > 0)
			{
				InputSettings->SaveKeyMappings();
				InputSettings->ForceRebuildKeymaps();
				InputSettings->SaveConfig();
			}

			LegacyObject->SetNumberField(TEXT("existingCount"), ExistingCount);
			LegacyObject->SetNumberField(TEXT("addedCount"), AddedCount);
			LegacyObject->SetNumberField(TEXT("wouldAddCount"), WouldAddCount);
			LegacyObject->SetBoolField(TEXT("wouldWrite"), bDryRun ? WouldAddCount > 0 : AddedCount > 0);
			LegacyObject->SetArrayField(TEXT("mappings"), ResultValues);
			return LegacyObject;
		}

		UClass* LoadEnhancedClass(const TCHAR* ClassPath)
		{
			if (UClass* ExistingClass = FindObject<UClass>(nullptr, ClassPath))
			{
				return ExistingClass;
			}
			return LoadObject<UClass>(nullptr, ClassPath);
		}

		FString NormalizePlayerInputObjectPath(const FString& RawPath)
		{
			FString Path = RawPath.TrimStartAndEnd();
			if (Path.IsEmpty() || Path.Contains(TEXT("."), ESearchCase::CaseSensitive))
			{
				return Path;
			}
			if (Path.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive)
				|| Path.StartsWith(TEXT("/Engine/"), ESearchCase::CaseSensitive))
			{
				return FString::Printf(TEXT("%s.%s"), *Path, *FPackageName::GetLongPackageAssetName(Path));
			}
			return Path;
		}

		bool ResolveEnhancedMappingsAccess(UObject* MappingContext, FEnhancedMappingsAccess& OutAccess)
		{
			OutAccess = FEnhancedMappingsAccess();
			if (!MappingContext)
			{
				return false;
			}

			UClass* ContextClass = MappingContext->GetClass();
			if (FStructProperty* DefaultKeyMappingsProperty = FindFProperty<FStructProperty>(ContextClass, TEXT("DefaultKeyMappings")))
			{
				if (FArrayProperty* NestedMappingsProperty = FindFProperty<FArrayProperty>(DefaultKeyMappingsProperty->Struct, TEXT("Mappings")))
				{
					OutAccess.ArrayProperty = NestedMappingsProperty;
					OutAccess.ArrayContainerPtr = DefaultKeyMappingsProperty->ContainerPtrToValuePtr<void>(MappingContext);
					OutAccess.StoragePath = TEXT("DefaultKeyMappings.Mappings");
				}
			}

			if (!OutAccess.ArrayProperty)
			{
				if (FArrayProperty* DirectMappingsProperty = FindFProperty<FArrayProperty>(ContextClass, TEXT("Mappings")))
				{
					OutAccess.ArrayProperty = DirectMappingsProperty;
					OutAccess.ArrayContainerPtr = MappingContext;
					OutAccess.StoragePath = TEXT("Mappings");
				}
			}

			if (!OutAccess.ArrayProperty || !OutAccess.ArrayProperty->Inner)
			{
				return false;
			}

			OutAccess.MappingStructProperty = CastField<FStructProperty>(OutAccess.ArrayProperty->Inner);
			if (!OutAccess.MappingStructProperty || !OutAccess.MappingStructProperty->Struct)
			{
				return false;
			}

			OutAccess.ActionProperty = CastField<FObjectPropertyBase>(FindFProperty<FProperty>(OutAccess.MappingStructProperty->Struct, TEXT("Action")));
			OutAccess.KeyProperty = FindFProperty<FStructProperty>(OutAccess.MappingStructProperty->Struct, TEXT("Key"));
			return OutAccess.IsValid();
		}

		UObject* GetEnhancedMappingAction(const FEnhancedMappingsAccess& Access, void* MappingPtr)
		{
			if (!Access.ActionProperty || !MappingPtr)
			{
				return nullptr;
			}
			return Access.ActionProperty->GetObjectPropertyValue(Access.ActionProperty->ContainerPtrToValuePtr<void>(MappingPtr));
		}

		FKey GetEnhancedMappingKey(const FEnhancedMappingsAccess& Access, void* MappingPtr)
		{
			if (!Access.KeyProperty || !MappingPtr)
			{
				return EKeys::Invalid;
			}

			const FKey* KeyPtr = Access.KeyProperty->ContainerPtrToValuePtr<FKey>(MappingPtr);
			return KeyPtr ? *KeyPtr : EKeys::Invalid;
		}

		bool EnhancedMappingExists(const FEnhancedMappingsAccess& Access, UObject* ActionObject, const FKey& Key)
		{
			if (!Access.IsValid() || !ActionObject || !Key.IsValid())
			{
				return false;
			}

			FScriptArrayHelper Helper(Access.ArrayProperty, Access.ArrayContainerPtr);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				void* MappingPtr = Helper.GetRawPtr(Index);
				if (GetEnhancedMappingAction(Access, MappingPtr) == ActionObject && GetEnhancedMappingKey(Access, MappingPtr) == Key)
				{
					return true;
				}
			}
			return false;
		}

		UObject* FindEnhancedActionInContext(const FEnhancedMappingsAccess& Access, const FString& MappingName)
		{
			if (!Access.IsValid())
			{
				return nullptr;
			}

			FScriptArrayHelper Helper(Access.ArrayProperty, Access.ArrayContainerPtr);
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				UObject* Action = GetEnhancedMappingAction(Access, Helper.GetRawPtr(Index));
				if (!Action)
				{
					continue;
				}
				if (Action->GetName().Contains(MappingName, ESearchCase::IgnoreCase)
					|| Action->GetPathName().Contains(MappingName, ESearchCase::IgnoreCase))
				{
					return Action;
				}
			}
			return nullptr;
		}

		void AddEnhancedMapping(const FEnhancedMappingsAccess& Access, UObject* ActionObject, const FKey& Key)
		{
			FScriptArrayHelper Helper(Access.ArrayProperty, Access.ArrayContainerPtr);
			const int32 NewIndex = Helper.AddValue();
			void* MappingPtr = Helper.GetRawPtr(NewIndex);
			Access.ActionProperty->SetObjectPropertyValue(Access.ActionProperty->ContainerPtrToValuePtr<void>(MappingPtr), ActionObject);
			Access.KeyProperty->CopyCompleteValue(Access.KeyProperty->ContainerPtrToValuePtr<void>(MappingPtr), &Key);
		}

		UObject* LoadEnhancedActionObject(const FString& RawPath, UClass* InputActionClass)
		{
			if (RawPath.TrimStartAndEnd().IsEmpty() || !InputActionClass)
			{
				return nullptr;
			}

			UObject* ActionObject = LoadObject<UObject>(nullptr, *NormalizePlayerInputObjectPath(RawPath));
			return ActionObject && ActionObject->IsA(InputActionClass) ? ActionObject : nullptr;
		}

		TSharedPtr<FJsonObject> ConfigureEnhancedPlayerInput(
			const TArray<FPlayerInputMappingSpec>& Mappings,
			const FString& MappingContextPath,
			bool bDryRun,
			bool& bOutWouldWrite)
		{
			bOutWouldWrite = false;

			TSharedPtr<FJsonObject> EnhancedObject = MakeShared<FJsonObject>();
			EnhancedObject->SetBoolField(TEXT("dryRun"), bDryRun);
			EnhancedObject->SetStringField(TEXT("mappingContextPath"), MappingContextPath);

			UClass* MappingContextClass = LoadEnhancedClass(TEXT("/Script/EnhancedInput.InputMappingContext"));
			UClass* InputActionClass = LoadEnhancedClass(TEXT("/Script/EnhancedInput.InputAction"));
			EnhancedObject->SetBoolField(TEXT("available"), MappingContextClass != nullptr && InputActionClass != nullptr);
			if (!MappingContextClass || !InputActionClass)
			{
				EnhancedObject->SetStringField(TEXT("diagnostic"), TEXT("Enhanced Input classes are unavailable; enable the EnhancedInput plugin or use inputSystem=legacy."));
				return EnhancedObject;
			}

			if (MappingContextPath.TrimStartAndEnd().IsEmpty())
			{
				EnhancedObject->SetStringField(TEXT("diagnostic"), TEXT("enhancedInputMappingContextPath is required for enhanced mapping writes."));
				return EnhancedObject;
			}

			UObject* MappingContext = LoadObject<UObject>(nullptr, *NormalizePlayerInputObjectPath(MappingContextPath));
			if (!MappingContext || !MappingContext->IsA(MappingContextClass))
			{
				EnhancedObject->SetStringField(TEXT("diagnostic"), TEXT("Mapping context asset was not found or is not an InputMappingContext."));
				return EnhancedObject;
			}

			FEnhancedMappingsAccess Access;
			if (!ResolveEnhancedMappingsAccess(MappingContext, Access))
			{
				EnhancedObject->SetStringField(TEXT("diagnostic"), TEXT("Could not find Enhanced Input mappings storage on the mapping context."));
				return EnhancedObject;
			}

			EnhancedObject->SetStringField(TEXT("resolvedMappingContext"), MappingContext->GetPathName());
			EnhancedObject->SetStringField(TEXT("mappingStorage"), Access.StoragePath);

			TArray<TSharedPtr<FJsonValue>> ResultValues;
			int32 ExistingCount = 0;
			int32 AddedCount = 0;
			int32 WouldAddCount = 0;
			int32 MissingActionCount = 0;

			for (const FPlayerInputMappingSpec& Mapping : Mappings)
			{
				UObject* ActionObject = LoadEnhancedActionObject(Mapping.InputActionPath, InputActionClass);
				if (!ActionObject)
				{
					ActionObject = FindEnhancedActionInContext(Access, Mapping.Name);
				}

				for (const FPlayerInputKeySpec& KeySpec : Mapping.Keys)
				{
					TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
					ResultObject->SetStringField(TEXT("name"), Mapping.Name);
					ResultObject->SetStringField(TEXT("kind"), LexToString(Mapping.Kind));
					ResultObject->SetObjectField(TEXT("key"), MakeKeySpecObject(KeySpec));
					ResultObject->SetStringField(TEXT("inputActionPath"), Mapping.InputActionPath);

					if (!ActionObject)
					{
						++MissingActionCount;
						ResultObject->SetStringField(TEXT("status"), TEXT("missing_input_action"));
						ResultObject->SetStringField(TEXT("diagnostic"), TEXT("Provide inputActionPath or seed the mapping context with an action whose asset name contains the mapping name."));
						ResultValues.Add(MakeShared<FJsonValueObject>(ResultObject));
						continue;
					}

					ResultObject->SetStringField(TEXT("resolvedInputAction"), ActionObject->GetPathName());
					const bool bExists = EnhancedMappingExists(Access, ActionObject, KeySpec.Key);
					ResultObject->SetBoolField(TEXT("existsBefore"), bExists);
					if (bExists)
					{
						++ExistingCount;
						ResultObject->SetStringField(TEXT("status"), TEXT("exists"));
					}
					else if (bDryRun)
					{
						++WouldAddCount;
						bOutWouldWrite = true;
						ResultObject->SetStringField(TEXT("status"), TEXT("would_add"));
					}
					else
					{
						MappingContext->Modify();
						AddEnhancedMapping(Access, ActionObject, KeySpec.Key);
						MappingContext->MarkPackageDirty();
						++AddedCount;
						bOutWouldWrite = true;
						ResultObject->SetStringField(TEXT("status"), TEXT("added"));
					}

					ResultObject->SetBoolField(TEXT("existsAfter"), bExists || !bDryRun);
					ResultValues.Add(MakeShared<FJsonValueObject>(ResultObject));
				}
			}

			EnhancedObject->SetNumberField(TEXT("existingCount"), ExistingCount);
			EnhancedObject->SetNumberField(TEXT("addedCount"), AddedCount);
			EnhancedObject->SetNumberField(TEXT("wouldAddCount"), WouldAddCount);
			EnhancedObject->SetNumberField(TEXT("missingActionCount"), MissingActionCount);
			EnhancedObject->SetBoolField(TEXT("wouldWrite"), bDryRun ? WouldAddCount > 0 : AddedCount > 0);
			EnhancedObject->SetBoolField(TEXT("assetMarkedDirty"), !bDryRun && AddedCount > 0);
			EnhancedObject->SetArrayField(TEXT("mappings"), ResultValues);
			return EnhancedObject;
		}

		TSharedPtr<FJsonObject> MakePlayerInputCapabilityObject(
			EPlayerInputSystem RequestedSystem,
			EPlayerInputSystem EffectiveSystem,
			bool bEnhancedAvailable,
			const FString& MappingContextPath)
		{
			TSharedPtr<FJsonObject> CapabilityObject = MakeShared<FJsonObject>();
			CapabilityObject->SetStringField(TEXT("requestedInputSystem"), LexToString(RequestedSystem));
			CapabilityObject->SetStringField(TEXT("effectiveInputSystem"), LexToString(EffectiveSystem));
			CapabilityObject->SetBoolField(TEXT("legacyAvailable"), GetMutableDefault<UInputSettings>() != nullptr);
			CapabilityObject->SetBoolField(TEXT("enhancedInputAvailable"), bEnhancedAvailable);
			CapabilityObject->SetBoolField(TEXT("hasEnhancedMappingContextPath"), !MappingContextPath.TrimStartAndEnd().IsEmpty());
			return CapabilityObject;
		}

		EPlayerInputSystem ResolveEffectiveInputSystem(
			EPlayerInputSystem RequestedSystem,
			bool bEnhancedAvailable,
			const FString& MappingContextPath)
		{
			if (RequestedSystem == EPlayerInputSystem::Legacy)
			{
				return EPlayerInputSystem::Legacy;
			}
			if (RequestedSystem == EPlayerInputSystem::Enhanced)
			{
				return EPlayerInputSystem::Enhanced;
			}
			return bEnhancedAvailable && !MappingContextPath.TrimStartAndEnd().IsEmpty()
				? EPlayerInputSystem::Enhanced
				: EPlayerInputSystem::Legacy;
		}

		FUnrealMcpExecutionResult ConfigurePlayerInput(const FJsonObject& Arguments)
		{
			FString InputSystemText;
			Arguments.TryGetStringField(TEXT("inputSystem"), InputSystemText);
			const EPlayerInputSystem RequestedSystem = ParsePlayerInputSystem(InputSystemText);

			bool bDryRun = true;
			Arguments.TryGetBoolField(TEXT("dryRun"), bDryRun);

			FString MappingContextPath;
			Arguments.TryGetStringField(TEXT("enhancedInputMappingContextPath"), MappingContextPath);
			MappingContextPath = MappingContextPath.TrimStartAndEnd();

			const TArray<FPlayerInputMappingSpec> Mappings = BuildRequestedMappings(Arguments);
			const bool bEnhancedAvailable =
				LoadEnhancedClass(TEXT("/Script/EnhancedInput.InputMappingContext")) != nullptr
				&& LoadEnhancedClass(TEXT("/Script/EnhancedInput.InputAction")) != nullptr;
			const EPlayerInputSystem EffectiveSystem = ResolveEffectiveInputSystem(RequestedSystem, bEnhancedAvailable, MappingContextPath);

			TSharedPtr<FJsonObject> Content = MakeShared<FJsonObject>();
			Content->SetStringField(TEXT("action"), TEXT("configure_player_input"));
			Content->SetBoolField(TEXT("dryRun"), bDryRun);
			Content->SetStringField(TEXT("inputSystem"), LexToString(EffectiveSystem));
			Content->SetArrayField(TEXT("plannedMappings"), MakeMappingSpecValues(Mappings));
			Content->SetObjectField(TEXT("capability"), MakePlayerInputCapabilityObject(RequestedSystem, EffectiveSystem, bEnhancedAvailable, MappingContextPath));

			if (Mappings.IsEmpty())
			{
				Content->SetBoolField(TEXT("wouldWrite"), false);
				Content->SetObjectField(TEXT("error"), MakePlayerInputErrorObject(TEXT("NO_MAPPINGS"), TEXT("No mappings were supplied by profile or custom mappings.")));
				return MakeExecutionResult(TEXT("No player input mappings were supplied."), Content, true);
			}

			bool bWouldWrite = false;
			if (EffectiveSystem == EPlayerInputSystem::Enhanced)
			{
				TSharedPtr<FJsonObject> EnhancedObject = ConfigureEnhancedPlayerInput(Mappings, MappingContextPath, bDryRun, bWouldWrite);
				Content->SetObjectField(TEXT("enhanced"), EnhancedObject);
				if (RequestedSystem == EPlayerInputSystem::Enhanced && (!bEnhancedAvailable || MappingContextPath.IsEmpty()))
				{
					Content->SetBoolField(TEXT("wouldWrite"), false);
					return MakeExecutionResult(TEXT("Enhanced Input capability report returned without applying mappings."), Content, false);
				}
			}
			else
			{
				TSharedPtr<FJsonObject> LegacyObject = ConfigureLegacyPlayerInput(Mappings, bDryRun, bWouldWrite);
				Content->SetObjectField(TEXT("legacy"), LegacyObject);
			}

			Content->SetBoolField(TEXT("wouldWrite"), bWouldWrite);

			TSharedPtr<FJsonObject> ToolPostcheck = MakeShared<FJsonObject>();
			ToolPostcheck->SetBoolField(TEXT("dryRun"), bDryRun);
			ToolPostcheck->SetBoolField(TEXT("mappingsRequested"), !Mappings.IsEmpty());
			ToolPostcheck->SetBoolField(TEXT("wouldWrite"), bWouldWrite);
			ToolPostcheck->SetStringField(TEXT("inputSystem"), LexToString(EffectiveSystem));
			Content->SetObjectField(TEXT("inputPostcheck"), ToolPostcheck);

			return MakeExecutionResult(
				bDryRun ? TEXT("Player input configuration dry run completed.") : TEXT("Player input configuration applied."),
				Content,
				false);
		}
	}

	bool TryExecutePlayerInputTool(const FString& ToolName, const FJsonObject& Arguments, FUnrealMcpExecutionResult& OutResult)
	{
		if (ToolName == TEXT("unreal.configure_player_input"))
		{
			OutResult = ConfigurePlayerInput(Arguments);
			return true;
		}
		return false;
	}
}
